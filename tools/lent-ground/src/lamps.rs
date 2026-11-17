//! The street lamps of the map (ADR-065): where each one stands, what kind it is and which way it faces, in one
//! file the server and the client read at run time (`MarburgumStreetLights.h`).
//!
//! The original game designed street lighting (its climate roadmap §10: "street lights activating from
//! ambient-light/time thresholds"; its 1990s roadmap §50: orange sodium-vapour streets) but never placed a lamp. The
//! evidence here is this map's own OpenStreetMap extract, the one the paving is built from:
//!
//! - **Mapped lamps.** Every `highway=street_lamp` node in the square, where it stands. Only 19 are mapped, so they
//!   anchor the rest rather than being the rest.
//! - **Lit streets.** A car road tagged `lit=yes` is lit, and so is one with no `lit` tag at all: every street of a
//!   town like Maribor is lit, and an untagged one is unsurveyed, not dark. A road tagged `lit=no` never is. The lamps
//!   are masts with an arm over the carriageway, on the pavement just behind the kerb: on one side of a narrow road,
//!   staggered on both sides of a wide one.
//! - **Pedestrian streets and squares** are streets, lit unless tagged `lit=no`, as the car roads are. **Footways,
//!   cycleways and paths** only where they are tagged `lit=yes`, since a park path is often dark. Their lamps are short
//!   lanterns at the path's edge or round the square. A path running beside a lit road is already lit by the road's
//!   masts and gets none of its own.
//!
//! A lamp never stands in the carriageway, a building, the river or on a bridge deck (whose lamps belong to the
//! bridge), nor where the street people walk (the walk graph, ADR-044), nor on a pedestrian crossing. A candidate that
//! cannot find such a place a little further out is dropped and counted. Lamps closer than the spacing allows to one
//! already placed are dropped too, mapped lamps first, then roads by class, then paths: so a mapped lamp is never
//! moved, and two streets meeting do not double their lamps at the corner.
//!
//! Everything is in plan metres (x east and y south of the middle of Stari most), z the walkable top (`top.bin`),
//! which the game grounds exactly where it loads them. Positions are rounded to the centimetre.

use crate::grid::Grid;
use crate::osm::{Osm, Pt, Tags};
use crate::shapes::{self, Index, Shapes};
use crate::{classify_way, sha256_file, WayKind};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

pub const KINDS: [&str; 2] = ["mast", "lantern"];
const MAST: u8 = 0;
const LANTERN: u8 = 1;

pub const SOURCES: [&str; 4] = ["mapped", "road", "path", "square"];
const MAPPED: u8 = 0;
const ROAD: u8 = 1;
const PATH: u8 = 2;
const SQUARE: u8 = 3;

/// Masts along a road, metres: one side of a narrow road, each side of a wide one (staggered by half).
const ROAD_SPACING_M: f64 = 30.0;
const WIDE_ROAD_SPACING_M: f64 = 36.0;
/// A carriageway at least this wide is lit from both sides.
const WIDE_ROAD_M: f64 = 8.5;
/// A mast stands this far behind the carriageway's edge, just behind the kerb.
const MAST_SETBACK_M: f64 = 0.45;
/// Lanterns along a path or round a square.
const PATH_SPACING_M: f64 = 22.0;
const SQUARE_SPACING_M: f64 = 20.0;
/// A lantern stands this far inside a wide path's edge, or this far outside a narrow one's.
const LANTERN_IN_M: f64 = 0.45;
const LANTERN_OUT_M: f64 = 0.5;
/// A path at least this wide has its lanterns on it rather than beside it.
const WIDE_PATH_M: f64 = 3.0;
/// A square's lanterns stand this far inside its edge.
const SQUARE_IN_M: f64 = 1.2;
/// A path this close to a lit road is lit by the road.
const PATH_BESIDE_ROAD_M: f64 = 7.0;
/// No two lamps closer than this, metres: two masts, or a lantern and anything.
const MAST_GAP_M: f64 = 12.0;
const LANTERN_GAP_M: f64 = 9.0;
/// Clear of the walk graph's lines by this much (a post is 0.2 m across, a walker 0.35 m from their line to their side).
const WALK_CLEAR_M: f64 = 0.55;
/// Clear of a building, of a crossing node and of a junction's middle.
const BUILDING_CLEAR_M: f64 = 0.6;
const CROSSING_CLEAR_M: f64 = 3.0;
/// A candidate that is not clear is tried further out, in these steps, then on the other side.
const NUDGES_M: [f64; 4] = [0.0, 0.4, 0.8, 1.4];
/// A mapped lamp not on clear ground may move this far to be.
const MAPPED_MOVE_M: f64 = 2.5;

/// The car-road classes lit when untagged (as places.rs counts `car_road`, without service ways and tracks, which a
/// town leaves dark as often as not).
const LIT_UNLESS_TAGGED: [&str; 14] = [
    "motorway", "trunk", "primary", "secondary", "tertiary", "unclassified", "residential", "living_street", "busway",
    "motorway_link", "trunk_link", "primary_link", "secondary_link", "tertiary_link",
];

/// Order in which roads claim their lamps: the bigger road first.
fn road_rank(hw: &str) -> u8 {
    match hw.trim_end_matches("_link") {
        "motorway" | "trunk" | "primary" => 0,
        "secondary" => 1,
        "tertiary" => 2,
        "unclassified" | "busway" => 3,
        "residential" => 4,
        "living_street" => 5,
        _ => 6,
    }
}

fn tag<'a>(t: &'a Tags, k: &str) -> Option<&'a str> {
    t.get(k).map(String::as_str)
}

fn dist(a: Pt, b: Pt) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

fn add(a: Pt, d: [f64; 2], s: f64) -> Pt {
    [a[0] + d[0] * s, a[1] + d[1] * s]
}

/// Distance from p to the segment ab.
fn seg_dist(p: Pt, a: Pt, b: Pt) -> f64 {
    let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
    let l2 = dx * dx + dy * dy;
    let t = if l2 < 1e-12 { 0.0 } else { (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2).clamp(0.0, 1.0) };
    dist(p, [a[0] + dx * t, a[1] + dy * t])
}

/// Points every `step` along a polyline, starting half a step in, with the unit direction there. A line shorter than
/// a step gets one point in its middle.
fn along(line: &[Pt], step: f64) -> Vec<(Pt, [f64; 2])> {
    let total: f64 = line.windows(2).map(|w| dist(w[0], w[1])).sum();
    if total < 1e-6 {
        return Vec::new();
    }
    let count = ((total / step).floor() as usize).max(1);
    let gap = total / count as f64;
    let mut out = Vec::new();
    let mut want = gap * 0.5;
    let mut walked = 0.0;
    for w in line.windows(2) {
        let len = dist(w[0], w[1]);
        if len < 1e-6 {
            continue;
        }
        let dir = [(w[1][0] - w[0][0]) / len, (w[1][1] - w[0][1]) / len];
        while want <= walked + len + 1e-9 && out.len() < count {
            out.push((add(w[0], dir, want - walked), dir));
            want += gap;
        }
        walked += len;
    }
    out
}

/// Segments in square buckets, for "anything within r of this point".
struct Segments {
    cell: f64,
    buckets: HashMap<(i64, i64), Vec<(Pt, Pt)>>,
}

impl Segments {
    fn new(cell: f64) -> Self {
        Segments { cell, buckets: HashMap::new() }
    }
    fn key(&self, p: Pt) -> (i64, i64) {
        ((p[0] / self.cell).floor() as i64, (p[1] / self.cell).floor() as i64)
    }
    fn add(&mut self, a: Pt, b: Pt) {
        let (k0, k1) = (self.key([a[0].min(b[0]), a[1].min(b[1])]), self.key([a[0].max(b[0]), a[1].max(b[1])]));
        for i in k0.0..=k1.0 {
            for j in k0.1..=k1.1 {
                self.buckets.entry((i, j)).or_default().push((a, b));
            }
        }
    }
    fn add_line(&mut self, line: &[Pt]) {
        for w in line.windows(2) {
            self.add(w[0], w[1]);
        }
    }
    /// The nearest segment's distance, if one is within r (r no more than a cell).
    fn nearest(&self, p: Pt, r: f64) -> Option<f64> {
        let k = self.key(p);
        let mut best: Option<f64> = None;
        for i in k.0 - 1..=k.0 + 1 {
            for j in k.1 - 1..=k.1 + 1 {
                for &(a, b) in self.buckets.get(&(i, j)).map(Vec::as_slice).unwrap_or(&[]) {
                    let d = seg_dist(p, a, b);
                    if d <= r && best.is_none_or(|x| d < x) {
                        best = Some(d);
                    }
                }
            }
        }
        best
    }
    /// The nearest point on any segment within r, if there is one.
    fn nearest_point(&self, p: Pt, r: f64) -> Option<Pt> {
        let k = self.key(p);
        let mut best: Option<(f64, Pt)> = None;
        for i in k.0 - 1..=k.0 + 1 {
            for j in k.1 - 1..=k.1 + 1 {
                for &(a, b) in self.buckets.get(&(i, j)).map(Vec::as_slice).unwrap_or(&[]) {
                    let (dx, dy) = (b[0] - a[0], b[1] - a[1]);
                    let l2 = dx * dx + dy * dy;
                    let t = if l2 < 1e-12 { 0.0 } else { (((p[0] - a[0]) * dx + (p[1] - a[1]) * dy) / l2).clamp(0.0, 1.0) };
                    let q = [a[0] + dx * t, a[1] + dy * t];
                    let d = dist(p, q);
                    if d <= r && best.is_none_or(|x| d < x.0) {
                        best = Some((d, q));
                    }
                }
            }
        }
        best.map(|b| b.1)
    }
}

/// The lamps placed so far, bucketed for the spacing check.
struct Placed {
    cell: f64,
    buckets: HashMap<(i64, i64), Vec<(Pt, u8)>>,
}

impl Placed {
    fn too_close(&self, p: Pt, kind: u8) -> bool {
        let k = ((p[0] / self.cell).floor() as i64, (p[1] / self.cell).floor() as i64);
        for i in k.0 - 1..=k.0 + 1 {
            for j in k.1 - 1..=k.1 + 1 {
                for &(q, other) in self.buckets.get(&(i, j)).map(Vec::as_slice).unwrap_or(&[]) {
                    let gap = if kind == MAST && other == MAST { MAST_GAP_M } else { LANTERN_GAP_M };
                    if dist(p, q) < gap {
                        return true;
                    }
                }
            }
        }
        false
    }
    fn add(&mut self, p: Pt, kind: u8) {
        let k = ((p[0] / self.cell).floor() as i64, (p[1] / self.cell).floor() as i64);
        self.buckets.entry(k).or_default().push((p, kind));
    }
}

/// Where a lamp may stand.
struct Ground<'a> {
    carriageway: Index,
    blocked: Index,
    bridges: Index,
    inside: f64,
    walk: &'a Segments,
    crossings: &'a [Pt],
}

impl Ground<'_> {
    /// Why p is not clear, or None when it is.
    fn refuse(&self, p: Pt) -> Option<&'static str> {
        if p[0].abs() > self.inside || p[1].abs() > self.inside {
            Some("outside")
        } else if self.carriageway.contains(p) {
            Some("carriageway")
        } else if self.blocked.contains(p) {
            Some("building_or_water")
        } else if self.bridges.contains(p) {
            Some("bridge")
        } else if self.crossings.iter().any(|c| dist(*c, p) < CROSSING_CLEAR_M) {
            Some("crossing")
        } else if self.walk.nearest(p, WALK_CLEAR_M).is_some() {
            Some("walkway")
        } else {
            None
        }
    }
}

#[derive(Default)]
struct Counts {
    refused: HashMap<&'static str, usize>,
    spaced_out: usize,
    candidates: usize,
    by_source: [usize; 4],
    by_kind: [usize; 2],
    mapped_moved: usize,
    mapped_in_square: usize,
    mapped_refused: usize,
    roads_lit_tagged: usize,
    roads_lit_assumed: usize,
    roads_unlit_tagged: usize,
    paths_lit: usize,
    paths_beside_road: usize,
    squares_lit: usize,
    bridges_left: usize,
}

struct Lamp {
    at: Pt,
    kind: u8,
    source: u8,
    /// Degrees, plan: 0 east, 90 south; the way the head reaches (a mast's arm over the road).
    yaw: f64,
}

fn yaw_to(from: Pt, to: Pt) -> f64 {
    let y = (to[1] - from[1]).atan2(to[0] - from[0]).to_degrees();
    if y < 0.0 { y + 360.0 } else { y }
}

pub struct Inputs<'a> {
    pub osm: &'a Osm,
    pub carriageway: &'a Shapes,
    pub footprints: &'a Shapes,
    pub water: &'a Shapes,
    pub bridges: &'a Shapes,
    pub crossings: Vec<Pt>,
    pub top: &'a Grid,
    pub half: f64,
}

/// The lamps, and the summary that goes in the file.
fn place(inp: &Inputs, walk: &Segments) -> (Vec<Lamp>, Counts) {
    let mut n = Counts::default();
    let ground = Ground {
        carriageway: Index::new(inp.carriageway),
        blocked: Index::new(&shapes::union_all(&[shapes::offset(inp.footprints, BUILDING_CLEAR_M), inp.water.clone()])),
        bridges: Index::new(inp.bridges),
        inside: inp.half - 1.5,
        walk,
        crossings: &inp.crossings,
    };
    let near = |pts: &[Pt]| pts.iter().any(|p| p[0].abs() < inp.half + 40.0 && p[1].abs() < inp.half + 40.0);
    let mut placed = Placed { cell: MAST_GAP_M, buckets: HashMap::new() };
    let mut lamps: Vec<Lamp> = Vec::new();
    let accept = |lamps: &mut Vec<Lamp>, placed: &mut Placed, n: &mut Counts, lamp: Lamp| {
        if placed.too_close(lamp.at, lamp.kind) {
            n.spaced_out += 1;
            return;
        }
        placed.add(lamp.at, lamp.kind);
        n.by_source[lamp.source as usize] += 1;
        n.by_kind[lamp.kind as usize] += 1;
        lamps.push(lamp);
    };

    // The lit car roads' centre lines, for facing masts and for paths beside them.
    let mut roads: Vec<(u8, i64, f64, &[Pt])> = Vec::new();
    let mut lit_roads = Segments::new(16.0);
    let mut lit_paths: Vec<(i64, f64, &[Pt])> = Vec::new();
    for w in &inp.osm.ways {
        if !near(&w.pts) {
            continue;
        }
        let hw = tag(&w.tags, "highway").unwrap_or("");
        let lit = tag(&w.tags, "lit");
        let is_bridge = tag(&w.tags, "bridge").is_some_and(|b| b != "no");
        match classify_way(&w.tags) {
            WayKind::Carriageway { width, .. } => {
                let lit_here = match lit {
                    Some("no") | Some("disused") => {
                        n.roads_unlit_tagged += 1;
                        false
                    }
                    Some(_) => {
                        n.roads_lit_tagged += 1;
                        true
                    }
                    None if LIT_UNLESS_TAGGED.contains(&hw) => {
                        n.roads_lit_assumed += 1;
                        true
                    }
                    None => false,
                };
                if lit_here {
                    roads.push((road_rank(hw), w.id, width, &w.pts));
                    lit_roads.add_line(&w.pts);
                }
            }
            // A pedestrian street is a street, lit unless tagged otherwise; a footway or a path only when tagged lit.
            WayKind::Footway { width } if hw == "pedestrian" && !matches!(lit, Some("no") | Some("disused")) => lit_paths.push((w.id, width, &w.pts)),
            WayKind::Footway { width } | WayKind::Unpaved { width } if lit == Some("yes") => lit_paths.push((w.id, width, &w.pts)),
            WayKind::Skip if is_bridge && (lit == Some("yes") || LIT_UNLESS_TAGGED.contains(&hw)) => n.bridges_left += 1,
            _ => {}
        }
    }

    // Mapped lamps, where they stand or as near as is clear.
    let mut mapped: Vec<&crate::osm::Node> = inp.osm.nodes.iter().filter(|nd| tag(&nd.tags, "highway") == Some("street_lamp")).collect();
    mapped.sort_by_key(|nd| nd.id);
    for nd in mapped {
        if nd.at[0].abs() > inp.half || nd.at[1].abs() > inp.half {
            continue;
        }
        n.candidates += 1;
        n.mapped_in_square += 1;
        let road = lit_roads.nearest_point(nd.at, 8.0);
        let kind = if road.is_some() { MAST } else { LANTERN };
        let mut found = None;
        'search: for r in [0.0, 0.5, 1.0, 1.5, 2.0, MAPPED_MOVE_M] {
            let steps = if r == 0.0 { 1 } else { 12 };
            for k in 0..steps {
                let a = k as f64 * std::f64::consts::TAU / steps as f64;
                let p = [nd.at[0] + r * a.cos(), nd.at[1] + r * a.sin()];
                if ground.refuse(p).is_none() {
                    found = Some((p, r > 0.0));
                    break 'search;
                }
            }
        }
        match found {
            Some((p, moved)) => {
                n.mapped_moved += moved as usize;
                let yaw = road.map_or(0.0, |q| yaw_to(p, q));
                accept(&mut lamps, &mut placed, &mut n, Lamp { at: p, kind, source: MAPPED, yaw });
            }
            None => {
                n.mapped_refused += 1;
                *n.refused.entry(ground.refuse(nd.at).unwrap_or("unknown")).or_default() += 1
            }
        }
    }

    // Masts along the lit roads, biggest roads first.
    roads.sort_by(|a, b| (a.0, a.1).cmp(&(b.0, b.1)));
    for (_, _, width, pts) in &roads {
        let wide = *width >= WIDE_ROAD_M;
        let spacing = if wide { WIDE_ROAD_SPACING_M } else { ROAD_SPACING_M };
        let reach = width * 0.5 + MAST_SETBACK_M;
        for (i, (p, dir)) in along(pts, spacing * if wide { 0.5 } else { 1.0 }).into_iter().enumerate() {
            n.candidates += 1;
            // Right of the way's direction (plan y is south: right of east is south), alternating on a wide road.
            let right = [-dir[1], dir[0]];
            let sides: [f64; 2] = if wide && i % 2 == 1 { [-1.0, 1.0] } else { [1.0, -1.0] };
            let sides: &[f64] = if wide { &sides[..1] } else { &sides[..] };
            let mut last = None;
            let mut found = None;
            'sides: for &s in sides {
                for nudge in NUDGES_M {
                    let q = add(p, right, s * (reach + nudge));
                    match ground.refuse(q) {
                        None => {
                            found = Some(q);
                            break 'sides;
                        }
                        Some(why) => last = Some(why),
                    }
                }
            }
            match found {
                Some(q) => accept(&mut lamps, &mut placed, &mut n, Lamp { at: q, kind: MAST, source: ROAD, yaw: yaw_to(q, p) }),
                None => *n.refused.entry(last.unwrap_or("unknown")).or_default() += 1,
            }
        }
    }

    // Lanterns along the lit paths that no road lights.
    lit_paths.sort_by_key(|p| p.0);
    for (_, width, pts) in &lit_paths {
        let beside = pts.iter().filter(|p| lit_roads.nearest(**p, PATH_BESIDE_ROAD_M).is_some()).count();
        if beside * 2 >= pts.len() {
            n.paths_beside_road += 1;
            continue;
        }
        n.paths_lit += 1;
        let reach = if *width >= WIDE_PATH_M { width * 0.5 - LANTERN_IN_M } else { width * 0.5 + LANTERN_OUT_M };
        for (i, (p, dir)) in along(pts, PATH_SPACING_M).into_iter().enumerate() {
            if lit_roads.nearest(p, PATH_BESIDE_ROAD_M).is_some() {
                continue;
            }
            n.candidates += 1;
            let right = [-dir[1], dir[0]];
            let first = if i % 2 == 0 { 1.0 } else { -1.0 };
            let mut last = None;
            let mut found = None;
            'sides: for s in [first, -first] {
                for nudge in NUDGES_M {
                    let q = add(p, right, s * (reach + nudge));
                    match ground.refuse(q) {
                        None => {
                            found = Some(q);
                            break 'sides;
                        }
                        Some(why) => last = Some(why),
                    }
                }
            }
            match found {
                Some(q) => accept(&mut lamps, &mut placed, &mut n, Lamp { at: q, kind: LANTERN, source: PATH, yaw: yaw_to(q, p) }),
                None => *n.refused.entry(last.unwrap_or("unknown")).or_default() += 1,
            }
        }
    }

    // Lanterns round the lit squares and pedestrian areas.
    let mut squares: Vec<&crate::osm::Area> = inp
        .osm
        .areas
        .iter()
        .filter(|a| !matches!(tag(&a.tags, "lit"), Some("no") | Some("disused")))
        .filter(|a| tag(&a.tags, "highway") == Some("pedestrian") || tag(&a.tags, "place") == Some("square") || matches!(tag(&a.tags, "area:highway"), Some("pedestrian" | "footway")))
        .filter(|a| a.rings.iter().any(|r| near(r)))
        .collect();
    squares.sort_by(|a, b| a.id.cmp(&b.id));
    for a in squares {
        n.squares_lit += 1;
        let inner = shapes::offset(&shapes::polygon(&a.rings), -SQUARE_IN_M);
        for shape in &inner {
            let Some(outer) = shape.first() else { continue };
            let mut ring = outer.clone();
            ring.push(ring[0]);
            for (p, dir) in along(&ring, SQUARE_SPACING_M) {
                n.candidates += 1;
                match ground.refuse(p) {
                    None => {
                        // Facing into the square: left of the outer ring's direction is inside for a counter-clockwise
                        // ring in plan (y south), and either way the lantern does not care much.
                        let yaw = yaw_to(p, add(p, [dir[1], -dir[0]], 1.0));
                        accept(&mut lamps, &mut placed, &mut n, Lamp { at: p, kind: LANTERN, source: SQUARE, yaw })
                    }
                    Some(why) => *n.refused.entry(why).or_default() += 1,
                }
            }
        }
    }
    (lamps, n)
}

/// Reads the walk graph's lines (the walk graph JSON written by `walk.rs`).
fn walk_segments(path: &Path) -> Result<(Segments, usize), String> {
    let text = std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let v: Value = serde_json::from_str(&text).map_err(|e| format!("{}: {e}", path.display()))?;
    if v["format"].as_str() != Some("marburgumii.walk_graph") {
        return Err(format!("{} is not a walk graph", path.display()));
    }
    let nodes: Vec<Pt> = v["nodes"].as_array().ok_or("walk graph: no nodes")?.iter().map(|n| [n[0].as_f64().unwrap_or(0.0), n[1].as_f64().unwrap_or(0.0)]).collect();
    let mut seg = Segments::new(4.0);
    let edges = v["edges"].as_array().ok_or("walk graph: no edges")?;
    for e in edges {
        let (a, b) = (e[0].as_u64().unwrap_or(0) as usize, e[1].as_u64().unwrap_or(0) as usize);
        if a < nodes.len() && b < nodes.len() {
            seg.add(nodes[a], nodes[b]);
        }
    }
    Ok((seg, edges.len()))
}

fn round(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}

/// Places the lamps and writes them to `out`; returns the summary.
pub fn build(inp: &Inputs, walk_graph: &Path, osm_path: &Path, out: &Path) -> Result<Value, String> {
    let (walk, walk_edges) = walk_segments(walk_graph)?;
    let (lamps, n) = place(inp, &walk);
    let mut refused: Vec<(&&str, &usize)> = n.refused.iter().collect();
    refused.sort();
    let summary = json!({
        "lamps": lamps.len(),
        "by_kind": { "mast": n.by_kind[0], "lantern": n.by_kind[1] },
        "by_source": { "mapped": n.by_source[0], "road": n.by_source[1], "path": n.by_source[2], "square": n.by_source[3] },
        "candidates": n.candidates,
        "spaced_out": n.spaced_out,
        "refused": refused.into_iter().map(|(k, v)| (k.to_string(), json!(v))).collect::<serde_json::Map<_, _>>(),
        "mapped": { "in_square": n.mapped_in_square, "moved": n.mapped_moved, "refused": n.mapped_refused },
        "roads": { "lit_tagged": n.roads_lit_tagged, "lit_assumed_untagged": n.roads_lit_assumed, "unlit_tagged": n.roads_unlit_tagged },
        "paths": { "lit": n.paths_lit, "lit_by_the_road_beside": n.paths_beside_road },
        "squares_lit": n.squares_lit,
        "bridges_left_to_their_own_lamps": n.bridges_left,
        "walk_graph_edges": walk_edges,
    });
    let doc = json!({
        "format": "marburgumii.street_lights",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most; z the walkable top above the Drava (top.bin), which the game grounds; yaw degrees in plan, 0 east and 90 south, the way the lamp's head reaches",
        "kinds": KINDS,
        "sources": SOURCES,
        "rules": {
            "road_spacing_m": ROAD_SPACING_M, "wide_road_spacing_m": WIDE_ROAD_SPACING_M, "wide_road_m": WIDE_ROAD_M, "mast_setback_m": MAST_SETBACK_M,
            "path_spacing_m": PATH_SPACING_M, "square_spacing_m": SQUARE_SPACING_M, "mast_gap_m": MAST_GAP_M, "lantern_gap_m": LANTERN_GAP_M,
            "walk_clear_m": WALK_CLEAR_M, "building_clear_m": BUILDING_CLEAR_M, "crossing_clear_m": CROSSING_CLEAR_M,
            "lit_unless_tagged": LIT_UNLESS_TAGGED,
        },
        "sources_used": [
            { "role": "lamps, lit streets and paths", "name": format!("OpenStreetMap via Overpass, {}", inp.osm.timestamp), "licence": "ODbL 1.0, © OpenStreetMap contributors", "sha256": sha256_file(osm_path) },
            { "role": "where people walk", "name": "lent_walk_graph.json (lent-ground, ADR-044)", "sha256": sha256_file(walk_graph) },
        ],
        "summary": summary,
        "lamps": lamps.iter().map(|l| json!([round(l.at[0]), round(l.at[1]), round(inp.top.sample(l.at) as f64), l.kind, (l.yaw * 10.0).round() / 10.0, l.source])).collect::<Vec<_>>(),
    });
    if let Some(dir) = out.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(out, serde_json::to_string(&doc).unwrap() + "\n").map_err(|e| e.to_string())?;
    Ok(doc["summary"].clone())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn along_spaces_points_evenly_from_half_a_step_in() {
        let pts = along(&[[0.0, 0.0], [10.0, 0.0], [10.0, 20.0]], 10.0);
        assert_eq!(pts.len(), 3);
        assert!((pts[0].0[0] - 5.0).abs() < 1e-9 && pts[0].0[1].abs() < 1e-9);
        assert!((pts[1].0[0] - 10.0).abs() < 1e-9 && (pts[1].0[1] - 5.0).abs() < 1e-9);
        assert_eq!(pts[1].1, [0.0, 1.0]);
        assert_eq!(along(&[[0.0, 0.0], [3.0, 0.0]], 30.0).len(), 1);
    }

    #[test]
    fn spacing_keeps_masts_twelve_metres_apart_and_lanterns_nine() {
        let mut placed = Placed { cell: MAST_GAP_M, buckets: HashMap::new() };
        placed.add([0.0, 0.0], MAST);
        assert!(placed.too_close([11.0, 0.0], MAST));
        assert!(!placed.too_close([11.0, 0.0], LANTERN));
        assert!(placed.too_close([8.0, 0.0], LANTERN));
        assert!(!placed.too_close([12.5, 0.0], MAST));
    }

    #[test]
    fn segments_find_the_nearest_line() {
        let mut s = Segments::new(4.0);
        s.add_line(&[[0.0, 0.0], [20.0, 0.0]]);
        assert!(s.nearest([10.0, 0.5], 0.75).is_some());
        assert!(s.nearest([10.0, 1.0], 0.75).is_none());
        assert_eq!(s.nearest_point([10.0, 3.0], 4.0), Some([10.0, 0.0]));
    }

    #[test]
    fn yaw_is_plan_degrees_east_then_south() {
        assert!((yaw_to([0.0, 0.0], [1.0, 0.0]) - 0.0).abs() < 1e-9);
        assert!((yaw_to([0.0, 0.0], [0.0, 1.0]) - 90.0).abs() < 1e-9);
        assert!((yaw_to([0.0, 0.0], [0.0, -1.0]) - 270.0).abs() < 1e-9);
    }
}
