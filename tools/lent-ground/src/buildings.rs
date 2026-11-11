//! Joins the original game's per-window building footprints into one list (ADR-045).
//!
//! The original builds a footprint that crosses a window edge in every window it reaches, each
//! window clipping it to its own square. Laid side by side, those parts would stand as separate
//! buildings with a wall through the middle. Here the parts of one OpenStreetMap id are joined
//! back into one outline. Each part's eaves and ridge came from the LiDAR returns inside that part
//! only, so the joined heights are the parts' heights weighted by how many roof returns each had;
//! the roof shape and pitch are the largest part's, and the ground is the lowest part's.

use crate::osm::Pt;
use crate::shapes::{self, Shapes};
use serde_json::{json, Value};
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

struct Part {
    window: String,
    record: Value,
    outline: Vec<Pt>,
    area: f64,
    roof_points: f64,
}

fn num(v: &Value, k: &str) -> Option<f64> {
    v.get(k).and_then(Value::as_f64)
}

/// A point inside the outline: the middle of the first span a horizontal line through the middle
/// of its extent crosses. (A U-shaped part's centroid can lie in its courtyard.)
fn interior_point(outline: &[Pt]) -> Pt {
    let (lo, hi) = outline.iter().fold((f64::MAX, f64::MIN), |a, p| (a.0.min(p[1]), a.1.max(p[1])));
    // Nudge off any vertex height so no crossing is counted twice.
    let y = (lo + hi) / 2.0 + 1e-7;
    let n = outline.len();
    let mut xs: Vec<f64> = (0..n)
        .filter_map(|i| {
            let (a, b) = (outline[i], outline[(i + 1) % n]);
            ((a[1] <= y) != (b[1] <= y)).then(|| a[0] + (y - a[1]) / (b[1] - a[1]) * (b[0] - a[0]))
        })
        .collect();
    xs.sort_by(|a, b| a.partial_cmp(b).unwrap());
    if xs.len() < 2 { outline[0] } else { [(xs[0] + xs[1]) / 2.0, y] }
}

/// One building from the parts inside one joined outline.
fn joined_record(parts: &[&Part], outline: &[Pt]) -> Value {
    let main = parts[0];
    let mut record = main.record.clone();
    let weight = |p: &Part| if p.roof_points > 0.0 { p.roof_points } else { p.area };
    let total: f64 = parts.iter().map(|p| weight(p)).sum();
    let mean = |k: &str| -> Option<f64> {
        let (mut s, mut w) = (0.0, 0.0);
        for p in parts {
            if let Some(v) = num(&p.record, k) {
                s += v * weight(p);
                w += weight(p);
            }
        }
        (w > 0.0).then(|| (s / w * 1000.0).round() / 1000.0)
    };
    let lowest = |f: &dyn Fn(&Value) -> Option<f64>| parts.iter().filter_map(|p| f(&p.record)).fold(None, |a: Option<f64>, v| Some(a.map_or(v, |a| a.min(v))));
    let (eaves, ridge) = (mean("eaves"), mean("ridge"));
    let ground = lowest(&|r| num(r, "ground"));
    let terrain_min = lowest(&|r| r["ground_contact"]["terrain_min"].as_f64());
    let obj = record.as_object_mut().unwrap();
    obj.remove("roof_sections");
    obj.insert("outline".into(), Value::Array(outline.iter().map(|p| json!([(p[0] * 100.0).round() / 100.0, (p[1] * 100.0).round() / 100.0])).collect()));
    obj.insert("area".into(), json!((shapes::ring_area(outline).abs() * 10.0).round() / 10.0));
    if let Some(v) = eaves {
        obj.insert("eaves".into(), json!(v));
    }
    if let Some(v) = ridge {
        obj.insert("ridge".into(), json!(v));
    }
    if let Some(v) = ground {
        obj.insert("ground".into(), json!(v));
    }
    if let Some(v) = terrain_min {
        obj.insert("ground_contact".into(), json!({ "terrain_min": v }));
    }
    obj.insert("roof_points".into(), json!(total.round()));
    obj.insert("parts".into(), json!(parts.iter().map(|p| p.window.clone()).collect::<Vec<_>>()));
    obj.insert("window".into(), json!(main.window));
    record
}

/// `sources` are (window name, `<window>_buildings.json`) pairs.
pub fn merge(sources: &[(String, PathBuf)], out: &Path) -> Result<(), String> {
    let mut by_id: BTreeMap<String, Vec<Part>> = BTreeMap::new();
    let mut inputs = Vec::new();
    for (window, path) in sources {
        let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let root: Value = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
        let list = root["buildings"].as_array().ok_or(format!("{}: no buildings", path.display()))?;
        inputs.push(json!({ "window": window, "file": path.file_name().map(|f| f.to_string_lossy().to_string()), "sha256": crate::sha256_file(path), "buildings": list.len() }));
        for b in list {
            let Some(id) = b["id"].as_str() else { continue };
            let outline: Vec<Pt> = b["outline"].as_array().into_iter().flatten().filter_map(|p| Some([p[0].as_f64()?, p[1].as_f64()?])).collect();
            if outline.len() < 3 {
                continue;
            }
            let area = num(b, "area").unwrap_or_else(|| shapes::ring_area(&outline).abs());
            let roof_points = num(b, "roof_points").unwrap_or(0.0);
            by_id.entry(id.to_string()).or_default().push(Part { window: window.clone(), record: b.clone(), outline, area, roof_points });
        }
    }

    let mut buildings = Vec::new();
    let (mut joined, mut detached, mut courtyards) = (0usize, 0usize, 0usize);
    for (id, mut parts) in by_id {
        parts.sort_by(|a, b| b.area.partial_cmp(&a.area).unwrap());
        if parts.len() == 1 {
            let mut record = parts[0].record.clone();
            let obj = record.as_object_mut().unwrap();
            // The roof sections are fitted strips the mesher no longer uses (ADR-034).
            obj.remove("roof_sections");
            obj.insert("window".into(), json!(parts[0].window));
            buildings.push(record);
            continue;
        }
        joined += 1;
        // The raw outlines, all wound the same way, in one overlay: polygons cleaned one by one are
        // each snapped to their own integer grid, and their shared edges would no longer meet.
        let rings: Vec<Shapes> = parts.iter().map(|p| {
            let mut ring = shapes::strip_closing(&p.outline);
            if shapes::ring_area(&ring) < 0.0 {
                ring.reverse();
            }
            vec![vec![ring]]
        }).collect();
        let mut union = shapes::union_all(&rings);
        union.sort_by(|a, b| shapes::ring_area(&b[0]).abs().partial_cmp(&shapes::ring_area(&a[0]).abs()).unwrap());
        // Parts that touch along a window edge become one piece. Pieces that do not touch (a
        // footprint mapped as several separate blocks) stay separate buildings, each with the
        // heights of the parts inside it; the second and later get the id with a suffix.
        for (k, shape) in union.iter().enumerate() {
            let inside: Vec<&Part> = parts.iter().filter(|p| shapes::contains(std::slice::from_ref(shape), interior_point(&p.outline))).collect();
            let inside = if inside.is_empty() { vec![&parts[0]] } else { inside };
            let suffix = if k > 0 { format!("_{}", k + 1) } else { String::new() };
            if k > 0 {
                detached += 1;
            }
            if shape.len() > 1 && inside.len() > 1 {
                // Joined round a courtyard: the mesher builds one outline without holes, so the
                // parts stay separate buildings meeting at a party wall, with the joined heights so
                // their roofs meet.
                courtyards += 1;
                let joined = joined_record(&inside, &shape[0]);
                for (j, p) in inside.iter().enumerate() {
                    let mut record = joined_record(&[*p], &shapes::strip_closing(&p.outline));
                    for k in ["eaves", "ridge", "ground", "ground_contact"] {
                        if let Some(v) = joined.get(k) {
                            record[k] = v.clone();
                        }
                    }
                    record["id"] = json!(if j == 0 { format!("{id}{suffix}") } else { format!("{id}{suffix}_{}", p.window) });
                    buildings.push(record);
                }
                continue;
            }
            let mut record = joined_record(&inside, &shape[0]);
            record["id"] = json!(format!("{id}{suffix}"));
            buildings.push(record);
        }
    }
    let report = json!({
        "format": "marburgumii.lent_buildings",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most (D96/TM E 549527.8, N 157564.0); z above the Drava (252.98 m)",
        "source": "magna-raedarum-marburgum <window>_buildings.json: OpenStreetMap footprints, ARSO 2014 LiDAR roof heights",
        "licence": "ODbL 1.0, © OpenStreetMap contributors",
        "inputs": inputs,
        "joined_across_windows": joined,
        "detached_pieces_kept_apart": detached,
        "courtyard_blocks_left_in_parts": courtyards,
        "buildings": buildings,
    });
    std::fs::write(out, serde_json::to_string(&report).unwrap() + "\n").map_err(|e| e.to_string())?;
    eprintln!("buildings: {} ({} joined across window edges, {} pieces that do not touch kept apart) -> {}", report["buildings"].as_array().unwrap().len(), joined, detached, out.display());
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn part(window: &str, x0: f64, x1: f64, eaves: f64, points: f64) -> Value {
        json!({ "id": "w1", "ground": 1.0, "eaves": eaves, "ridge": eaves + 2.0, "roof": "gabled", "pitch": 30.0,
            "area": (x1 - x0) * 10.0, "roof_points": points, "outline": [[x0, 0.0], [x1, 0.0], [x1, 10.0], [x0, 10.0]],
            "ground_contact": { "terrain_min": if window == "a" { 0.5 } else { 0.2 } } })
    }

    #[test]
    fn parts_cut_by_a_window_edge_join_into_one_footprint() {
        let dir = std::env::temp_dir().join(format!("lent-ground-merge-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let a = dir.join("a_buildings.json");
        let b = dir.join("b_buildings.json");
        std::fs::write(&a, json!({ "buildings": [part("a", 0.0, 30.0, 10.0, 300.0)] }).to_string()).unwrap();
        std::fs::write(&b, json!({ "buildings": [part("b", 30.0, 40.0, 14.0, 100.0)] }).to_string()).unwrap();
        let out = dir.join("out.json");
        merge(&[("a".into(), a), ("b".into(), b)], &out).unwrap();
        let root: Value = serde_json::from_str(&std::fs::read_to_string(&out).unwrap()).unwrap();
        let list = root["buildings"].as_array().unwrap();
        assert_eq!(list.len(), 1);
        let w = &list[0];
        assert_eq!(w["window"], "a");
        assert!((w["area"].as_f64().unwrap() - 400.0).abs() < 0.5);
        assert!((w["eaves"].as_f64().unwrap() - 11.0).abs() < 1e-9);
        assert!((w["ground_contact"]["terrain_min"].as_f64().unwrap() - 0.2).abs() < 1e-9);
        // Joined along x = 30: the shared edge's corners are gone.
        let outline = w["outline"].as_array().unwrap();
        assert_eq!(outline.len(), 4, "{outline:?}");
        std::fs::remove_dir_all(&dir).ok();
    }
    #[test]
    fn a_block_that_would_close_round_a_courtyard_stays_in_parts() {
        // r1630983, cut by the avtobusna / titov_most edge at plan y = -256.
        let a = vec![[645.26, -336.08], [647.98, -327.07], [651.74, -319.72], [657.32, -314.75], [662.39, -314.54], [665.09, -311.95], [664.54, -256.0], [652.02, -256.0], [652.6, -305.34], [642.13, -305.26], [641.45, -309.9], [632.1, -323.87], [625.74, -324.08], [625.31, -302.05], [629.73, -302.05], [628.55, -257.87], [624.19, -257.88], [624.19, -256.0], [609.36, -256.0], [609.49, -268.87], [613.85, -268.83], [614.05, -289.19], [609.94, -289.23], [611.22, -335.75], [614.29, -339.06]];
        let b = vec![[663.79, -179.32], [608.61, -179.85], [609.36, -256.0], [624.19, -256.0], [624.2, -192.69], [651.28, -192.37], [652.02, -256.0], [664.54, -256.0]];
        let dir = std::env::temp_dir().join(format!("lent-ground-courtyard-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let record = |o: &Vec<[f64; 2]>| json!({ "buildings": [{ "id": "r1", "ground": 1.0, "eaves": 10.0, "ridge": 10.0, "roof": "flat", "outline": o }] }).to_string();
        std::fs::write(dir.join("a_buildings.json"), record(&a)).unwrap();
        std::fs::write(dir.join("b_buildings.json"), record(&b)).unwrap();
        let out = dir.join("out.json");
        merge(&[("a".into(), dir.join("a_buildings.json")), ("b".into(), dir.join("b_buildings.json"))], &out).unwrap();
        let root: Value = serde_json::from_str(&std::fs::read_to_string(&out).unwrap()).unwrap();
        // The joined block would close round a courtyard the mesher cannot hollow out, so the two
        // parts stay apart and take the same heights.
        let list = root["buildings"].as_array().unwrap();
        assert_eq!(list.len(), 2);
        assert_eq!(list[0]["id"], "r1");
        assert_eq!(list[1]["id"], "r1_b");
        std::fs::remove_dir_all(&dir).ok();
    }
}
