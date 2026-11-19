//! What kind of street every walk-graph node is on (ADR-068): the original game's zone and activity, read off its
//! own road graph and written onto ours, so the street people know a shopping street from a block of flats.
//!
//! The original's `tools/build_road_graph.py` (`classify`, `annotate_zones`) gave every node of its city-wide walk
//! graph one of eight zones (old town, commercial, residential blocks, residential, industrial, school, park, rural)
//! and an activity (0..1, how busy the street is with people). It worked them out from GURS's cadastre (dwellings
//! and business premises per building), the Center quarter and the heritage listings, OpenStreetMap's industrial and
//! school buildings and the green land-use polygons, for the whole city at once, so a node by the edge of a window
//! still saw the buildings past it. `project/data/road_graph.json` carries the result. Ours is a different graph,
//! built from a newer OpenStreetMap extract (`walk.rs`), so each of our nodes takes the zone and activity of the
//! original's nearest walk node: the original classified on a 20 m grid, and half our nodes have one within 5 m.
//! A node with no original node within `REACH_M` (a path the original did not map, far from any it did) is the
//! original's own answer for a street with nothing near it: rural, activity 0.05.
//!
//! The zones are written in the original's order, which is also `MarburgumStreetContract.h`'s `EZone`.

use crate::sha256_file;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

/// The original's ZONES, in its order.
pub const ZONES: [&str; 8] = ["old_town", "commercial", "residential_blocks", "residential", "industrial", "school", "park", "rural"];
const RURAL: usize = 7;
/// The original's activity for a node with no building near and no green land: `classify`'s rural answer.
const RURAL_ACTIVITY: f64 = 0.05;
/// No original node this close: the original never classified the place.
pub const REACH_M: f64 = 100.0;
const CELL_M: f64 = 20.0;

struct Original {
    /// x, y (plan), zone, activity.
    nodes: Vec<(f64, f64, usize, f64)>,
    cells: HashMap<(i64, i64), Vec<usize>>,
}

impl Original {
    fn read(root: &Value) -> Result<Original, String> {
        let walk = &root["walk"];
        let names: Vec<&str> = walk["zones"].as_array().ok_or("road graph: walk has no zones")?.iter().filter_map(Value::as_str).collect();
        if names != ZONES {
            return Err(format!("road graph: walk zones are {names:?}, expected {ZONES:?}"));
        }
        let fields: Vec<&str> = walk["node_fields"].as_array().ok_or("road graph: walk has no node_fields")?.iter().filter_map(Value::as_str).collect();
        if fields.get(4) != Some(&"zone") || fields.get(5).is_none_or(|f| !f.starts_with("activity")) {
            return Err(format!("road graph: walk node fields are {fields:?}, expected zone and activity at 4 and 5"));
        }
        let coordinates = root["coordinates"].as_str().unwrap_or("");
        if !coordinates.contains("x east and z south") {
            return Err(format!("road graph: coordinates are {coordinates:?}, expected x east and z south of the world origin"));
        }
        let mut nodes = Vec::new();
        let mut cells: HashMap<(i64, i64), Vec<usize>> = HashMap::new();
        for row in walk["nodes"].as_array().ok_or("road graph: walk has no nodes")? {
            let n = |i: usize| row.get(i).and_then(Value::as_f64);
            let (Some(x), Some(y), Some(zone), Some(activity)) = (n(0), n(1), n(4), n(5)) else {
                return Err(format!("road graph: walk node {row} is short"));
            };
            let zone = zone as usize;
            if zone >= ZONES.len() || !(0.0..=1.0).contains(&activity) {
                return Err(format!("road graph: walk node {row} has zone {zone} or activity {activity} out of range"));
            }
            cells.entry(cell_of(x, y)).or_default().push(nodes.len());
            nodes.push((x, y, zone, activity));
        }
        if nodes.is_empty() {
            return Err("road graph: no walk nodes".into());
        }
        Ok(Original { nodes, cells })
    }

    /// The nearest original node within REACH_M, and how far it is.
    fn nearest(&self, x: f64, y: f64) -> Option<(usize, f64)> {
        let (cx, cy) = cell_of(x, y);
        let reach = (REACH_M / CELL_M).ceil() as i64;
        let mut best: Option<(usize, f64)> = None;
        for ring in 0..=reach {
            for dx in -ring..=ring {
                for dy in -ring..=ring {
                    if dx.abs().max(dy.abs()) != ring {
                        continue;
                    }
                    for &i in self.cells.get(&(cx + dx, cy + dy)).into_iter().flatten() {
                        let d = (self.nodes[i].0 - x).hypot(self.nodes[i].1 - y);
                        if d <= REACH_M && best.is_none_or(|(_, b)| d < b) {
                            best = Some((i, d));
                        }
                    }
                }
            }
            // Every cell of a later ring lies at least ring * CELL_M away.
            if best.is_some_and(|(_, d)| d <= ring as f64 * CELL_M) {
                break;
            }
        }
        best
    }
}

fn cell_of(x: f64, y: f64) -> (i64, i64) {
    ((x / CELL_M).floor() as i64, (y / CELL_M).floor() as i64)
}

/// Writes zone and activity onto every node of the walk graph at `walk_path`, from the original's road graph.
/// Running it again replaces what it wrote before.
pub fn annotate(walk_path: &Path, road_graph_path: &Path) -> Result<String, String> {
    let read = |p: &Path| -> Result<Value, String> {
        let text = std::fs::read_to_string(p).map_err(|e| format!("{}: {e}", p.display()))?;
        serde_json::from_str(&text).map_err(|e| format!("{}: {e}", p.display()))
    };
    let original = Original::read(&read(road_graph_path)?)?;
    let mut graph = read(walk_path)?;
    if graph["format"] != "marburgumii.walk_graph" {
        return Err(format!("{} is not a walk graph", walk_path.display()));
    }
    let (summary, rows) = apply(&original, graph["nodes"].as_array().ok_or("walk graph: no nodes")?)?;
    graph["nodes"] = Value::Array(rows);
    graph["zones"] = json!(ZONES);
    graph["node_fields"] = json!(["x", "y", "z", "flags", "zone", "activity(0..1)"]);
    graph["summary"]["zones"] = summary.clone();
    let source = json!({
        "role": "street zones and activity",
        "name": "magna-raedarum-marburgum project/data/road_graph.json (tools/build_road_graph.py classify, annotate_zones): the nearest original walk node's",
        "upstream": "GURS cadastre (dwellings, business premises), Register prostorskih enot, Ministry of Culture heritage register, OpenStreetMap, land use",
        "sha256": sha256_file(road_graph_path),
    });
    let sources = graph["sources"].as_array_mut().ok_or("walk graph: no sources")?;
    sources.retain(|s| s["role"] != "street zones and activity");
    sources.push(source);
    std::fs::write(walk_path, serde_json::to_string(&graph).unwrap() + "\n").map_err(|e| e.to_string())?;
    Ok(summary.to_string())
}

/// The walk graph's node rows with zone and activity appended, and what was found.
fn apply(original: &Original, nodes: &[Value]) -> Result<(Value, Vec<Value>), String> {
    let mut by_zone = [0usize; 8];
    let mut distances = Vec::with_capacity(nodes.len());
    let mut unmatched = 0usize;
    let mut rows = Vec::with_capacity(nodes.len());
    for node in nodes {
        let row = node.as_array().ok_or("walk graph: a node is not a row")?;
        if row.len() < 4 {
            return Err(format!("walk graph: node {node} is short"));
        }
        let (x, y) = (row[0].as_f64().unwrap_or(f64::NAN), row[1].as_f64().unwrap_or(f64::NAN));
        if !x.is_finite() || !y.is_finite() {
            return Err(format!("walk graph: node {node} has no position"));
        }
        let (zone, activity) = match original.nearest(x, y) {
            Some((i, d)) => {
                distances.push(d);
                (original.nodes[i].2, original.nodes[i].3)
            }
            None => {
                unmatched += 1;
                (RURAL, RURAL_ACTIVITY)
            }
        };
        by_zone[zone] += 1;
        let mut out: Vec<Value> = row[..4].to_vec();
        out.push(json!(zone));
        out.push(json!(activity));
        rows.push(Value::Array(out));
    }
    distances.sort_by(f64::total_cmp);
    let at = |q: f64| distances.get(((distances.len() as f64 - 1.0) * q).round() as usize).map_or(0.0, |d| (d * 10.0).round() / 10.0);
    let mut counts = serde_json::Map::new();
    for (i, name) in ZONES.iter().enumerate() {
        counts.insert(name.to_string(), json!(by_zone[i]));
    }
    let summary = json!({
        "nodes_by_zone": counts,
        "nearest_original_m": { "median": at(0.5), "p99": at(0.99), "max": at(1.0) },
        "no_original_within_m": REACH_M,
        "no_original_nodes": unmatched,
    });
    Ok((summary, rows))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn original(nodes: Value) -> Original {
        Original::read(&json!({
            "coordinates": "game metres, x east and z south of the world origin (as the topology)",
            "walk": { "zones": ZONES, "node_fields": ["x", "z", "flags", "component", "zone", "activity(0..1)"], "nodes": nodes }
        }))
        .unwrap()
    }

    #[test]
    fn each_node_takes_the_nearest_original_nodes_zone() {
        let o = original(json!([[0.0, 0.0, 0, 0, 0, 0.9], [30.0, 0.0, 0, 0, 3, 0.4], [300.0, 0.0, 0, 0, 1, 0.8]]));
        let (summary, rows) = apply(&o, &[json!([4.0, 1.0, 2.5, 0]), json!([26.0, -3.0, 2.0, 2]), json!([255.0, 0.0, 1.0, 0])]).unwrap();
        assert_eq!(rows[0], json!([4.0, 1.0, 2.5, 0, 0, 0.9]));
        assert_eq!(rows[1], json!([26.0, -3.0, 2.0, 2, 3, 0.4]));
        // 45 m from the commercial node, across two empty cells
        assert_eq!(rows[2], json!([255.0, 0.0, 1.0, 0, 1, 0.8]));
        assert_eq!(summary["no_original_nodes"], 0);
    }

    #[test]
    fn a_node_nothing_original_is_near_is_rural() {
        let o = original(json!([[0.0, 0.0, 0, 0, 0, 0.9]]));
        let (summary, rows) = apply(&o, &[json!([150.0, 0.0, 0.0, 0])]).unwrap();
        assert_eq!(rows[0], json!([150.0, 0.0, 0.0, 0, 7, 0.05]));
        assert_eq!(summary["no_original_nodes"], 1);
        assert_eq!(summary["nodes_by_zone"]["rural"], 1);
    }

    #[test]
    fn running_again_replaces_what_was_written() {
        let o = original(json!([[0.0, 0.0, 0, 0, 5, 0.6]]));
        let (_, rows) = apply(&o, &[json!([1.0, 1.0, 0.0, 0, 2, 0.3])]).unwrap();
        assert_eq!(rows[0], json!([1.0, 1.0, 0.0, 0, 5, 0.6]));
    }

    #[test]
    fn a_graph_with_other_zones_is_refused() {
        let bad = json!({
            "coordinates": "game metres, x east and z south of the world origin",
            "walk": { "zones": ["old_town", "commercial"], "node_fields": ["x", "z", "flags", "component", "zone", "activity"], "nodes": [] }
        });
        assert!(Original::read(&bad).is_err());
    }
}
