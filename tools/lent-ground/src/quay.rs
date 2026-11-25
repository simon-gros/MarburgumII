//! The railing along the Drava's quays (GFX-01, VQ-024): where the paved promenade ends in a drop to the river, a 2 m
//! module of painted steel railing every 2 m along its edge, written into the street furniture file as the piece
//! `quay_railing` (furniture.rs, `MarburgumStreetFurniture.h`).
//!
//! ADR-040 left the promenade ending "in a paved edge over the water". Where that edge stands well above the river a
//! railing belongs on it; where the bank slopes into the water, people wade in and anglers stand, and it stays open.
//! The rule, from the ground the paving and the landscape already have:
//!
//! - **A quay edge** is a point of the paved walkable edge (pavement and squares) whose outward side reaches the river
//!   within WATER_REACH_M, standing at least MIN_DROP_M above the water's surface (z 0, the Drava, ADR-040), with the
//!   ground falling at least MIN_FALL_M within the first FALL_REACH_M outward. A gentle bank fails the last test.
//! - **Open where people meet the water:** nothing within OPEN_RADIUS_M of the Lent angler's spot (the fishing spot is
//!   placed relative to him, ADR-048) or of the swimmers' way in (ADR-032's smoke), nothing on or beside a bridge (its
//!   own parapets), and no module within WALK_CLEAR_M of a walk line (ADR-044), which would block people walking along.
//! - **Runs:** quay points closer than JOIN_M join a run; a run shorter than a module is left out; a run is cut into
//!   whole 2 m modules centred on it, each straight along its chord, RAIL_INSET_M inside the paved edge.
//!
//! The quay wall itself (a vertical stone face down to the water) is not built here: the landscape slopes from the edge
//! to the river, and a wall needs that ground reshaped in the ground build. The summary reports how high the drops are.

use crate::furniture::Piece;
use crate::grid::Grid;
use crate::lamps::Segments;
use crate::osm::Pt;
use crate::shapes::{Index, Shapes};
use serde_json::{json, Value};

/// The quay edge stands at least this far above the water.
const MIN_DROP_M: f64 = 1.0;
/// And somewhere between the edge and the river it falls at least this much within FALL_REACH_M: a steep bank or a
/// wall, not a slope people walk down.
const MIN_FALL_M: f64 = 0.8;
const FALL_REACH_M: f64 = 1.0;
/// The river within this distance outward of the edge.
const WATER_REACH_M: f64 = 6.0;
/// Edge points, this far apart.
const STEP_M: f64 = 0.5;
/// Outward from the edge, the ground is read this often.
const PROBE_M: f64 = 0.25;
/// The railing stands this far back from a crest beyond the paving.
const CREST_BACK_M: f64 = 0.3;
/// Points closer than this make one run.
const JOIN_M: f64 = 2.0;
/// The railing stands this far inside the paved edge.
const RAIL_INSET_M: f64 = 0.25;
const MODULE_M: f64 = 2.0;
/// Kept open round the places people meet the water.
const OPEN_RADIUS_M: f64 = 8.0;
/// The Lent angler, where the server moves him to the bank (ADR-081's angler_to_bank, plan metres), and the swim
/// smoke's way into the river (its `in_water` position on the Lent bank).
pub const OPEN_AT: [(&str, Pt); 2] = [("lent_angler_fishing_spot", [18.13, -52.12]), ("swim_way_in", [40.14, -47.58])];
/// No module this close to a bridge.
const BRIDGE_CLEAR_M: f64 = 3.0;
/// A module's line keeps this far from every walk line.
const WALK_CLEAR_M: f64 = 0.45;

pub struct Inputs<'a> {
    pub walkable: &'a Shapes,
    pub water: &'a Shapes,
    pub bridges: &'a Shapes,
    pub heights: &'a Grid,
    pub top: &'a Grid,
    pub walk: &'a Segments,
    pub half: f64,
}

fn dist(a: Pt, b: Pt) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

fn yaw_to(a: Pt, b: Pt) -> f64 {
    let y = (b[1] - a[1]).atan2(b[0] - a[0]).to_degrees();
    if y < 0.0 { y + 360.0 } else { y }
}

#[derive(Default)]
pub struct Counts {
    pub edge_points: usize,
    pub by_water: usize,
    pub quay_points: usize,
    pub open_for_people: usize,
    pub by_bridge: usize,
    pub on_walk_line: usize,
    pub short_runs: usize,
    pub runs: usize,
    pub run_m: f64,
    pub drops: Vec<f64>,
}

/// One railing point: where it stands, the edge's top and the run direction is decided later.
struct Point {
    at: Pt,
}

/// The railing modules, as furniture pieces of `kind`.
pub fn place(inp: &Inputs, kind: u8, source: u8, n: &mut Counts) -> Vec<Piece> {
    let water = Index::new(inp.water);
    let walkable = Index::new(inp.walkable);
    let bridges = Index::new(&crate::shapes::offset(inp.bridges, BRIDGE_CLEAR_M));
    let inside = inp.half - 2.0;
    let mut runs: Vec<Vec<Point>> = Vec::new();
    for contour in inp.walkable.iter().flatten() {
        let m = contour.len();
        let mut current: Vec<Point> = Vec::new();
        let flush = |current: &mut Vec<Point>, runs: &mut Vec<Vec<Point>>| {
            if !current.is_empty() {
                runs.push(std::mem::take(current));
            }
        };
        for i in 0..m {
            let (a, b) = (contour[i], contour[(i + 1) % m]);
            let len = dist(a, b);
            if len < 1e-6 {
                continue;
            }
            let d = [(b[0] - a[0]) / len, (b[1] - a[1]) / len];
            // The outward side: the one that is not paved just off the edge.
            let mut out = [d[1], -d[0]];
            let mid = [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0];
            if walkable.contains([mid[0] + out[0] * 0.2, mid[1] + out[1] * 0.2]) {
                out = [-out[0], -out[1]];
            }
            let steps = (len / STEP_M).ceil().max(1.0) as usize;
            for s in 0..steps {
                let t = (s as f64 + 0.5) * len / steps as f64;
                let p = [a[0] + d[0] * t, a[1] + d[1] * t];
                n.edge_points += 1;
                if p[0].abs() > inside || p[1].abs() > inside {
                    flush(&mut current, &mut runs);
                    continue;
                }
                // Out from the edge in short steps to the river: where the ground falls steepest is the bank's crest.
                let probe = |k: usize| [p[0] + out[0] * PROBE_M * k as f64, p[1] + out[1] * PROBE_M * k as f64];
                let Some(k_water) = (1..=(WATER_REACH_M / PROBE_M) as usize).find(|&k| water.contains(probe(k))) else {
                    flush(&mut current, &mut runs);
                    continue;
                };
                n.by_water += 1;
                let window = (FALL_REACH_M / PROBE_M).round() as usize;
                let height = |k: usize| if k == 0 { inp.top.sample([p[0] - out[0] * 0.05, p[1] - out[1] * 0.05]) as f64 } else { inp.heights.sample(probe(k)) as f64 };
                let (crest_k, fall) = (0..k_water).map(|k| (k, height(k) - height(k + window))).fold((0, f64::MIN), |best, c| if c.1 > best.1 { c } else { best });
                let crest_top = height(crest_k);
                if crest_top < MIN_DROP_M || fall < MIN_FALL_M {
                    flush(&mut current, &mut runs);
                    continue;
                }
                n.quay_points += 1;
                n.drops.push(crest_top);
                // Just back from the crest; on the paving's own edge, just inside it.
                let back = if crest_k == 0 { -RAIL_INSET_M } else { crest_k as f64 * PROBE_M - CREST_BACK_M };
                let inner = [p[0] + out[0] * back, p[1] + out[1] * back];
                if OPEN_AT.iter().any(|(_, o)| dist(*o, inner) < OPEN_RADIUS_M) {
                    n.open_for_people += 1;
                    flush(&mut current, &mut runs);
                    continue;
                }
                if bridges.contains(inner) {
                    n.by_bridge += 1;
                    flush(&mut current, &mut runs);
                    continue;
                }
                if inp.walk.nearest(inner, WALK_CLEAR_M).is_some() {
                    n.on_walk_line += 1;
                    flush(&mut current, &mut runs);
                    continue;
                }
                if let Some(last) = current.last() {
                    if dist(last.at, inner) > JOIN_M {
                        flush(&mut current, &mut runs);
                    }
                }
                current.push(Point { at: inner });
            }
        }
        flush(&mut current, &mut runs);
    }

    let mut out = Vec::new();
    for run in runs {
        let length: f64 = run.windows(2).map(|w| dist(w[0].at, w[1].at)).sum();
        if length < MODULE_M {
            n.short_runs += 1;
            continue;
        }
        n.runs += 1;
        n.run_m += length;
        // Arc-length positions along the run, then whole modules centred on it.
        let mut cum = vec![0.0];
        for w in run.windows(2) {
            cum.push(cum.last().unwrap() + dist(w[0].at, w[1].at));
        }
        let at = |s: f64| -> Pt {
            let i = cum.partition_point(|&c| c < s).clamp(1, run.len() - 1);
            let (a, b) = (run[i - 1].at, run[i].at);
            let seg = (cum[i] - cum[i - 1]).max(1e-9);
            let t = ((s - cum[i - 1]) / seg).clamp(0.0, 1.0);
            [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
        };
        let count = (length / MODULE_M).floor() as usize;
        let start = (length - count as f64 * MODULE_M) / 2.0;
        for k in 0..count {
            let (s0, s1) = (start + k as f64 * MODULE_M, start + (k + 1) as f64 * MODULE_M);
            let (a, b) = (at(s0), at(s1));
            let centre = [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0];
            // As a fence panel: the front faces across the run, so the module's length lies along it.
            out.push(Piece { at: centre, kind, yaw: (yaw_to(a, b) + 90.0).rem_euclid(360.0), source });
        }
    }
    out
}

pub fn summary(n: &Counts, pieces: usize) -> Value {
    let mut drops = n.drops.clone();
    drops.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let q = |f: f64| drops.get(((drops.len() as f64 - 1.0) * f).round() as usize).map(|v| (v * 100.0).round() / 100.0);
    json!({
        "modules": pieces,
        "runs": n.runs,
        "run_m": n.run_m.round(),
        "short_runs_left_out": n.short_runs,
        "edge_points": n.edge_points,
        "edge_points_by_the_river": n.by_water,
        "quay_points": n.quay_points,
        "left_open": { "where_people_meet_the_water": n.open_for_people, "by_a_bridge": n.by_bridge, "on_a_walk_line": n.on_walk_line },
        "quay_height_above_water_m": { "min": q(0.0), "median": q(0.5), "p90": q(0.9), "max": q(1.0) },
        "open_at": OPEN_AT.iter().map(|(name, p)| json!({ "name": name, "x": p[0], "y": p[1], "radius_m": OPEN_RADIUS_M })).collect::<Vec<_>>(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::shapes;

    /// A 40 m promenade along a river to its south; the ground drops 2 m at its edge in the west half and slopes
    /// gently in the east half.
    #[test]
    fn railing_only_on_the_quay() {
        let promenade = shapes::polygon(&[vec![[-20.0, -6.0], [20.0, -6.0], [20.0, 0.0], [-20.0, 0.0]]]);
        let river = shapes::polygon(&[vec![[-30.0, 1.0], [30.0, 1.0], [30.0, 20.0], [-30.0, 20.0]]]);
        let empty: Shapes = Vec::new();
        let mut heights = Grid::new(161, 40.0, 0.0);
        for r in 0..161 {
            for c in 0..161 {
                let p = heights.pos(r, c);
                let z = if p[1] <= 0.0 { 2.0 } else if p[0] < 0.0 { -0.5 } else { (2.0 - 0.5 * p[1]).max(-0.5) };
                heights.set(r, c, z as f32);
            }
        }
        let top = heights.clone();
        let walk = Segments::new(4.0);
        let inp = Inputs { walkable: &promenade, water: &river, bridges: &empty, heights: &heights, top: &top, walk: &walk, half: 40.0 };
        let mut n = Counts::default();
        let pieces = place(&inp, 6, 4, &mut n);
        assert!(!pieces.is_empty());
        assert!(pieces.iter().all(|p| p.at[0] < 0.5), "only the west half is a quay: {:?}", pieces.iter().map(|p| p.at).collect::<Vec<_>>());
        assert!(pieces.iter().all(|p| (p.at[1] + RAIL_INSET_M).abs() < 0.05), "on the river edge, inset");
        assert!(pieces.iter().all(|p| (p.yaw - 90.0).abs() < 1.0 || (p.yaw - 270.0).abs() < 1.0), "facing across the run");
        assert!(pieces.len() >= 8 && pieces.len() <= 10, "{} modules on 20 m", pieces.len());
    }
}
