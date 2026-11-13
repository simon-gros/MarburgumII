//! The place names of the map (ADR-063): what the location banner calls the quarter or village a player
//! is in, the street or square they are on and the landmark they are at, in one file the server and the
//! client read at run time (`MarburgumPlaces.h`).
//!
//! - Districts are the original game's `project/data/districts.json` (its `tools/build_districts.py`
//!   over GURS's Register prostorskih enot): 11 quarters, 4 local communities and 12 settlements, every
//!   one kept whole, because a ring cut at the map's edge would put its inside in the wrong place.
//! - Landmarks are the original's `project/data/landmarks.json` (its `tools/landmarks.py` over the
//!   Ministry of Culture's heritage register), the tiers asked for (the banner names the bespoke one).
//! - Streets are the named highway ways of this map's own OpenStreetMap extract, the one the paving and
//!   the walk graph are built from, within the square. Unlike the original, which read only its car
//!   roads, pedestrian ways and squares are named too: most of Lent and the old town is pedestrian.
//!   A named square (a pedestrian area, or a `place=square`) is kept as rings, and standing on it is
//!   standing on it.
//!
//! Everything is written in plan metres (x east and y south of the middle of Stari most), so the game
//! needs no projection. Rings and lines are rounded to the centimetre.

use crate::osm::{self, Pt, Tags};
use crate::proj;
use crate::sha256_file;
use serde_json::{json, Value};
use std::path::Path;

/// Highway values that are never a street anyone is on: not built, not public, or not a way at all.
const NOT_STREETS: [&str; 9] = ["construction", "proposed", "abandoned", "platform", "corridor", "elevator", "raceway", "bus_stop", "rest_area"];

/// The car-road classes, as the original's topology marked `car_road`.
const CAR_ROADS: [&str; 15] = [
    "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential", "service", "living_street",
    "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link", "road",
];

/// How far past the square a way may lie and still be read: a street along the edge is still nearest
/// to someone standing at it.
const STREET_MARGIN_M: f64 = 50.0;

fn round(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}

fn pts_json(pts: &[Pt]) -> Value {
    Value::Array(pts.iter().map(|p| json!([round(p[0]), round(p[1])])).collect())
}

fn near_square(pts: &[Pt], half: f64) -> bool {
    let reach = half + STREET_MARGIN_M;
    pts.iter().any(|p| p[0].abs() <= reach && p[1].abs() <= reach)
}

fn is_street(t: &Tags) -> bool {
    let named = t.get("name").is_some_and(|n| !n.trim().is_empty());
    let highway = t.get("highway").map(String::as_str);
    named && highway.is_some_and(|h| !NOT_STREETS.contains(&h))
}

fn is_square(t: &Tags) -> bool {
    t.get("name").is_some_and(|n| !n.trim().is_empty()) && t.get("place").map(String::as_str) == Some("square")
}

/// The named streets and squares of the extract near the square, sorted by name then id.
pub fn streets(osm: &osm::Osm, half: f64) -> Vec<Value> {
    let mut out: Vec<(String, String, Value)> = Vec::new();
    let mut squares = std::collections::HashSet::new();
    for a in &osm.areas {
        if !(is_street(&a.tags) || is_square(&a.tags)) || !a.rings.iter().any(|r| near_square(r, half)) {
            continue;
        }
        let name = a.tags["name"].trim().to_string();
        let highway = a.tags.get("highway").cloned().unwrap_or_else(|| "pedestrian".to_string());
        squares.insert(a.id.clone());
        let rings: Vec<Value> = a.rings.iter().map(|r| pts_json(r)).collect();
        out.push((name.clone(), a.id.clone(), json!({ "id": a.id, "name": name, "highway": highway, "car_road": false, "rings": rings })));
    }
    for w in &osm.ways {
        let id = format!("w{}", w.id);
        if squares.contains(&id) || !is_street(&w.tags) || !near_square(&w.pts, half) {
            continue;
        }
        let name = w.tags["name"].trim().to_string();
        let highway = w.tags["highway"].clone();
        let car_road = CAR_ROADS.contains(&highway.as_str());
        out.push((name.clone(), id.clone(), json!({ "id": id, "name": name, "highway": highway, "car_road": car_road, "line": pts_json(&w.pts) })));
    }
    out.sort_by(|a, b| (&a.0, &a.1).cmp(&(&b.0, &b.1)));
    out.into_iter().map(|(_, _, v)| v).collect()
}

/// A register file's rings, from EPSG:3794 easting/northing into plan metres. The file's own origin
/// must be this map's, or its places would land somewhere else.
fn register_rings(root: &Value, rings: &Value, file: &str) -> Result<Vec<Value>, String> {
    let origin = &root["world_origin"];
    let (east, north) = (origin["east"].as_f64().unwrap_or(f64::NAN), origin["north"].as_f64().unwrap_or(f64::NAN));
    if root["crs"] != "EPSG:3794" || (east - proj::ORIGIN_EAST).abs() > 1e-6 || (north - proj::ORIGIN_NORTH).abs() > 1e-6 {
        return Err(format!("{file} is not EPSG:3794 about Stari most (E {}, N {})", proj::ORIGIN_EAST, proj::ORIGIN_NORTH));
    }
    let mut out = Vec::new();
    for ring in rings.as_array().ok_or(format!("{file}: rings are not a list"))? {
        let pts: Option<Vec<Pt>> = ring
            .as_array()
            .map(|r| r.iter().map(|p| Some(proj::d96_to_plan(p[0].as_f64()?, p[1].as_f64()?))).map(|p| p.map(|(x, y)| [x, y])).collect())
            .unwrap_or(None);
        match pts {
            Some(p) if p.len() >= 3 => out.push(pts_json(&p)),
            _ => return Err(format!("{file}: a ring is not three or more coordinate pairs")),
        }
    }
    if out.is_empty() {
        return Err(format!("{file}: a record has no rings"));
    }
    Ok(out)
}

fn read_json(path: &Path) -> Result<Value, String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
    serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))
}

/// Every district, in file order (quarters, then communities, then settlements).
pub fn districts(root: &Value) -> Result<Vec<Value>, String> {
    let mut out = Vec::new();
    for d in root["districts"].as_array().ok_or("districts.json has no districts")? {
        let area = d["area_m2"].as_f64().filter(|a| *a > 0.0).ok_or("districts.json: a district has no area")?;
        out.push(json!({
            "id": d["id"], "kind": d["kind"], "name": d["name"], "display_name": d["display_name"],
            "area_m2": area, "rings": register_rings(root, &d["rings"], "districts.json")?,
        }));
    }
    Ok(out)
}

/// The landmarks of the given tiers, in register order.
pub fn landmarks(root: &Value, tiers: &[&str]) -> Result<Vec<Value>, String> {
    let mut out = Vec::new();
    for l in root["landmarks"].as_array().ok_or("landmarks.json has no landmarks")? {
        if !l["tier"].as_str().is_some_and(|t| tiers.contains(&t)) {
            continue;
        }
        let area = l["area_m2"].as_f64().filter(|a| *a > 0.0).ok_or("landmarks.json: a landmark has no area")?;
        out.push(json!({
            "eid": l["eid"], "name": l["name"], "display_name": l["display_name"], "tier": l["tier"],
            "dating": l["dating"].as_str().unwrap_or_default().trim().trim_end_matches(',').trim(),
            "area_m2": area, "rings": register_rings(root, &l["outline"], "landmarks.json")?,
        }));
    }
    Ok(out)
}

/// Writes the places file and returns a one-line summary.
pub fn build(osm_path: &Path, osm_text: &str, districts_path: &Path, landmarks_path: &Path, tiers: &[&str], half: f64, out: &Path) -> Result<String, String> {
    let osm = osm::read(osm_text)?;
    let district_root = read_json(districts_path)?;
    let landmark_root = read_json(landmarks_path)?;
    let streets = streets(&osm, half);
    let districts = districts(&district_root)?;
    let landmarks = landmarks(&landmark_root, tiers)?;
    let names: std::collections::BTreeSet<&str> = streets.iter().filter_map(|s| s["name"].as_str()).collect();
    let squares = streets.iter().filter(|s| s.get("rings").is_some()).count();
    let summary = format!(
        "{} districts, {} street ways and squares ({} squares, {} names), {} landmarks ({})",
        districts.len(), streets.len(), squares, names.len(), landmarks.len(), tiers.join(",")
    );
    let doc = json!({
        "format": "marburgumii.places",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most (D96/TM E 549527.8, N 157564.0)",
        "half_extent_m": half,
        "historical_note": "Present-day names and boundaries: the Register prostorskih enot's current units, the heritage register's current entries and today's OpenStreetMap street names. None is yet checked against 1990s Maribor.",
        "generator": "tools/lent-ground places (ADR-063)",
        "sources": [
            { "role": "quarters, local communities and settlements", "name": "magna-raedarum-marburgum project/data/districts.json (tools/build_districts.py)", "upstream": "GURS Register prostorskih enot (RPE)", "licence": "CC BY 4.0, Geodetska uprava Republike Slovenije", "sha256": sha256_file(districts_path) },
            { "role": "landmarks", "name": "magna-raedarum-marburgum project/data/landmarks.json (tools/landmarks.py)", "upstream": "Ministrstvo za kulturo, Register nepremične kulturne dediščine", "licence": "Slovenian public-sector open data, with attribution", "sha256": sha256_file(landmarks_path) },
            { "role": "streets and squares", "name": format!("OpenStreetMap via Overpass, {}", osm.timestamp), "licence": "ODbL 1.0, © OpenStreetMap contributors", "sha256": sha256_file(osm_path) },
        ],
        "summary": { "districts": districts.len(), "street_ways": streets.len(), "squares": squares, "street_names": names.len(), "landmarks": landmarks.len(), "landmark_tiers": tiers },
        "districts": districts,
        "streets": streets,
        "landmarks": landmarks,
    });
    if let Some(dir) = out.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(out, serde_json::to_string(&doc).map_err(|e| e.to_string())? + "\n").map_err(|e| e.to_string())?;
    Ok(summary)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn osm(json: &str) -> osm::Osm {
        osm::read(json).unwrap()
    }

    #[test]
    fn named_ways_and_squares_are_streets_and_the_rest_are_not() {
        // Four nodes just south-east of Stari most, a line and a closed pedestrian area over them.
        let o = osm(r#"{"osm3s":{"timestamp_osm_base":"t"},"elements":[
            {"type":"node","id":1,"lat":46.5555,"lon":15.6430},{"type":"node","id":2,"lat":46.5550,"lon":15.6430},
            {"type":"node","id":3,"lat":46.5550,"lon":15.6440},{"type":"node","id":4,"lat":46.5555,"lon":15.6440},
            {"type":"way","id":10,"nodes":[1,2],"tags":{"highway":"pedestrian","name":"Poštna ulica"}},
            {"type":"way","id":11,"nodes":[2,3],"tags":{"highway":"footway"}},
            {"type":"way","id":12,"nodes":[3,4],"tags":{"highway":"construction","name":"Nova"}},
            {"type":"way","id":13,"nodes":[1,2,3,4,1],"tags":{"highway":"pedestrian","area":"yes","name":"Glavni trg"}},
            {"type":"way","id":14,"nodes":[3,4],"tags":{"highway":"residential","name":"Glavni trg"}}
        ]}"#);
        let s = streets(&o, 768.0);
        let names: Vec<(&str, bool)> = s.iter().map(|v| (v["name"].as_str().unwrap(), v.get("rings").is_some())).collect();
        assert_eq!(names, vec![("Glavni trg", true), ("Glavni trg", false), ("Poštna ulica", false)]);
        assert_eq!(s[1]["car_road"], true);
        assert_eq!(s[2]["car_road"], false);
    }

    #[test]
    fn ways_far_outside_the_square_are_left_out() {
        let o = osm(r#"{"elements":[
            {"type":"node","id":1,"lat":46.60,"lon":15.6430},{"type":"node","id":2,"lat":46.61,"lon":15.6430},
            {"type":"way","id":10,"nodes":[1,2],"tags":{"highway":"residential","name":"Daleč"}}
        ]}"#);
        assert!(streets(&o, 768.0).is_empty());
    }

    #[test]
    fn register_rings_land_in_plan_metres_and_a_foreign_origin_is_refused() {
        let root = json!({ "crs": "EPSG:3794", "world_origin": { "east": 549527.8, "north": 157564.0 } });
        let rings = register_rings(&root, &json!([[[549527.8, 157564.0], [549537.8, 157564.0], [549537.8, 157554.0]]]), "t").unwrap();
        assert_eq!(rings[0], json!([[0.0, 0.0], [10.0, 0.0], [10.0, 10.0]]));
        let moved = json!({ "crs": "EPSG:3794", "world_origin": { "east": 0.0, "north": 0.0 } });
        assert!(register_rings(&moved, &json!([]), "t").is_err());
    }

    #[test]
    fn only_the_asked_tiers_are_kept() {
        let ring = json!([[[549527.8, 157564.0], [549537.8, 157564.0], [549537.8, 157554.0]]]);
        let root = json!({ "crs": "EPSG:3794", "world_origin": { "east": 549527.8, "north": 157564.0 }, "landmarks": [
            { "eid": "1-1", "name": "Maribor - Vodni stolp", "display_name": "Vodni stolp", "tier": "bespoke", "dating": "16. stol., ", "area_m2": 215.0, "outline": ring },
            { "eid": "1-2", "name": "Maribor - Hiša", "display_name": "Hiša", "tier": "generated", "area_m2": 90.0, "outline": ring },
        ]});
        let kept = landmarks(&root, &["bespoke"]).unwrap();
        assert_eq!(kept.len(), 1);
        assert_eq!(kept[0]["display_name"], "Vodni stolp");
        assert_eq!(kept[0]["dating"], "16. stol.");
    }
}
