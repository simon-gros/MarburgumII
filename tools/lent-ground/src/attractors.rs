//! Places that draw street people in (ADR-069): the original game's bus stops, cafés, restaurants, bars, fast food,
//! shops and sights, each pinned to a node of our walk graph with a spot to stand at, written into the walk graph.
//!
//! The original's `tools/build_road_graph.py` (`load_attractors`, `annotate_attractors`) put 756 of them on its road
//! graph: bus stops from OpenStreetMap, shops and services from OpenStreetMap's building tags (the door is the point of
//! the building's outline nearest the pavement), cafés, restaurants, bars and fast food from the municipal points, and
//! sights (fountains, monuments, attractions). Each was pinned to the walk node within 40 m that people reach it from.
//! `project/data/road_graph.json` carries the result, and its Unreal port walked people the straight line from that
//! node to the point, through whatever stood between.
//!
//! Ours is a different graph on real ground (`walk.rs`), so each place inside the square is pinned again, to the
//! nearest node of ours (within the original's 40 m, never a node on the carriageway) from which it can be reached
//! without crossing the carriageway, a building or the river. A venue in or on a building takes its door by the
//! original's own rule, on our joined footprint: the point of the outline nearest that node (the original's door was on
//! its own, per-window outline, which our joined blocks may have swallowed). Where people stand is that point, pulled
//! toward that node until it is clear of the carriageway, the river and BODY_CLEAR_M of every building: a door's spot
//! is just outside it, a bus stop mapped on the road is the kerb, and a sight is looked at from SIGHT_STAND_OFF_M off,
//! as the original's port did. A venue's spot must stay within DOOR_REACH_M of its door, or nobody could go in.
//!
//! The kinds are written in the original's order, which is also `MarburgumStreetContract.h`'s `EAttractor`.

use crate::osm::Pt;
use crate::sha256_file;
use crate::shapes::{self, Index, Shapes};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

/// The original's ATTRACTOR_KINDS, in its order.
pub const KINDS: [&str; 7] = ["bus_stop", "cafe", "restaurant", "bar", "fast_food", "shop", "sight"];
const SIGHT: usize = 6;
/// The original's ATTRACTOR_REACH_M: a place farther than this from any pavement is out of reach.
pub const REACH_M: f64 = 40.0;
/// The original's SightStandOff: a sight is looked at from this far off, on the pavement's side.
pub const SIGHT_STAND_OFF_M: f64 = 3.0;
/// Where somebody stands keeps this clear of every building (a body is 0.3 m round, and a wall is not flat).
const BODY_CLEAR_M: f64 = 0.45;
/// A venue's spot is at most this far from its door, or the place is out of reach.
pub const DOOR_REACH_M: f64 = 1.5;
/// The nearest nodes tried for each place.
const TRIES: usize = 8;
/// The way from a node to the spot is checked this often.
const STEP_M: f64 = 0.1;
const CELL_M: f64 = 20.0;

/// What the ground tool knows of the square: the carriageway, the buildings and the river.
pub struct Inputs<'a> {
    pub carriageway: &'a Shapes,
    pub footprints: &'a Shapes,
    pub water: &'a Shapes,
    pub half: f64,
}

struct Ground {
    carriageway: Index,
    blocked: Index,
    /// The buildings themselves, without the margin.
    solid: Index,
    /// Each building (joined block), alone, for finding a venue's door.
    footprints: Vec<Vec<Vec<Pt>>>,
}

impl Ground {
    fn new(inp: &Inputs) -> Ground {
        Ground {
            carriageway: Index::new(inp.carriageway),
            blocked: Index::new(&shapes::union_all(&[shapes::offset(inp.footprints, BODY_CLEAR_M), inp.water.clone()])),
            solid: Index::new(inp.footprints),
            footprints: inp.footprints.clone(),
        }
    }
    fn clear(&self, p: Pt) -> bool {
        !self.carriageway.contains(p) && !self.blocked.contains(p)
    }
    /// The door of the building a venue at p is in or on: the point of its outline nearest the node.
    fn door(&self, p: Pt, node: Pt) -> Option<Pt> {
        let building = self.footprints.iter().find(|b| shapes::contains(std::slice::from_ref(*b), p) || edge_dist(b, p).0 <= BODY_CLEAR_M)?;
        Some(edge_dist(building, node).1)
    }
}

/// The distance from p to the nearest point on a shape's outline, and that point.
fn edge_dist(shape: &[Vec<Pt>], p: Pt) -> (f64, Pt) {
    let mut best = (f64::MAX, p);
    for ring in shape {
        for i in 0..ring.len() {
            let (a, b) = (ring[i], ring[(i + 1) % ring.len()]);
            let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
            let l2 = dx * dx + dy * dy;
            let t = if l2 < 1e-12 { 0.0 } else { (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2).clamp(0.0, 1.0) };
            let q = [a[0] + dx * t, a[1] + dy * t];
            let d = dist(p, q);
            if d < best.0 {
                best = (d, q);
            }
        }
    }
    best
}

fn dist(a: Pt, b: Pt) -> f64 {
    (a[0] - b[0]).hypot(a[1] - b[1])
}

fn lerp(a: Pt, b: Pt, t: f64) -> Pt {
    [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
}

fn round(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}

/// A place of the original's: where it is, and what kind.
struct Original {
    at: Pt,
    kind: usize,
}

fn read_originals(root: &Value) -> Result<Vec<Original>, String> {
    let coordinates = root["coordinates"].as_str().unwrap_or("");
    if !coordinates.contains("x east and z south") {
        return Err(format!("road graph: coordinates are {coordinates:?}, expected x east and z south of the world origin"));
    }
    let attractors = &root["attractors"];
    let kinds: Vec<&str> = attractors["kinds"].as_array().ok_or("road graph: no attractors")?.iter().filter_map(Value::as_str).collect();
    if kinds != KINDS {
        return Err(format!("road graph: attractor kinds are {kinds:?}, expected {KINDS:?}"));
    }
    let fields: Vec<&str> = attractors["fields"].as_array().ok_or("road graph: attractors have no fields")?.iter().filter_map(Value::as_str).collect();
    if fields.get(..3) != Some(&["x", "z", "kind"][..]) {
        return Err(format!("road graph: attractor fields are {fields:?}, expected x, z and kind first"));
    }
    let mut out = Vec::new();
    for row in attractors["items"].as_array().ok_or("road graph: attractors have no items")? {
        let n = |i: usize| row.get(i).and_then(Value::as_f64);
        let (Some(x), Some(z), Some(kind)) = (n(0), n(1), n(2)) else {
            return Err(format!("road graph: attractor {row} is short"));
        };
        let kind = kind as usize;
        if kind >= KINDS.len() {
            return Err(format!("road graph: attractor {row} has kind {kind}"));
        }
        out.push(Original { at: [x, z], kind });
    }
    Ok(out)
}

/// Our walk nodes people may set off from: every node not on the carriageway, bucketed.
struct Nodes {
    at: Vec<Pt>,
    cells: HashMap<(i64, i64), Vec<usize>>,
}

impl Nodes {
    fn new(rows: &[Value], road_flag: u64) -> Result<Nodes, String> {
        let mut at = Vec::with_capacity(rows.len());
        let mut cells: HashMap<(i64, i64), Vec<usize>> = HashMap::new();
        for (i, row) in rows.iter().enumerate() {
            let (Some(x), Some(y), Some(flags)) = (row.get(0).and_then(Value::as_f64), row.get(1).and_then(Value::as_f64), row.get(3).and_then(Value::as_u64)) else {
                return Err(format!("walk graph: node {i} is short"));
            };
            at.push([x, y]);
            if flags & road_flag == 0 {
                cells.entry(cell_of([x, y])).or_default().push(i);
            }
        }
        Ok(Nodes { at, cells })
    }

    /// Nodes within REACH_M of p, nearest first.
    fn near(&self, p: Pt) -> Vec<(usize, f64)> {
        let (cx, cy) = cell_of(p);
        let reach = (REACH_M / CELL_M).ceil() as i64;
        let mut out = Vec::new();
        for dx in -reach..=reach {
            for dy in -reach..=reach {
                for &i in self.cells.get(&(cx + dx, cy + dy)).into_iter().flatten() {
                    let d = dist(self.at[i], p);
                    if d <= REACH_M {
                        out.push((i, d));
                    }
                }
            }
        }
        out.sort_by(|a, b| a.1.total_cmp(&b.1).then(a.0.cmp(&b.0)));
        out
    }
}

fn cell_of(p: Pt) -> (i64, i64) {
    ((p[0] / CELL_M).floor() as i64, (p[1] / CELL_M).floor() as i64)
}

/// The place as seen from `node` (a venue's door), and where somebody from there stands for it: the place (a sight from
/// its stand-off), pulled toward the node until it is clear, if the straight way there from the node is clear too.
fn stand_for(ground: &Ground, at: Pt, kind: usize, node: Pt) -> Result<(Pt, Pt), &'static str> {
    let venue = kind != 0 && kind != SIGHT;
    let at = if venue { ground.door(at, node).unwrap_or(at) } else { at };
    let span = dist(at, node);
    let target = if kind == SIGHT && span > 0.0 { lerp(at, node, SIGHT_STAND_OFF_M.min(span) / span) } else { at };
    let reach = dist(target, node);
    let steps = (reach / STEP_M).ceil().max(1.0) as usize;
    let Some(clear) = (0..steps).map(|k| lerp(target, node, k as f64 / steps as f64)).find(|&p| ground.clear(p)) else {
        return Err("no_spot");
    };
    if venue && dist(clear, at) > DOOR_REACH_M {
        return Err("door_out_of_reach");
    }
    // Out of a building the place is in (a museum, a tower), but never into one: a place behind a building is not
    // reached, or looked at, from the street in front of it.
    let pulled = (dist(at, clear) / STEP_M).ceil().max(1.0) as usize;
    let mut inside = ground.solid.contains(at);
    for k in 1..=pulled {
        let now = ground.solid.contains(lerp(at, clear, k as f64 / pulled as f64));
        if now && !inside {
            return Err("behind_a_building");
        }
        inside = now;
    }
    let way = (dist(clear, node) / STEP_M).ceil().max(1.0) as usize;
    if !(0..=way).all(|k| ground.clear(lerp(node, clear, k as f64 / way as f64))) {
        return Err("way_blocked");
    }
    Ok((at, clear))
}

#[derive(Default)]
struct Counts {
    by_kind: [usize; KINDS.len()],
    refused: HashMap<&'static str, usize>,
    doors_moved: usize,
    node_m: Vec<f64>,
    moved_m: Vec<f64>,
}

/// The rows to write ([x, y, kind, node, stand_x, stand_y]) and what was found.
fn place(ground: &Ground, originals: &[Original], nodes: &Nodes, half: f64) -> (Vec<Value>, Counts) {
    let mut n = Counts::default();
    let mut rows = Vec::new();
    for o in originals {
        if o.at[0].abs() > half || o.at[1].abs() > half {
            continue;
        }
        let near = nodes.near(o.at);
        if near.is_empty() {
            *n.refused.entry("no_node_within_reach").or_default() += 1;
            continue;
        }
        let mut why = "no_node_within_reach";
        let mut found = None;
        for &(node, d) in near.iter().take(TRIES) {
            match stand_for(ground, o.at, o.kind, nodes.at[node]) {
                Ok((at, stand)) => {
                    found = Some((node, d, at, stand));
                    break;
                }
                Err(e) => why = e,
            }
        }
        let Some((node, d, at, stand)) = found else {
            *n.refused.entry(why).or_default() += 1;
            continue;
        };
        n.by_kind[o.kind] += 1;
        n.doors_moved += usize::from(dist(at, o.at) > BODY_CLEAR_M);
        n.node_m.push(d);
        n.moved_m.push(dist(stand, at));
        rows.push(json!([round(at[0]), round(at[1]), o.kind, node, round(stand[0]), round(stand[1])]));
    }
    (rows, n)
}

fn quantiles(mut v: Vec<f64>) -> Value {
    v.sort_by(f64::total_cmp);
    let at = |q: f64| v.get(((v.len() as f64 - 1.0) * q).round() as usize).map_or(0.0, |d| (d * 10.0).round() / 10.0);
    json!({ "median": at(0.5), "p90": at(0.9), "max": at(1.0) })
}

/// Pins the original's places inside the square to the walk graph at `walk_path` and writes them into it. Running it
/// again replaces what it wrote before.
pub fn annotate(inp: &Inputs, walk_path: &Path, road_graph_path: &Path) -> Result<Value, String> {
    let read = |p: &Path| -> Result<Value, String> {
        let text = std::fs::read_to_string(p).map_err(|e| format!("{}: {e}", p.display()))?;
        serde_json::from_str(&text).map_err(|e| format!("{}: {e}", p.display()))
    };
    let originals = read_originals(&read(road_graph_path)?)?;
    let mut graph = read(walk_path)?;
    if graph["format"] != "marburgumii.walk_graph" {
        return Err(format!("{} is not a walk graph", walk_path.display()));
    }
    let road_flag = graph["flags"]["road"].as_u64().unwrap_or(2);
    let nodes = Nodes::new(graph["nodes"].as_array().ok_or("walk graph: no nodes")?, road_flag)?;
    let ground = Ground::new(inp);
    // A spot by the landscape's very edge has no paving under it.
    let (rows, n) = place(&ground, &originals, &nodes, inp.half - 2.0);
    let in_square = originals.iter().filter(|o| o.at[0].abs() <= inp.half - 2.0 && o.at[1].abs() <= inp.half - 2.0).count();
    let mut refused: Vec<(&&str, &usize)> = n.refused.iter().collect();
    refused.sort();
    let summary = json!({
        "attractors": rows.len(),
        "in_square": in_square,
        "by_kind": KINDS.iter().zip(n.by_kind).map(|(k, c)| (k.to_string(), json!(c))).collect::<serde_json::Map<_, _>>(),
        "refused": refused.into_iter().map(|(k, v)| (k.to_string(), json!(v))).collect::<serde_json::Map<_, _>>(),
        "doors_moved_to_our_outline": n.doors_moved,
        "node_m": quantiles(n.node_m),
        "spot_from_place_m": quantiles(n.moved_m),
        "reach_m": REACH_M,
    });
    graph["attractors"] = json!({
        "fields": ["x", "y", "kind", "walk_node", "stand_x", "stand_y"],
        "kinds": KINDS,
        "items": rows,
    });
    graph["summary"]["attractors"] = summary.clone();
    let source = json!({
        "role": "attractors",
        "name": "magna-raedarum-marburgum project/data/road_graph.json attractors (tools/build_road_graph.py load_attractors, annotate_attractors), pinned again to this graph",
        "upstream": "OpenStreetMap (bus stops, building tags), Maribor municipal points (cafés, restaurants, bars, fast food, sights)",
        "sha256": sha256_file(road_graph_path),
    });
    let sources = graph["sources"].as_array_mut().ok_or("walk graph: no sources")?;
    sources.retain(|s| s["role"] != "attractors");
    sources.push(source);
    std::fs::write(walk_path, serde_json::to_string(&graph).unwrap() + "\n").map_err(|e| e.to_string())?;
    Ok(summary)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn square(x0: f64, y0: f64, x1: f64, y1: f64) -> Shapes {
        shapes::polygon(&[vec![[x0, y0], [x1, y0], [x1, y1], [x0, y1]]])
    }

    /// A road along y = 0 (y -4..4), pavements either side, a building north of the north pavement (y > 8).
    fn street() -> (Ground, Nodes) {
        let carriageway = square(-100.0, -4.0, 100.0, 4.0);
        let footprints = square(-20.0, 8.0, 20.0, 30.0);
        let ground = Ground::new(&Inputs { carriageway: &carriageway, footprints: &footprints, water: &Vec::new(), half: 768.0 });
        // north pavement y = 6, south pavement y = -6, and a crossing node on the road
        let rows = vec![json!([0.0, 6.0, 1.0, 0]), json!([0.0, -6.0, 1.0, 0]), json!([30.0, 0.0, 1.0, 2])];
        (ground, Nodes::new(&rows, 2).unwrap())
    }

    #[test]
    fn a_doors_spot_is_just_outside_it() {
        let (ground, nodes) = street();
        let (rows, n) = place(&ground, &[Original { at: [0.0, 8.0], kind: 1 }], &nodes, 768.0);
        assert_eq!(rows.len(), 1, "{:?}", n.refused);
        let row = rows[0].as_array().unwrap();
        assert_eq!(row[3], 0, "the north pavement's node");
        let y = row[5].as_f64().unwrap();
        assert!(y <= 8.0 - BODY_CLEAR_M + 1e-9 && y > 6.0, "spot at y {y}");
    }

    #[test]
    fn a_venue_inside_a_building_has_its_door_on_the_outline_nearest_the_pavement() {
        let (ground, nodes) = street();
        let (rows, n) = place(&ground, &[Original { at: [3.0, 20.0], kind: 5 }], &nodes, 768.0);
        let row = rows[0].as_array().unwrap();
        assert_eq!((row[0].as_f64(), row[1].as_f64(), row[3].as_u64()), (Some(0.0), Some(8.0), Some(0)), "the wall opposite the node");
        assert_eq!(n.doors_moved, 1);
    }

    #[test]
    fn a_bus_stop_mapped_on_the_road_is_waited_at_on_the_kerb() {
        let (ground, nodes) = street();
        let (rows, _) = place(&ground, &[Original { at: [1.0, -1.0], kind: 0 }], &nodes, 768.0);
        let row = rows[0].as_array().unwrap();
        assert_eq!(row[3], 1, "the south pavement's node, the nearer");
        let y = row[5].as_f64().unwrap();
        assert!((-4.2..=-4.0).contains(&y), "spot at y {y}, the kerb");
    }

    #[test]
    fn nobody_crosses_the_road_or_goes_through_a_wall_to_a_place() {
        let (ground, nodes) = street();
        // a sight behind the building, nearest the north pavement: the building is in the way, from either side
        let (rows, n) = place(&ground, &[Original { at: [0.0, 31.0], kind: SIGHT }], &nodes, 768.0);
        assert!(rows.is_empty());
        assert_eq!(n.refused.get("behind_a_building"), Some(&1));
        // a museum mapped inside its building is looked at from the street in front
        let (rows, _) = place(&ground, &[Original { at: [0.0, 20.0], kind: SIGHT }], &nodes, 768.0);
        assert_eq!(rows[0][3], 0);
        // a café door on the far side of the road from its nearest node: the node on its own side is taken
        let carriageway = square(-100.0, -4.0, 100.0, 4.0);
        let empty = Vec::new();
        let ground = Ground::new(&Inputs { carriageway: &carriageway, footprints: &empty, water: &empty, half: 768.0 });
        let rows_in = vec![json!([0.0, 4.5, 1.0, 0]), json!([0.0, -14.0, 1.0, 0])];
        let nodes = Nodes::new(&rows_in, 2).unwrap();
        let (rows, _) = place(&ground, &[Original { at: [0.0, -4.6], kind: 1 }], &nodes, 768.0);
        assert_eq!(rows[0][3], 1, "the nearer node is across the road");
    }

    #[test]
    fn a_sight_is_looked_at_from_a_little_way_off() {
        let empty = Vec::new();
        let ground = Ground::new(&Inputs { carriageway: &empty, footprints: &empty, water: &empty, half: 768.0 });
        let nodes = Nodes::new(&[json!([10.0, 0.0, 1.0, 0])], 2).unwrap();
        let (rows, _) = place(&ground, &[Original { at: [0.0, 0.0], kind: SIGHT }], &nodes, 768.0);
        assert_eq!(rows[0], json!([0.0, 0.0, SIGHT, 0, 3.0, 0.0]));
    }

    #[test]
    fn places_outside_the_square_or_far_from_any_pavement_are_left_out() {
        let empty = Vec::new();
        let ground = Ground::new(&Inputs { carriageway: &empty, footprints: &empty, water: &empty, half: 768.0 });
        let nodes = Nodes::new(&[json!([0.0, 0.0, 1.0, 0])], 2).unwrap();
        let originals = [Original { at: [900.0, 0.0], kind: 1 }, Original { at: [0.0, 45.0], kind: 1 }, Original { at: [0.0, 39.0], kind: 0 }];
        let (rows, n) = place(&ground, &originals, &nodes, 766.0);
        assert_eq!(rows.len(), 1);
        assert_eq!(n.refused.get("no_node_within_reach"), Some(&1));
    }

    #[test]
    fn a_road_graph_with_other_kinds_is_refused() {
        let bad = json!({ "coordinates": "game metres, x east and z south of the world origin",
            "attractors": { "kinds": ["bus_stop", "cafe"], "fields": ["x", "z", "kind", "walk_node"], "items": [] } });
        assert!(read_originals(&bad).is_err());
        let good = json!({ "coordinates": "game metres, x east and z south of the world origin",
            "attractors": { "kinds": KINDS, "fields": ["x", "z", "kind", "walk_node"], "items": [[1.0, 2.0, 3, 9]] } });
        let o = read_originals(&good).unwrap();
        assert_eq!((o[0].at, o[0].kind), ([1.0, 2.0], 3));
    }
}
