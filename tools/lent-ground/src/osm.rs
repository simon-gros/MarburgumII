//! A small reader for an Overpass JSON extract: tagged ways as plan polylines, closed ways and
//! multipolygon relations as plan areas, and tagged nodes as plan points.

use crate::proj::wgs84_to_plan;
use serde_json::Value;
use std::collections::HashMap;

pub type Pt = [f64; 2];
pub type Tags = HashMap<String, String>;

#[allow(dead_code)]
pub struct Way {
    pub id: i64,
    pub tags: Tags,
    pub pts: Vec<Pt>,
    pub closed: bool,
}

#[allow(dead_code)]
pub struct Area {
    pub id: String,
    pub tags: Tags,
    /// Outer rings first, then inner rings; `holes[i]` says whether ring `i` is a hole.
    pub rings: Vec<Vec<Pt>>,
    pub holes: Vec<bool>,
}

#[allow(dead_code)]
pub struct Node {
    pub id: i64,
    pub tags: Tags,
    pub at: Pt,
}

pub struct Osm {
    pub ways: Vec<Way>,
    pub areas: Vec<Area>,
    pub nodes: Vec<Node>,
    pub timestamp: String,
}

fn tags_of(v: &Value) -> Tags {
    v.get("tags")
        .and_then(Value::as_object)
        .map(|o| o.iter().map(|(k, v)| (k.clone(), v.as_str().unwrap_or_default().to_string())).collect())
        .unwrap_or_default()
}

/// Whether a closed way is an area rather than a ring-shaped line (a roundabout, a fence).
fn closed_way_is_area(t: &Tags) -> bool {
    if t.get("area").map(String::as_str) == Some("yes") {
        return true;
    }
    if t.get("area").map(String::as_str) == Some("no") {
        return false;
    }
    if t.contains_key("highway") {
        return t.get("highway").map(String::as_str) == Some("pedestrian") && t.get("area").is_some();
    }
    ["landuse", "leisure", "amenity", "place", "natural", "man_made", "area:highway", "building", "water"]
        .iter()
        .any(|k| t.contains_key(*k))
        && !matches!(t.get("natural").map(String::as_str), Some("tree_row"))
        && !matches!(t.get("barrier").map(String::as_str), Some(_))
}

/// Joins relation member ways end to end into closed rings.
fn assemble_rings(mut parts: Vec<Vec<Pt>>) -> Vec<Vec<Pt>> {
    let same = |a: &Pt, b: &Pt| (a[0] - b[0]).abs() < 1e-6 && (a[1] - b[1]).abs() < 1e-6;
    let mut rings = Vec::new();
    while let Some(mut ring) = parts.pop() {
        loop {
            if ring.len() > 2 && same(&ring[0], ring.last().unwrap()) {
                rings.push(ring);
                break;
            }
            let end = *ring.last().unwrap();
            let Some(i) = parts.iter().position(|p| same(&p[0], &end) || same(p.last().unwrap(), &end)) else {
                break; // an open fragment, cut by the extract's bounding box
            };
            let mut next = parts.swap_remove(i);
            if !same(&next[0], &end) {
                next.reverse();
            }
            ring.extend(next.into_iter().skip(1));
        }
    }
    rings
}

pub fn read(text: &str) -> Result<Osm, String> {
    let root: Value = serde_json::from_str(text).map_err(|e| e.to_string())?;
    let elements = root["elements"].as_array().ok_or("no elements")?;
    let mut coords: HashMap<i64, Pt> = HashMap::new();
    for e in elements {
        if e["type"] == "node" {
            if let (Some(lat), Some(lon)) = (e["lat"].as_f64(), e["lon"].as_f64()) {
                let (x, y) = wgs84_to_plan(lat, lon);
                coords.insert(e["id"].as_i64().unwrap_or(0), [x, y]);
            }
        }
    }
    let mut way_pts: HashMap<i64, Vec<Pt>> = HashMap::new();
    let mut osm = Osm {
        ways: Vec::new(),
        areas: Vec::new(),
        nodes: Vec::new(),
        timestamp: root["osm3s"]["timestamp_osm_base"].as_str().unwrap_or_default().to_string(),
    };
    for e in elements {
        let tags = tags_of(e);
        match e["type"].as_str() {
            Some("node") if !tags.is_empty() => {
                let id = e["id"].as_i64().unwrap_or(0);
                if let Some(at) = coords.get(&id) {
                    osm.nodes.push(Node { id, tags, at: *at });
                }
            }
            Some("way") => {
                let id = e["id"].as_i64().unwrap_or(0);
                let pts: Vec<Pt> = e["nodes"]
                    .as_array()
                    .map(|a| a.iter().filter_map(|n| n.as_i64().and_then(|n| coords.get(&n).copied())).collect())
                    .unwrap_or_default();
                if pts.len() < 2 {
                    continue;
                }
                way_pts.insert(id, pts.clone());
                if tags.is_empty() {
                    continue;
                }
                let closed = pts.len() > 3 && pts[0] == *pts.last().unwrap();
                if closed && closed_way_is_area(&tags) {
                    osm.areas.push(Area { id: format!("w{id}"), tags: tags.clone(), rings: vec![pts.clone()], holes: vec![false] });
                }
                osm.ways.push(Way { id, tags, pts, closed });
            }
            _ => {}
        }
    }
    for e in elements {
        if e["type"] != "relation" {
            continue;
        }
        let tags = tags_of(e);
        if tags.get("type").map(String::as_str) != Some("multipolygon") {
            continue;
        }
        let (mut outer, mut inner) = (Vec::new(), Vec::new());
        for m in e["members"].as_array().into_iter().flatten() {
            if m["type"] != "way" {
                continue;
            }
            let Some(pts) = m["ref"].as_i64().and_then(|r| way_pts.get(&r)) else { continue };
            if m["role"] == "inner" {
                inner.push(pts.clone());
            } else {
                outer.push(pts.clone());
            }
        }
        let outer = assemble_rings(outer);
        let inner = assemble_rings(inner);
        if outer.is_empty() {
            continue;
        }
        let holes = outer.iter().map(|_| false).chain(inner.iter().map(|_| true)).collect();
        let rings = outer.into_iter().chain(inner).collect();
        osm.areas.push(Area { id: format!("r{}", e["id"].as_i64().unwrap_or(0)), tags, rings, holes });
    }
    Ok(osm)
}
