//! The street furniture of the map (GFX-01, VQ-023): the original game's street kit (stop posts, bus shelters,
//! benches, fence and wall panels, bollards), placed from this map's OpenStreetMap extract by the original's rules, in
//! one file the server and the client read at run time (`MarburgumStreetFurniture.h`).
//!
//! The original's `MarburgumStreetKit` (branch `feat/unreal-street-kit`) furnished each window from its OSM topology:
//!
//! - **Stops.** A `public_transport=platform` is where people wait (a `stop_position` is on the road and is left
//!   alone). Every platform gets a stop post where it is mapped. A shelter stands only where the survey records
//!   `shelter=yes`, set back SHELTER_SETBACK_M behind the post and open toward the road; a bench only where it records
//!   `bench=yes`, inside the shelter or beside the post. A blocked shelter slides along the kerb, 2, 4 then 6 m, right
//!   before left, and is left out when none of those is clear.
//! - **Barrier runs.** Fences and gates become 2 m fence panels, walls 2 m wall panels, a `barrier=bollard` way a row
//!   of bollards every 1.5 m. Kerbs (the paving owns them), hedges (vegetation) and retaining walls (the ground owns
//!   slopes, ADR-040) are counted and not furnished.
//!
//! This map adds what the original's import did not have to look at:
//!
//! - **Mapped benches and bollards.** Every `amenity=bench` and `barrier=bollard` node of the extract, not only those at
//!   stops. A bench faces its `direction` tag, or else the path or road nearest it.
//! - **Nobody's way is blocked.** The street people follow the walk graph (ADR-044). A solid piece keeps its footprint
//!   WALK_CLEAR_M off every walk line; a bench or bollard that is not clear is tried up to BENCH_MOVE_M further from the
//!   line, a panel whose 2 m span crosses a line is left out, which leaves a gap where people walk through.
//! - **Trees stand where they stand** (the original's D12). The plants of `lent_plants.json` are the better
//!   measurement, so furniture never stands within TRUNK_CLEAR_M of a trunk; the stop's furniture slides instead.
//! - Nothing stands in the carriageway, a building, the river or on a bridge deck.
//!
//! Positions are in plan metres (x east, y south of the middle of Stari most), z the walkable top (`top.bin`), which the
//! game grounds. Yaw is the plan direction a piece's front faces (0 east, 90 south): the open side of a shelter, the way a
//! bench's sitter looks, across a panel's run.

use crate::grid::Grid;
use crate::lamps::{walk_segments, Segments};
use crate::osm::{Osm, Pt, Tags};
use crate::sha256_file;
use crate::shapes::{Index, Shapes};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::Path;

/// The kit, in the original's `EPiece` order, which is also `MarburgumStreetFurniture.h`'s `EPiece`.
pub const PIECES: [&str; 7] = ["stop_post", "shelter", "bench", "fence_panel", "wall_panel", "bollard", "quay_railing"];
const STOP_POST: u8 = 0;
const SHELTER: u8 = 1;
const BENCH: u8 = 2;
const FENCE_PANEL: u8 = 3;
const WALL_PANEL: u8 = 4;
const BOLLARD: u8 = 5;
const QUAY_RAILING: u8 = 6;

pub const SOURCES: [&str; 5] = ["stop", "bench", "bollard", "barrier", "quay"];
const STOP: u8 = 0;
const MAPPED_BENCH: u8 = 1;
const MAPPED_BOLLARD: u8 = 2;
const BARRIER: u8 = 3;
const QUAY: u8 = 4;

/// Footprints, metres across (along the front) and deep (front to back), from the original's kit.
const SHELTER_SIZE: [f64; 2] = [3.6, 1.4];
const BENCH_SIZE: [f64; 2] = [1.8, 0.55];
const POST_SIZE: [f64; 2] = [0.44, 0.09];
const BOLLARD_SIZE: [f64; 2] = [0.15, 0.15];
const PANEL_MODULE_M: f64 = 2.0;
const PANEL_DEPTH: [f64; 2] = [0.1, 0.32];
/// A quay railing module, along its run and across.
const QUAY_MODULE: [f64; 2] = [2.0, 0.1];
const BOLLARD_SPACING_M: f64 = 1.5;
/// The original's stop arrangement, metres in the stop's frame (right of the post, behind it).
const SHELTER_SETBACK_M: f64 = 1.30;
const BENCH_IN_SHELTER_M: f64 = 1.60;
const BENCH_ASIDE: [f64; 2] = [1.40, 0.55];
const SLIDE_STEPS_M: [f64; 3] = [2.0, 4.0, 6.0];
/// A stop's road: the carriageway's edge within this distance of its post.
const ROAD_REACH_M: f64 = 12.0;
/// A solid footprint's edge keeps this far from every walk line (a walker is 0.35 m from their line to their side).
const WALK_CLEAR_M: f64 = 0.4;
/// A post this thin keeps only the walker's own half-width.
const THIN_WALK_CLEAR_M: f64 = 0.35;
/// A mapped bench or bollard that is not clear is tried this far further from the line, in steps of 0.25 m.
const BENCH_MOVE_M: f64 = 1.5;
/// Clear of a building and of a tree's trunk (on top of the trunk's own radius).
const BUILDING_CLEAR_M: f64 = 0.15;
const TRUNK_CLEAR_M: f64 = 0.3;
/// A bench faces the walk line or road within this distance; beyond it, its yaw comes from its position.
const BENCH_FACE_REACH_M: f64 = 4.0;
/// Two platforms closer than this are one stop (a node and the way drawn round it).
const SAME_STOP_M: f64 = 8.0;
/// Inside the square by this much.
const EDGE_M: f64 = 1.5;

fn dist(a: Pt, b: Pt) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

fn tag<'a>(t: &'a Tags, k: &str) -> Option<&'a str> {
    t.get(k).map(String::as_str)
}

/// Plan yaw, degrees in [0, 360), of the direction from a to b.
fn yaw_to(a: Pt, b: Pt) -> f64 {
    let y = (b[1] - a[1]).atan2(b[0] - a[0]).to_degrees();
    if y < 0.0 { y + 360.0 } else { y }
}

/// The unit vector a plan yaw points along.
fn unit(yaw: f64) -> [f64; 2] {
    let r = yaw.to_radians();
    [r.cos(), r.sin()]
}

/// A point `right` to the right of and `behind` behind `at`, for something facing `yaw`. In plan, x east and y south,
/// a piece facing east has its right to the south.
fn offset(at: Pt, yaw: f64, right: f64, behind: f64) -> Pt {
    let f = unit(yaw);
    let r = [-f[1], f[0]];
    [at[0] + r[0] * right - f[0] * behind, at[1] + r[1] * right - f[1] * behind]
}

/// A compass `direction` tag (degrees from north, clockwise, or N/NE/...) as a plan yaw.
fn compass(v: &str) -> Option<f64> {
    let deg = match v.trim() {
        "N" => 0.0,
        "NE" => 45.0,
        "E" => 90.0,
        "SE" => 135.0,
        "S" => 180.0,
        "SW" => 225.0,
        "W" => 270.0,
        "NW" => 315.0,
        s => s.parse::<f64>().ok()?,
    };
    Some((deg - 90.0).rem_euclid(360.0))
}

/// A cheap position hash in [0, 1), for a yaw nothing else decides.
fn hash01(p: Pt) -> f64 {
    let v = (p[0] * 12.9898 + p[1] * 78.233).sin() * 43758.5453;
    v - v.floor()
}

/// A tree's trunk, from the plants file.
struct Trunk {
    at: Pt,
    radius: f64,
}

fn read_trunks(path: &Path) -> Result<Vec<Trunk>, String> {
    let root: Value = serde_json::from_str(&std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?).map_err(|e| format!("{}: {e}", path.display()))?;
    if root["format"].as_str() != Some("marburgumii.vegetation") {
        return Err(format!("{} is not a vegetation file", path.display()));
    }
    let species: Vec<(bool, f64)> = root["species"].as_array().ok_or("vegetation: no species")?.iter().map(|s| (s["tree"].as_bool().unwrap_or(false), s["trunk_radius_m"].as_f64().unwrap_or(0.0))).collect();
    let mut out = Vec::new();
    for row in root["plants"].as_array().ok_or("vegetation: no plants")? {
        let (Some(x), Some(y), Some(kind), Some(spread)) = (row[0].as_f64(), row[1].as_f64(), row[3].as_u64(), row[5].as_f64()) else { continue };
        if let Some(&(true, radius)) = species.get(kind as usize) {
            // As the game's trunk blocker: at least 10 cm round.
            out.push(Trunk { at: [x, y], radius: (radius * spread).max(0.1) });
        }
    }
    Ok(out)
}

/// What the ground tool knows of the square.
pub struct Inputs<'a> {
    pub osm: &'a Osm,
    pub carriageway: &'a Shapes,
    /// The paved walkable ground (pavements and squares), whose edge over the river carries the quay railing.
    pub walkable: &'a Shapes,
    pub heights: &'a Grid,
    pub footprints: &'a Shapes,
    pub water: &'a Shapes,
    /// The river alone, whose quays carry the railing (other water, such as a park pond, keeps its open edge).
    pub river: &'a Shapes,
    pub bridges: &'a Shapes,
    pub top: &'a Grid,
    pub half: f64,
}

struct Ground<'a> {
    carriageway: Index,
    buildings: Index,
    water: Index,
    bridges: Index,
    walk: &'a Segments,
    kerb: &'a Segments,
    trunks: HashMap<(i64, i64), Vec<Trunk>>,
    inside: f64,
}

const TRUNK_CELL_M: f64 = 8.0;

impl Ground<'_> {
    fn near_trunk(&self, p: Pt, reach: f64) -> bool {
        let k = ((p[0] / TRUNK_CELL_M).floor() as i64, (p[1] / TRUNK_CELL_M).floor() as i64);
        (k.0 - 1..=k.0 + 1).any(|i| (k.1 - 1..=k.1 + 1).any(|j| self.trunks.get(&(i, j)).is_some_and(|ts| ts.iter().any(|t| dist(t.at, p) < t.radius + reach))))
    }

    /// Why a rectangle of `size` (across, deep) centred at p and facing yaw cannot stand, or None when it can.
    /// `walk_clear` is how far its edge keeps from the walk lines (0 for none).
    fn refuse(&self, p: Pt, yaw: f64, size: [f64; 2], walk_clear: f64) -> Option<&'static str> {
        let (half_across, half_deep) = (size[0] / 2.0, size[1] / 2.0);
        // Sample the footprint every 0.25 m or so, edges included.
        let steps = |h: f64| ((2.0 * h / 0.25).ceil() as i32).max(1);
        let (na, nd) = (steps(half_across), steps(half_deep));
        let mut points = Vec::new();
        for i in 0..=na {
            for j in 0..=nd {
                let right = -half_across + 2.0 * half_across * i as f64 / na as f64;
                let behind = -half_deep + 2.0 * half_deep * j as f64 / nd as f64;
                points.push(offset(p, yaw, right, behind));
            }
        }
        for &q in &points {
            if q[0].abs() > self.inside || q[1].abs() > self.inside {
                return Some("outside");
            }
            if self.carriageway.contains(q) {
                return Some("carriageway");
            }
            if self.water.contains(q) {
                return Some("water");
            }
            if self.bridges.contains(q) {
                return Some("bridge");
            }
            if self.buildings.contains(q) {
                return Some("building");
            }
            if self.near_trunk(q, TRUNK_CLEAR_M) {
                return Some("tree");
            }
            if walk_clear > 0.0 && self.walk.nearest(q, walk_clear).is_some() {
                return Some("walkway");
            }
        }
        None
    }

    /// The point of the carriageway's edge nearest p, within ROAD_REACH_M.
    fn kerb_near(&self, p: Pt) -> Option<Pt> {
        self.kerb.nearest_point(p, ROAD_REACH_M)
    }
}

pub(crate) struct Piece {
    pub(crate) at: Pt,
    pub(crate) kind: u8,
    pub(crate) yaw: f64,
    pub(crate) source: u8,
}

#[derive(Default)]
struct Counts {
    refused: HashMap<String, usize>,
    platforms: usize,
    platforms_same_stop: usize,
    stops: usize,
    stops_without_road: usize,
    shelters_asked: usize,
    shelters_slid: usize,
    benches_asked: usize,
    benches_moved: usize,
    bollards_moved: usize,
    barrier_runs: HashMap<String, usize>,
    barrier_m: f64,
    panels_gapped_for_walkers: usize,
}

impl Counts {
    fn refuse(&mut self, what: &str, why: &str) {
        *self.refused.entry(format!("{what}:{why}")).or_default() += 1;
    }
}

fn place(inp: &Inputs, g: &Ground, n: &mut Counts) -> Vec<Piece> {
    let mut out: Vec<Piece> = Vec::new();

    // ---- Stops ------------------------------------------------------------------------------------
    let mut platforms: Vec<(Pt, &Tags)> = Vec::new();
    for node in &inp.osm.nodes {
        if tag(&node.tags, "public_transport") == Some("platform") {
            platforms.push((node.at, &node.tags));
        }
    }
    for way in &inp.osm.ways {
        if tag(&way.tags, "public_transport") == Some("platform") && !way.pts.is_empty() {
            let mid = way.pts[way.pts.len() / 2];
            let c = [way.pts.iter().map(|p| p[0]).sum::<f64>() / way.pts.len() as f64, way.pts.iter().map(|p| p[1]).sum::<f64>() / way.pts.len() as f64];
            platforms.push((if way.pts.len() > 2 { c } else { mid }, &way.tags));
        }
    }
    let mut stops: Vec<Pt> = Vec::new();
    for (at, tags) in platforms {
        if at[0].abs() > inp.half || at[1].abs() > inp.half {
            continue;
        }
        n.platforms += 1;
        if stops.iter().any(|s| dist(*s, at) < SAME_STOP_M) {
            n.platforms_same_stop += 1;
            continue;
        }
        stops.push(at);
        let Some(kerb) = g.kerb_near(at) else {
            // Nothing says which way the stop faces; a post alone would be a guess at a stop on a path.
            n.stops_without_road += 1;
            continue;
        };
        let facing = yaw_to(at, kerb);
        // The post: where it is mapped, or pulled back from the road until it is clear.
        let post = (0..=6).map(|i| offset(at, facing, 0.0, 0.25 * i as f64)).find(|&p| g.refuse(p, facing, POST_SIZE, THIN_WALK_CLEAR_M).is_none());
        let Some(post) = post else {
            n.refuse("stop_post", g.refuse(at, facing, POST_SIZE, THIN_WALK_CLEAR_M).unwrap_or("unknown"));
            continue;
        };
        n.stops += 1;
        out.push(Piece { at: post, kind: STOP_POST, yaw: facing, source: STOP });
        let shelter = tag(tags, "shelter") == Some("yes");
        let bench = tag(tags, "bench") == Some("yes");
        let mut sheltered = false;
        if shelter {
            n.shelters_asked += 1;
            let slides = std::iter::once(0.0).chain(SLIDE_STEPS_M.iter().flat_map(|&s| [s, -s]));
            let mut first_refusal = None;
            for (k, slide) in slides.enumerate() {
                let centre = offset(post, facing, slide, SHELTER_SETBACK_M + SHELTER_SIZE[1] / 2.0);
                match g.refuse(centre, facing, SHELTER_SIZE, WALK_CLEAR_M) {
                    None => {
                        out.push(Piece { at: centre, kind: SHELTER, yaw: facing, source: STOP });
                        n.shelters_slid += usize::from(k > 0);
                        sheltered = true;
                        if bench {
                            n.benches_asked += 1;
                            out.push(Piece { at: offset(post, facing, slide, BENCH_IN_SHELTER_M + BENCH_SIZE[1] / 2.0), kind: BENCH, yaw: facing, source: STOP });
                        }
                        break;
                    }
                    Some(why) => {
                        first_refusal.get_or_insert(why);
                    }
                }
            }
            if !sheltered {
                n.refuse("shelter", first_refusal.unwrap_or("unknown"));
            }
        }
        if bench && !sheltered {
            n.benches_asked += 1;
            let at = offset(post, facing, BENCH_ASIDE[0], BENCH_ASIDE[1]);
            match g.refuse(at, facing, BENCH_SIZE, WALK_CLEAR_M) {
                None => out.push(Piece { at, kind: BENCH, yaw: facing, source: STOP }),
                Some(why) => n.refuse("stop_bench", why),
            }
        }
    }

    // ---- Mapped benches and bollards ------------------------------------------------------------------
    let taken = |out: &[Piece], p: Pt, r: f64| out.iter().any(|q| matches!(q.kind, BENCH | BOLLARD | STOP_POST) && dist(q.at, p) < r);
    for node in &inp.osm.nodes {
        let is_bench = tag(&node.tags, "amenity") == Some("bench");
        let is_bollard = tag(&node.tags, "barrier") == Some("bollard");
        if !(is_bench || is_bollard) || node.at[0].abs() > inp.half || node.at[1].abs() > inp.half {
            continue;
        }
        if is_bench {
            n.benches_asked += 1;
            if taken(&out, node.at, 1.0) {
                n.refuse("bench", "at_a_stop");
                continue;
            }
            // Facing: the tag, else the walk line or the road it sits by.
            let line = g.walk.nearest_point(node.at, BENCH_FACE_REACH_M);
            let facing = tag(&node.tags, "direction")
                .and_then(compass)
                .or_else(|| line.filter(|q| dist(*q, node.at) > 0.05).map(|q| yaw_to(node.at, q)))
                .or_else(|| g.kerb_near(node.at).filter(|q| dist(*q, node.at) < BENCH_FACE_REACH_M).map(|q| yaw_to(node.at, q)))
                .unwrap_or(hash01(node.at) * 360.0);
            // Not clear: back away from what it faces, up to BENCH_MOVE_M.
            let tries = (0..=(BENCH_MOVE_M / 0.25) as usize).map(|i| offset(node.at, facing, 0.0, 0.25 * i as f64));
            match tries.enumerate().find(|(_, p)| g.refuse(*p, facing, BENCH_SIZE, WALK_CLEAR_M).is_none()) {
                Some((i, at)) => {
                    n.benches_moved += usize::from(i > 0);
                    out.push(Piece { at, kind: BENCH, yaw: facing, source: MAPPED_BENCH });
                }
                None => n.refuse("bench", g.refuse(node.at, facing, BENCH_SIZE, WALK_CLEAR_M).unwrap_or("unknown")),
            }
        } else {
            place_bollard(g, n, &mut out, node.at, MAPPED_BOLLARD);
        }
    }

    // ---- Barrier runs -----------------------------------------------------------------------------
    for way in &inp.osm.ways {
        let Some(kind) = tag(&way.tags, "barrier") else { continue };
        if way.pts.len() < 2 || !way.pts.iter().any(|p| p[0].abs() < inp.half && p[1].abs() < inp.half) {
            continue;
        }
        *n.barrier_runs.entry(kind.to_string()).or_default() += 1;
        let piece = match kind {
            "fence" | "gate" | "railing" | "guard_rail" => FENCE_PANEL,
            "wall" | "city_wall" => WALL_PANEL,
            "bollard" => BOLLARD,
            // Kerbs belong to the paving, hedges to the vegetation, retaining walls to the ground.
            _ => continue,
        };
        for w in way.pts.windows(2) {
            let (a, b) = (w[0], w[1]);
            let len = dist(a, b);
            if len < 1e-3 {
                continue;
            }
            n.barrier_m += len;
            let d = [(b[0] - a[0]) / len, (b[1] - a[1]) / len];
            let run = yaw_to(a, b);
            if piece == BOLLARD {
                let count = (len / BOLLARD_SPACING_M).floor() as usize + 1;
                for i in 0..count {
                    let t = if count == 1 { len / 2.0 } else { i as f64 * len / (count - 1) as f64 };
                    place_bollard(g, n, &mut out, [a[0] + d[0] * t, a[1] + d[1] * t], BARRIER);
                }
                continue;
            }
            // Whole modules, centred on the segment; a remainder under half a module is left open.
            let count = (len / PANEL_MODULE_M + 0.5).floor() as usize;
            let start = (len - count as f64 * PANEL_MODULE_M) / 2.0;
            let depth = if piece == WALL_PANEL { PANEL_DEPTH[1] } else { PANEL_DEPTH[0] };
            for i in 0..count {
                let t = start + (i as f64 + 0.5) * PANEL_MODULE_M;
                let centre = [a[0] + d[0] * t, a[1] + d[1] * t];
                // The panel's front faces across its run, so its width lies along it.
                let facing = (run + 90.0).rem_euclid(360.0);
                match g.refuse(centre, facing, [PANEL_MODULE_M, depth], WALK_CLEAR_M) {
                    None => out.push(Piece { at: centre, kind: piece, yaw: facing, source: BARRIER }),
                    // Where a walk line crosses the run, people walk through a gap.
                    Some("walkway") => n.panels_gapped_for_walkers += 1,
                    Some(why) => n.refuse(PIECES[piece as usize], why),
                }
            }
        }
    }
    out
}

/// A bollard where it is mapped or, when that is not clear, up to BENCH_MOVE_M off the walk line it is on.
fn place_bollard(g: &Ground, n: &mut Counts, out: &mut Vec<Piece>, at: Pt, source: u8) {
    if out.iter().any(|q| q.kind == BOLLARD && dist(q.at, at) < 0.5) {
        return;
    }
    let away = g.walk.nearest_point(at, BENCH_MOVE_M).filter(|q| dist(*q, at) > 0.01).map(|q| yaw_to(q, at)).unwrap_or(0.0);
    let tries = (0..=(BENCH_MOVE_M / 0.25) as usize).map(|i| offset(at, away, 0.0, -0.25 * i as f64));
    match tries.enumerate().find(|(_, p)| g.refuse(*p, 0.0, BOLLARD_SIZE, THIN_WALK_CLEAR_M).is_none()) {
        Some((i, p)) => {
            n.bollards_moved += usize::from(i > 0);
            out.push(Piece { at: p, kind: BOLLARD, yaw: hash01(at) * 360.0, source });
        }
        None => n.refuse("bollard", g.refuse(at, 0.0, BOLLARD_SIZE, THIN_WALK_CLEAR_M).unwrap_or("unknown")),
    }
}

fn round(v: f64) -> f64 {
    (v * 100.0).round() / 100.0
}

/// Places the furniture and writes it to `out`; returns the summary.
pub fn build(inp: &Inputs, walk_graph: &Path, plants: &Path, osm_path: &Path, out: &Path) -> Result<Value, String> {
    let (walk, _) = walk_segments(walk_graph)?;
    let mut kerb = Segments::new(ROAD_REACH_M);
    for ring in inp.carriageway.iter().flatten() {
        let mut closed = ring.clone();
        if let Some(&first) = ring.first() {
            closed.push(first);
        }
        kerb.add_line(&closed);
    }
    let mut trunks: HashMap<(i64, i64), Vec<Trunk>> = HashMap::new();
    let all_trunks = read_trunks(plants)?;
    let trunk_count = all_trunks.len();
    for t in all_trunks {
        trunks.entry(((t.at[0] / TRUNK_CELL_M).floor() as i64, (t.at[1] / TRUNK_CELL_M).floor() as i64)).or_default().push(t);
    }
    let g = Ground {
        carriageway: Index::new(inp.carriageway),
        buildings: Index::new(&crate::shapes::offset(inp.footprints, BUILDING_CLEAR_M)),
        water: Index::new(inp.water),
        bridges: Index::new(inp.bridges),
        walk: &walk,
        kerb: &kerb,
        trunks,
        inside: inp.half - EDGE_M,
    };
    let mut n = Counts::default();
    let mut pieces = place(inp, &g, &mut n);
    let mut q = crate::quay::Counts::default();
    let quay_inputs = crate::quay::Inputs { walkable: inp.walkable, water: inp.river, bridges: inp.bridges, heights: inp.heights, top: inp.top, walk: &walk, half: inp.half };
    let mut railing = crate::quay::place(&quay_inputs, QUAY_RAILING, QUAY, &mut q);
    // The same clearances as every other piece: a railing module on a building, a tree, the road or a walk line is left out.
    railing.retain(|p| match g.refuse(p.at, p.yaw, [QUAY_MODULE[0], QUAY_MODULE[1]], WALK_CLEAR_M) {
        None => true,
        Some(why) => {
            n.refuse("quay_railing", why);
            false
        }
    });
    let quay_summary = crate::quay::summary(&q, railing.len());
    pieces.extend(railing);

    let mut by_piece = serde_json::Map::new();
    for (i, name) in PIECES.iter().enumerate() {
        by_piece.insert(name.to_string(), json!(pieces.iter().filter(|p| p.kind as usize == i).count()));
    }
    let mut refused: Vec<_> = n.refused.iter().collect();
    refused.sort();
    let mut runs: Vec<_> = n.barrier_runs.iter().collect();
    runs.sort();
    let summary = json!({
        "pieces": pieces.len(),
        "by_piece": by_piece,
        "stops": { "platforms": n.platforms, "same_stop": n.platforms_same_stop, "furnished": n.stops, "without_a_road": n.stops_without_road,
                   "shelters_asked": n.shelters_asked, "shelters_slid": n.shelters_slid },
        "benches": { "asked": n.benches_asked, "moved_off_the_path": n.benches_moved },
        "bollards_moved_off_the_path": n.bollards_moved,
        "barrier_runs": runs.into_iter().map(|(k, v)| (k.clone(), json!(v))).collect::<serde_json::Map<_, _>>(),
        "barrier_m": n.barrier_m.round(),
        "panels_gapped_for_walkers": n.panels_gapped_for_walkers,
        "refused": refused.into_iter().map(|(k, v)| (k.clone(), json!(v))).collect::<serde_json::Map<_, _>>(),
        "trees_kept_clear_of": trunk_count,
        "quay": quay_summary,
    });
    let doc = json!({
        "format": "marburgumii.street_furniture",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most; z the walkable top above the Drava (top.bin), which the game grounds; yaw degrees in plan, 0 east and 90 south, the way the piece's front faces (a shelter's open side, a bench's sitter, across a panel's run)",
        "pieces": PIECES,
        "sources": SOURCES,
        "rules": {
            "shelter_setback_m": SHELTER_SETBACK_M, "bench_in_shelter_m": BENCH_IN_SHELTER_M, "bench_aside_m": BENCH_ASIDE, "slide_steps_m": SLIDE_STEPS_M,
            "panel_module_m": PANEL_MODULE_M, "bollard_spacing_m": BOLLARD_SPACING_M, "walk_clear_m": WALK_CLEAR_M, "thin_walk_clear_m": THIN_WALK_CLEAR_M,
            "bench_move_m": BENCH_MOVE_M, "building_clear_m": BUILDING_CLEAR_M, "trunk_clear_m": TRUNK_CLEAR_M, "road_reach_m": ROAD_REACH_M,
        },
        "sources_used": [
            { "role": "stops, benches, bollards and barriers", "name": format!("OpenStreetMap via Overpass, {}", inp.osm.timestamp), "licence": "ODbL 1.0, © OpenStreetMap contributors", "sha256": sha256_file(osm_path) },
            { "role": "where people walk", "name": "lent_walk_graph.json (lent-ground, ADR-044)", "sha256": sha256_file(walk_graph) },
            { "role": "the trees' trunks", "name": "lent_plants.json (lent-ground --mode plants, GFX-01)", "sha256": sha256_file(plants) },
        ],
        "summary": summary,
        "fields": ["x", "y", "z", "piece", "yaw", "source"],
        "furniture": pieces.iter().map(|p| json!([round(p.at[0]), round(p.at[1]), round(inp.top.sample(p.at) as f64), p.kind, (p.yaw * 10.0).round() / 10.0, p.source])).collect::<Vec<_>>(),
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
    use crate::shapes;

    #[test]
    fn frames_and_compass() {
        // Facing east, the right is south and behind is west.
        let p = offset([0.0, 0.0], 0.0, 1.0, 2.0);
        assert!((p[0] + 2.0).abs() < 1e-9 && (p[1] - 1.0).abs() < 1e-9);
        assert_eq!(compass("N"), Some(270.0));
        assert_eq!(compass("90"), Some(0.0));
        assert_eq!(compass("S"), Some(90.0));
        assert_eq!(compass("sideways"), None);
        assert!((yaw_to([0.0, 0.0], [0.0, 1.0]) - 90.0).abs() < 1e-9);
    }

    #[test]
    fn footprints_keep_clear() {
        let empty: Shapes = Vec::new();
        let road = shapes::polygon(&[vec![[-20.0, 5.0], [20.0, 5.0], [20.0, 12.0], [-20.0, 12.0]]]);
        let house = shapes::polygon(&[vec![[-20.0, -10.0], [20.0, -10.0], [20.0, -6.0], [-20.0, -6.0]]]);
        let mut walk = Segments::new(4.0);
        walk.add_line(&[[-20.0, 0.0], [20.0, 0.0]]);
        let kerb = Segments::new(ROAD_REACH_M);
        let mut trunks = HashMap::new();
        trunks.insert((1, 0), vec![Trunk { at: [10.0, 3.0], radius: 0.3 }]);
        let g = Ground {
            carriageway: Index::new(&road),
            buildings: Index::new(&shapes::offset(&house, BUILDING_CLEAR_M)),
            water: Index::new(&empty),
            bridges: Index::new(&empty),
            walk: &walk,
            kerb: &kerb,
            trunks,
            inside: 30.0,
        };
        // A shelter between the walk line and the kerb, open to the road (south).
        assert_eq!(g.refuse([0.0, 3.0], 90.0, SHELTER_SIZE, WALK_CLEAR_M), None);
        assert_eq!(g.refuse([0.0, 1.0], 90.0, SHELTER_SIZE, WALK_CLEAR_M), Some("walkway"));
        assert_eq!(g.refuse([0.0, 4.5], 90.0, SHELTER_SIZE, WALK_CLEAR_M), Some("carriageway"));
        assert_eq!(g.refuse([9.0, 3.0], 90.0, SHELTER_SIZE, WALK_CLEAR_M), Some("tree"));
        assert_eq!(g.refuse([0.0, -5.6], 90.0, BENCH_SIZE, WALK_CLEAR_M), Some("building"));
        assert_eq!(g.refuse([0.0, -3.0], 90.0, BENCH_SIZE, WALK_CLEAR_M), None);
        assert_eq!(g.refuse([40.0, 0.0], 90.0, BOLLARD_SIZE, 0.0), Some("outside"));
    }
}
