//! The walk graph street people follow (ADR-044): undirected edges along the pavements of the
//! paved meshes this tool builds, so a walker never leaves what the level has paved for them.
//!
//! Carried forward from the original game's pedestrian graph (`tools/build_road_graph.py`,
//! `walk`): mapped footways, paths and pedestrian streets; sidewalks beside every road; mapped
//! crossings; loose ends joined; corners crossed where two pavements face each other over a road.
//! The original offset a road's sidewalk a fixed 1.6 m beyond the carriageway and trusted the map;
//! here every line is fitted to the pavement this tool has just built, and kept where a person
//! fits:
//!
//! - **Clearance.** Paved and unpaved walking surfaces are rasterised at 25 cm, and every point
//!   gets its distance to the nearest point that is not walkable (a wall, the carriageway, the
//!   river, grass). A walker's line keeps at least `CLEAR_M` from all of them.
//! - **Sidewalks.** Along each side of a road, at every `STEP_M`, the line sits `KERB_IN_M` in from
//!   where the pavement starts (or in its middle when it is narrower than that), unless a mapped
//!   footway already runs within `MAPPED_NEAR_M`.
//! - **Footways** keep their mapped line where it is clear, and are nudged sideways up to
//!   `NUDGE_M` where it is not (a mapped line along a house front).
//! - **Crossings.** Mapped crossings join the nearest walk nodes on either side of the road.
//!   Corners: a dead end within `CORNER_M` of a node of another network joins it across the road.
//! - **Bridges.** Footways on a bridge are kept as mapped and flagged: the deck is a building of
//!   the level, not paving, so the game finds its height there.
//!
//! The output is `walk_graph.json` in plan metres, with the walkable top height (`top.bin`) at
//! every node.

use crate::grid::Grid;
use crate::osm::{Osm, Pt, Tags};
use crate::shapes::{self, Shapes};
use std::collections::{HashMap, HashSet};

/// Raster resolution for the clearance field: at least 2049 points, and no coarser than 0.25 m per
/// cell however wide the extent (ADR-045).
const RASTER_POINTS: usize = 2049;
const RASTER_M: f64 = 0.25;
/// Distance a walker's line keeps from anything not walkable (body radius 0.34 m and a margin).
pub const CLEAR_M: f64 = 0.5;
/// Spacing of samples along a line.
const STEP_M: f64 = 2.0;
/// A sidewalk line sits this far in from where the pavement starts beside the road.
const KERB_IN_M: f64 = 1.0;
/// How far beyond the carriageway edge to look for a road's pavement.
const SIDEWALK_SEARCH_M: f64 = 7.0;
/// A mapped footway this close means the road side needs no inferred sidewalk.
const MAPPED_NEAR_M: f64 = 3.0;
/// A footway point that is not clear may move this far sideways to be.
const NUDGE_M: f64 = 1.5;
/// Consecutive samples further apart than this are not joined.
const LINK_MAX_M: f64 = 4.5;
/// A loose end joins the nearest other node this close along clear ground.
const JOIN_M: f64 = 4.0;
/// A crossing reaches this far to each side of the road.
const CROSSING_REACH_M: f64 = 16.0;
/// A dead end reaches over a road to another network this close.
const CORNER_M: f64 = 14.0;
/// Components smaller than this are dropped (a scrap of pavement nobody can reach).
const MIN_COMPONENT: usize = 8;
/// Simplification: a node in a straight run is dropped when it is this close to the line.
const SIMPLIFY_M: f64 = 0.2;
/// ...and the merged edge would be no longer than this.
const SIMPLIFY_MAX_EDGE_M: f64 = 12.0;

pub const KINDS: [&str; 5] = ["path", "sidewalk", "crossing", "corner", "bridge"];
const PATH: u8 = 0;
const SIDEWALK: u8 = 1;
const CROSSING: u8 = 2;
const CORNER: u8 = 3;
const BRIDGE: u8 = 4;

pub mod flags {
    /// On a bridge deck: the height is the deck's, which the game finds there.
    pub const BRIDGE: u8 = 1;
    /// In the carriageway (a crossing).
    pub const ROAD: u8 = 2;
}

fn tag<'a>(t: &'a Tags, k: &str) -> Option<&'a str> {
    t.get(k).map(String::as_str)
}

fn dist(a: Pt, b: Pt) -> f64 {
    ((a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2)).sqrt()
}

fn lerp(a: Pt, b: Pt, t: f64) -> Pt {
    [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t]
}

/// Points every `step` along a polyline, with the unit direction there.
fn resample(line: &[Pt], step: f64) -> Vec<(Pt, [f64; 2])> {
    let mut out = Vec::new();
    let mut carry = 0.0;
    for w in line.windows(2) {
        let len = dist(w[0], w[1]);
        if len < 1e-6 {
            continue;
        }
        let dir = [(w[1][0] - w[0][0]) / len, (w[1][1] - w[0][1]) / len];
        let mut t = carry;
        while t <= len {
            out.push((lerp(w[0], w[1], t / len), dir));
            t += step;
        }
        carry = t - len;
    }
    if let (Some(last), Some(w)) = (line.last(), line.windows(2).last()) {
        if out.last().map_or(true, |(p, _)| dist(*p, *last) > step * 0.3) {
            let len = dist(w[0], w[1]).max(1e-6);
            out.push((*last, [(w[1][0] - w[0][0]) / len, (w[1][1] - w[0][1]) / len]));
        }
    }
    out
}

/// What a walker may stand on, and how far each point is from the edge of it.
pub struct Field {
    walk: Grid,
    clearance: Grid,
    road: Grid,
    blocked: Grid,
}

impl Field {
    pub fn new(walkable: &[&Shapes], road: &Shapes, blocked: &Shapes, half: f64) -> Field {
        let mut walk = Grid::new(RASTER_POINTS.max((2.0 * half / RASTER_M).round() as usize + 1), half, 0.0);
        for s in walkable.iter().flat_map(|s| s.iter()) {
            walk.fill_rings(s, 1.0);
        }
        let mut road_mask = walk.like(0.0);
        for s in road {
            road_mask.fill_rings(s, 1.0);
        }
        let mut blocked_mask = walk.like(0.0);
        for s in blocked {
            blocked_mask.fill_rings(s, 1.0);
        }
        // Walkable only where it is not road and not blocked, whatever overlaps.
        for i in 0..walk.v.len() {
            if road_mask.v[i] > 0.5 || blocked_mask.v[i] > 0.5 {
                walk.v[i] = 0.0;
            }
        }
        let not_walk = {
            let mut m = walk.clone();
            for v in m.v.iter_mut() {
                *v = 1.0 - *v;
            }
            m
        };
        let clearance = Grid::distance_to(&not_walk);
        Field { walk, clearance, road: road_mask, blocked: blocked_mask }
    }

    fn inside(&self, p: Pt) -> bool {
        p[0].abs() < self.walk.half - 0.5 && p[1].abs() < self.walk.half - 0.5
    }

    /// Metres to the nearest point a walker may not stand on (0 outside the walkable area).
    pub fn clear(&self, p: Pt) -> f64 {
        if !self.inside(p) {
            return 0.0;
        }
        self.clearance.sample(p) as f64
    }

    fn is_road(&self, p: Pt) -> bool {
        self.inside(p) && self.road.sample(p) > 0.5
    }

    fn is_blocked(&self, p: Pt) -> bool {
        !self.inside(p) || self.blocked.sample(p) > 0.5
    }

    /// The straight walk from a to b keeps its clearance all the way.
    pub fn clear_between(&self, a: Pt, b: Pt, min: f64) -> bool {
        let n = (dist(a, b) / 0.2).ceil().max(1.0) as usize;
        (0..=n).all(|i| self.clear(lerp(a, b, i as f64 / n as f64)) >= min)
    }

    /// The straight line from a to b crosses only clear ground and the carriageway, never a
    /// building or the river: a way over a road.
    fn crossable(&self, a: Pt, b: Pt) -> bool {
        let n = (dist(a, b) / 0.2).ceil().max(1.0) as usize;
        (0..=n).all(|i| {
            let p = lerp(a, b, i as f64 / n as f64);
            // Raster edges blend road into pavement, so either half counts as solid footing.
            !self.is_blocked(p) && self.inside(p) && (self.road.sample(p) > 0.25 || self.walk.sample(p) > 0.25)
        })
    }

    /// Whether the line from a to b runs over the carriageway for at least `min` metres.
    fn crosses_road(&self, a: Pt, b: Pt, min: f64) -> bool {
        let n = (dist(a, b) / 0.2).ceil().max(1.0) as usize;
        let on = (0..=n).filter(|&i| self.is_road(lerp(a, b, i as f64 / n as f64))).count();
        on as f64 * dist(a, b) / n as f64 >= min
    }
}

#[derive(Default)]
pub struct Graph {
    pub nodes: Vec<Pt>,
    pub flags: Vec<u8>,
    pub edges: HashMap<(usize, usize), u8>,
}

impl Graph {
    fn add_node(&mut self, p: Pt, flags: u8) -> usize {
        self.nodes.push(p);
        self.flags.push(flags);
        self.nodes.len() - 1
    }

    fn add_edge(&mut self, a: usize, b: usize, kind: u8) {
        if a == b {
            return;
        }
        let key = (a.min(b), a.max(b));
        self.edges.entry(key).or_insert(kind);
    }

    fn adjacency(&self) -> Vec<Vec<usize>> {
        let mut adj = vec![Vec::new(); self.nodes.len()];
        for &(a, b) in self.edges.keys() {
            adj[a].push(b);
            adj[b].push(a);
        }
        for list in adj.iter_mut() {
            list.sort_unstable();
        }
        adj
    }

    fn components(&self) -> Vec<usize> {
        let adj = self.adjacency();
        let mut label = vec![usize::MAX; self.nodes.len()];
        let mut next = 0;
        for start in 0..self.nodes.len() {
            if label[start] != usize::MAX {
                continue;
            }
            let mut stack = vec![start];
            label[start] = next;
            while let Some(n) = stack.pop() {
                for &m in &adj[n] {
                    if label[m] == usize::MAX {
                        label[m] = next;
                        stack.push(m);
                    }
                }
            }
            next += 1;
        }
        label
    }

    /// Adds a chain of points as consecutive nodes, joining neighbours that are close and clear.
    fn add_chain(&mut self, field: &Field, pts: &[Option<Pt>], kind: u8, flags: u8, check: bool) {
        let mut prev: Option<(usize, Pt)> = None;
        for p in pts {
            let Some(p) = *p else {
                prev = None;
                continue;
            };
            let n = self.add_node(p, flags);
            if let Some((m, q)) = prev {
                if dist(p, q) <= LINK_MAX_M && (!check || field.clear_between(p, q, CLEAR_M * 0.8)) {
                    self.add_edge(m, n, kind);
                }
            }
            prev = Some((n, p));
        }
    }
}

/// A spatial hash over node positions.
struct Cells {
    cell: f64,
    map: HashMap<(i64, i64), Vec<usize>>,
}

impl Cells {
    fn new(nodes: &[Pt], cell: f64) -> Cells {
        let mut map: HashMap<(i64, i64), Vec<usize>> = HashMap::new();
        for (i, p) in nodes.iter().enumerate() {
            map.entry(((p[0] / cell).floor() as i64, (p[1] / cell).floor() as i64)).or_default().push(i);
        }
        Cells { cell, map }
    }

    fn near(&self, p: Pt, within: f64) -> Vec<usize> {
        let r = (within / self.cell).ceil() as i64;
        let (cx, cy) = ((p[0] / self.cell).floor() as i64, (p[1] / self.cell).floor() as i64);
        let mut out = Vec::new();
        for dx in -r..=r {
            for dy in -r..=r {
                if let Some(v) = self.map.get(&(cx + dx, cy + dy)) {
                    out.extend_from_slice(v);
                }
            }
        }
        out.sort_unstable();
        out
    }
}

/// The ways people walk along: footways, paths, pedestrian and living streets, steps.
fn is_walk_way(t: &Tags) -> bool {
    if tag(t, "area") == Some("yes") || tag(t, "indoor") == Some("yes") || tag(t, "tunnel").is_some_and(|v| v != "no") {
        return false;
    }
    if tag(t, "access").is_some_and(|a| a == "private" || a == "no") || tag(t, "foot") == Some("no") {
        return false;
    }
    matches!(tag(t, "highway"), Some("footway" | "path" | "pedestrian" | "living_street" | "steps" | "cycleway" | "bridleway"))
}

fn is_crossing_way(t: &Tags) -> bool {
    tag(t, "footway") == Some("crossing") || tag(t, "cycleway") == Some("crossing") || tag(t, "path") == Some("crossing")
}

fn is_bridge(t: &Tags) -> bool {
    tag(t, "bridge").is_some_and(|b| b != "no")
}

/// Roads with a pavement beside them (not motorways, not tracks).
fn road_half_width(t: &Tags) -> Option<f64> {
    if is_bridge(t) || tag(t, "tunnel").is_some_and(|v| v != "no") || tag(t, "area") == Some("yes") {
        return None;
    }
    let hw = tag(t, "highway")?;
    let oneway = matches!(tag(t, "oneway"), Some("yes" | "1" | "-1")) || tag(t, "junction") == Some("roundabout");
    let lanes = tag(t, "lanes").and_then(|l| l.parse::<f64>().ok());
    let width = |two: f64, one: f64| {
        tag(t, "width")
            .and_then(|w| w.trim_end_matches('m').trim().replace(',', ".").parse::<f64>().ok())
            .filter(|w| *w > 0.5 && *w < 40.0)
            .or(lanes.map(|l| l * 3.1 + 0.6))
            .unwrap_or(if oneway { one } else { two })
    };
    let w = match hw {
        "trunk" | "primary" | "secondary" => width(9.0, 6.5),
        "tertiary" => width(7.5, 5.5),
        "unclassified" => width(6.5, 4.5),
        "residential" => width(6.0, 4.0),
        "busway" => width(6.5, 3.6),
        h if h.ends_with("_link") => width(6.0, 4.5),
        "service" if !matches!(tag(t, "service"), Some("driveway" | "parking_aisle")) => width(4.0, 4.0),
        _ => return None,
    };
    Some(w / 2.0)
}

/// The sidewalk point beside a road at `p`, looking along `normal` from the road's edge: in from
/// where the pavement starts, or in the middle of it when it is narrow.
fn sidewalk_point(field: &Field, p: Pt, normal: [f64; 2], half_width: f64) -> Option<Pt> {
    let at = |d: f64| [p[0] + normal[0] * d, p[1] + normal[1] * d];
    let step = 0.2;
    let mut d = (half_width - 1.0).max(0.5);
    let end = half_width + SIDEWALK_SEARCH_M;
    // Find where the walkable pavement starts.
    while d < end && field.clear(at(d)) < CLEAR_M {
        // A wall or the river before any pavement: there is none on this side here.
        if field.is_blocked(at(d)) {
            return None;
        }
        d += step;
    }
    if d >= end {
        return None;
    }
    let start = d;
    while d < start + 2.0 * KERB_IN_M && field.clear(at(d + step)) >= CLEAR_M {
        d += step;
    }
    let width = d - start;
    let pick = if width >= 2.0 * KERB_IN_M { start + KERB_IN_M } else { start + width / 2.0 };
    Some(at(pick))
}

/// Moves a footway point sideways onto clear ground, nearest first; None when there is none.
fn nudge(field: &Field, p: Pt, dir: [f64; 2]) -> Option<Pt> {
    if field.clear(p) >= CLEAR_M {
        return Some(p);
    }
    let normal = [-dir[1], dir[0]];
    let mut d = 0.2;
    while d <= NUDGE_M {
        for s in [1.0, -1.0] {
            let q = [p[0] + normal[0] * d * s, p[1] + normal[1] * d * s];
            if field.clear(q) >= CLEAR_M {
                return Some(q);
            }
        }
        d += 0.2;
    }
    None
}

fn segments_cross(p1: Pt, p2: Pt, q1: Pt, q2: Pt) -> Option<f64> {
    let r = [p2[0] - p1[0], p2[1] - p1[1]];
    let s = [q2[0] - q1[0], q2[1] - q1[1]];
    let den = r[0] * s[1] - r[1] * s[0];
    if den.abs() < 1e-9 {
        return None;
    }
    let qp = [q1[0] - p1[0], q1[1] - p1[1]];
    let t = (qp[0] * s[1] - qp[1] * s[0]) / den;
    let u = (qp[0] * r[1] - qp[1] * r[0]) / den;
    if t > 0.02 && t < 0.98 && u > 0.02 && u < 0.98 {
        Some(t)
    } else {
        None
    }
}

pub struct Summary {
    pub mapped_points: usize,
    pub sidewalk_points: usize,
    pub crossings: usize,
    pub joins: usize,
    pub splits: usize,
    pub corners: usize,
    pub dropped_components: usize,
}

/// Builds the graph. `crossings` are mapped crossing points in the carriageway with the road's
/// direction there.
pub fn build(osm: &Osm, field: &Field, crossings: &[(Pt, [f64; 2])], half: f64) -> (Graph, Summary) {
    let mut g = Graph::default();
    let near_window = |pts: &[Pt]| pts.iter().any(|p| p[0].abs() < half + 20.0 && p[1].abs() < half + 20.0);
    let mut summary = Summary { mapped_points: 0, sidewalk_points: 0, crossings: 0, joins: 0, splits: 0, corners: 0, dropped_components: 0 };

    // ---- mapped ways --------------------------------------------------------------------------
    let mut mapped: Vec<Pt> = Vec::new();
    for w in osm.ways.iter().filter(|w| near_window(&w.pts)) {
        let t = &w.tags;
        if !is_walk_way(t) && !is_crossing_way(t) {
            continue;
        }
        let samples = resample(&w.pts, STEP_M);
        if is_crossing_way(t) {
            // Over the carriageway: kept as mapped; their ends are joined below.
            let pts: Vec<Option<Pt>> = samples.iter().map(|(p, _)| if field.inside(*p) { Some(*p) } else { None }).collect();
            g.add_chain(field, &pts, CROSSING, flags::ROAD, false);
            summary.crossings += 1;
            continue;
        }
        if is_bridge(t) {
            let pts: Vec<Option<Pt>> = samples.iter().map(|(p, _)| if field.inside(*p) { Some(*p) } else { None }).collect();
            g.add_chain(field, &pts, BRIDGE, flags::BRIDGE, false);
            continue;
        }
        let pts: Vec<Option<Pt>> = samples.iter().map(|(p, d)| nudge(field, *p, *d)).collect();
        summary.mapped_points += pts.iter().flatten().count();
        mapped.extend(pts.iter().flatten().copied());
        g.add_chain(field, &pts, PATH, 0, true);
    }
    let mapped_cells = Cells::new(&mapped, 8.0);

    // ---- sidewalks beside every road ------------------------------------------------------------
    for w in osm.ways.iter().filter(|w| near_window(&w.pts)) {
        let Some(half_width) = road_half_width(&w.tags) else { continue };
        let samples = resample(&w.pts, STEP_M);
        for side in [1.0, -1.0] {
            let pts: Vec<Option<Pt>> = samples
                .iter()
                .map(|(p, d)| {
                    let normal = [-d[1] * side, d[0] * side];
                    let q = sidewalk_point(field, *p, normal, half_width)?;
                    let taken = mapped_cells.near(q, MAPPED_NEAR_M).iter().any(|&i| dist(mapped[i], q) < MAPPED_NEAR_M);
                    if taken {
                        None
                    } else {
                        Some(q)
                    }
                })
                .collect();
            summary.sidewalk_points += pts.iter().flatten().count();
            g.add_chain(field, &pts, SIDEWALK, 0, true);
        }
    }

    // ---- lines that cross without a shared node meet there ----------------------------------------
    {
        let mut edges: Vec<((usize, usize), u8)> = g.edges.iter().map(|(k, v)| (*k, *v)).collect();
        // HashMap order is random; the output must not be.
        edges.sort_unstable();
        let mids: Vec<Pt> = edges.iter().map(|((a, b), _)| lerp(g.nodes[*a], g.nodes[*b], 0.5)).collect();
        let cells = Cells::new(&mids, 6.0);
        let mut cuts: HashMap<usize, Vec<(f64, Pt)>> = HashMap::new();
        for (i, ((a, b), _)) in edges.iter().enumerate() {
            for j in cells.near(mids[i], LINK_MAX_M + 1.0) {
                if j <= i {
                    continue;
                }
                let ((c, d), _) = edges[j];
                if [*a, *b].contains(&c) || [*a, *b].contains(&d) {
                    continue;
                }
                if let Some(t) = segments_cross(g.nodes[*a], g.nodes[*b], g.nodes[c], g.nodes[d]) {
                    let p = lerp(g.nodes[*a], g.nodes[*b], t);
                    let u = {
                        let (q1, q2) = (g.nodes[c], g.nodes[d]);
                        let len2 = (q2[0] - q1[0]).powi(2) + (q2[1] - q1[1]).powi(2);
                        ((p[0] - q1[0]) * (q2[0] - q1[0]) + (p[1] - q1[1]) * (q2[1] - q1[1])) / len2.max(1e-9)
                    };
                    cuts.entry(i).or_default().push((t, p));
                    cuts.entry(j).or_default().push((u, p));
                }
            }
        }
        // One node per crossing point, shared by both edges.
        let mut at_point: HashMap<(i64, i64), usize> = HashMap::new();
        let mut keys: Vec<usize> = cuts.keys().copied().collect();
        keys.sort_unstable();
        for i in keys {
            let ((a, b), kind) = edges[i];
            let mut list = cuts.remove(&i).unwrap();
            list.sort_by(|x, y| x.0.partial_cmp(&y.0).unwrap());
            g.edges.remove(&(a, b));
            let mut prev = a;
            for (_, p) in list {
                let key = ((p[0] * 100.0).round() as i64, (p[1] * 100.0).round() as i64);
                let flag = g.flags[a] & g.flags[b];
                let n = *at_point.entry(key).or_insert_with(|| {
                    summary.splits += 1;
                    g.add_node(p, flag)
                });
                g.add_edge(prev, n, kind);
                prev = n;
            }
            g.add_edge(prev, b, kind);
        }
    }

    // ---- loose ends join the nearest node along clear ground ---------------------------------------
    for _ in 0..2 {
        let adj = g.adjacency();
        let cells = Cells::new(&g.nodes, 6.0);
        let label = g.components();
        for n in 0..g.nodes.len() {
            if adj[n].len() > 1 {
                continue;
            }
            let p = g.nodes[n];
            let mut best: Option<(f64, usize)> = None;
            for m in cells.near(p, JOIN_M) {
                if m == n || adj[n].contains(&m) {
                    continue;
                }
                // Not back along its own line: only another network, or its own beyond a bend.
                let d = dist(p, g.nodes[m]);
                if d > JOIN_M || (label[m] == label[n] && d < 1.0) {
                    continue;
                }
                let road = g.flags[n] & flags::ROAD != 0 || g.flags[m] & flags::ROAD != 0;
                let bridge = g.flags[n] & flags::BRIDGE != 0 || g.flags[m] & flags::BRIDGE != 0;
                let ok = if road || bridge { field.crossable(p, g.nodes[m]) || bridge } else { field.clear_between(p, g.nodes[m], CLEAR_M * 0.8) };
                if ok && best.map_or(true, |(bd, _)| d < bd) {
                    best = Some((d, m));
                }
            }
            if let Some((_, m)) = best {
                let kind = if g.flags[n] & flags::ROAD != 0 { CROSSING } else { PATH };
                g.add_edge(n, m, kind);
                summary.joins += 1;
            }
        }
    }

    // ---- mapped crossing points join the pavements either side ---------------------------------------
    {
        let cells = Cells::new(&g.nodes, 8.0);
        for &(at, road_dir) in crossings {
            let normal = [-road_dir[1], road_dir[0]];
            let covered = cells.near(at, 3.0).iter().any(|&i| g.flags[i] & flags::ROAD != 0 && dist(g.nodes[i], at) < 3.0);
            if covered {
                continue;
            }
            let mut ends = Vec::new();
            for side in [1.0, -1.0] {
                let mut best: Option<(f64, usize)> = None;
                for m in cells.near(at, CROSSING_REACH_M) {
                    if g.flags[m] & flags::ROAD != 0 {
                        continue;
                    }
                    let v = [g.nodes[m][0] - at[0], g.nodes[m][1] - at[1]];
                    let across = (v[0] * normal[0] + v[1] * normal[1]) * side;
                    let along = (v[0] * road_dir[0] + v[1] * road_dir[1]).abs();
                    if across < 1.0 || across > CROSSING_REACH_M || along > 3.0 {
                        continue;
                    }
                    let score = across + along * 2.0;
                    if best.map_or(true, |(s, _)| score < s) && field.crossable(at, g.nodes[m]) {
                        best = Some((score, m));
                    }
                }
                ends.extend(best.map(|(_, m)| m));
            }
            if ends.len() == 2 {
                let c = g.add_node(at, flags::ROAD);
                g.add_edge(ends[0], c, CROSSING);
                g.add_edge(c, ends[1], CROSSING);
                summary.crossings += 1;
            }
        }
    }

    // ---- corners: a dead end reaches over the road to another network -------------------------------
    {
        let adj = g.adjacency();
        let cells = Cells::new(&g.nodes, 8.0);
        let label = g.components();
        let mut added: HashSet<(usize, usize)> = HashSet::new();
        for n in 0..g.nodes.len() {
            if adj[n].len() != 1 || g.flags[n] & (flags::ROAD | flags::BRIDGE) != 0 {
                continue;
            }
            let p = g.nodes[n];
            let mut best: Option<(f64, usize)> = None;
            for m in cells.near(p, CORNER_M) {
                if label[m] == label[n] || g.flags[m] & flags::ROAD != 0 {
                    continue;
                }
                let d = dist(p, g.nodes[m]);
                if d <= CORNER_M && best.map_or(true, |(bd, _)| d < bd) && field.crossable(p, g.nodes[m]) && field.crosses_road(p, g.nodes[m], 1.5) {
                    best = Some((d, m));
                }
            }
            if let Some((_, m)) = best {
                if added.insert((n.min(m), n.max(m))) {
                    g.add_edge(n, m, CORNER);
                    summary.corners += 1;
                }
            }
        }
    }

    // ---- networks still apart: the nearest pair of their nodes over a road joins them -------------------
    loop {
        let label = g.components();
        let cells = Cells::new(&g.nodes, 8.0);
        let mut best: HashMap<(usize, usize), (f64, usize, usize)> = HashMap::new();
        for n in 0..g.nodes.len() {
            if g.flags[n] & flags::ROAD != 0 {
                continue;
            }
            for m in cells.near(g.nodes[n], CORNER_M) {
                if m <= n || label[m] == label[n] || g.flags[m] & flags::ROAD != 0 {
                    continue;
                }
                let d = dist(g.nodes[n], g.nodes[m]);
                let key = (label[n].min(label[m]), label[n].max(label[m]));
                if d <= CORNER_M && best.get(&key).map_or(true, |b| d < b.0) && field.crossable(g.nodes[n], g.nodes[m]) {
                    best.insert(key, (d, n, m));
                }
            }
        }
        if best.is_empty() {
            break;
        }
        // One join per pair of networks per round, shortest first, so two networks meet once.
        let mut joins: Vec<_> = best.into_values().collect();
        joins.sort_by(|a, b| a.0.partial_cmp(&b.0).unwrap().then(a.1.cmp(&b.1)).then(a.2.cmp(&b.2)));
        let mut merged: Vec<usize> = (0..label.iter().max().map_or(0, |m| m + 1)).collect();
        fn root(m: &mut [usize], x: usize) -> usize {
            let mut r = x;
            while m[r] != r {
                r = m[r];
            }
            m[x] = r;
            r
        }
        for (_, n, m) in joins {
            let (a, b) = (root(&mut merged, label[n]), root(&mut merged, label[m]));
            if a != b {
                merged[a] = b;
                g.add_edge(n, m, CORNER);
                summary.corners += 1;
            }
        }
    }

    // ---- small scraps go, straight runs merge --------------------------------------------------------
    let label = g.components();
    let mut size: HashMap<usize, usize> = HashMap::new();
    for &l in &label {
        *size.entry(l).or_default() += 1;
    }
    summary.dropped_components = size.values().filter(|&&s| s < MIN_COMPONENT).count();
    let keep: Vec<bool> = label.iter().map(|l| size[l] >= MIN_COMPONENT).collect();
    let g = simplify(compact(g, &keep));
    (g, summary)
}

/// Only the kept nodes, renumbered, with their edges.
fn compact(g: Graph, keep: &[bool]) -> Graph {
    let mut index = vec![usize::MAX; g.nodes.len()];
    let mut out = Graph::default();
    for (i, k) in keep.iter().enumerate() {
        if *k {
            index[i] = out.add_node(g.nodes[i], g.flags[i]);
        }
    }
    let mut edges: Vec<_> = g.edges.into_iter().collect();
    edges.sort_unstable();
    for ((a, b), kind) in edges {
        if index[a] != usize::MAX && index[b] != usize::MAX {
            out.add_edge(index[a], index[b], kind);
        }
    }
    out
}

/// Drops the middle node of straight runs: same kind both sides, close to the line, not too long.
fn simplify(mut g: Graph) -> Graph {
    loop {
        let adj = g.adjacency();
        let mut removed = vec![false; g.nodes.len()];
        let mut changed = false;
        for n in 0..g.nodes.len() {
            if adj[n].len() != 2 || removed[n] {
                continue;
            }
            let (a, b) = (adj[n][0], adj[n][1]);
            if removed[a] || removed[b] || g.edges.contains_key(&(a.min(b), a.max(b))) {
                continue;
            }
            let ka = g.edges[&(a.min(n), a.max(n))];
            let kb = g.edges[&(b.min(n), b.max(n))];
            if ka != kb || g.flags[n] != g.flags[a] || g.flags[n] != g.flags[b] {
                continue;
            }
            let (pa, pb, p) = (g.nodes[a], g.nodes[b], g.nodes[n]);
            let len = dist(pa, pb);
            if len > SIMPLIFY_MAX_EDGE_M || len < 1e-6 {
                continue;
            }
            let off = ((pb[0] - pa[0]) * (pa[1] - p[1]) - (pa[0] - p[0]) * (pb[1] - pa[1])).abs() / len;
            if off > SIMPLIFY_M {
                continue;
            }
            g.edges.remove(&(a.min(n), a.max(n)));
            g.edges.remove(&(b.min(n), b.max(n)));
            g.add_edge(a, b, ka);
            removed[n] = true;
            // Neighbours keep their degree, but do not chain removals in one pass.
            removed[a] = true;
            removed[b] = true;
            changed = true;
        }
        if !changed {
            break;
        }
        // Un-mark the neighbours: only nodes without edges are gone.
        let adj = g.adjacency();
        let keep: Vec<bool> = adj.iter().map(|l| !l.is_empty()).collect();
        g = compact(g, &keep);
    }
    g
}

/// The graph as `walk_graph.json`: nodes `[x, y, z, flags]` (plan metres; z the walkable top),
/// edges `[a, b, kind]`.
pub fn to_json(g: &Graph, top: &Grid, summary: &Summary, sources: serde_json::Value) -> serde_json::Value {
    let label = g.components();
    let mut sizes: HashMap<usize, usize> = HashMap::new();
    for &l in &label {
        *sizes.entry(l).or_default() += 1;
    }
    let mut sizes: Vec<usize> = sizes.into_values().collect();
    sizes.sort_unstable_by(|a, b| b.cmp(a));
    let round = |v: f64| (v * 100.0).round() / 100.0;
    let nodes: Vec<serde_json::Value> = g
        .nodes
        .iter()
        .zip(&g.flags)
        .map(|(p, f)| serde_json::json!([round(p[0]), round(p[1]), round(top.sample(*p) as f64), f]))
        .collect();
    let mut edges: Vec<(&(usize, usize), &u8)> = g.edges.iter().collect();
    edges.sort_unstable();
    let total: f64 = edges.iter().map(|((a, b), _)| dist(g.nodes[*a], g.nodes[*b])).sum();
    let mut by_kind = serde_json::Map::new();
    for (k, name) in KINDS.iter().enumerate() {
        let m: f64 = edges.iter().filter(|(_, kind)| **kind as usize == k).map(|((a, b), _)| dist(g.nodes[*a], g.nodes[*b])).sum();
        by_kind.insert(name.to_string(), serde_json::json!(m.round()));
    }
    serde_json::json!({
        "format": "marburgumii.walk_graph",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most; z the walkable top above the Drava (top.bin), or 0 on a bridge deck (flags & 1), whose height the game finds",
        "clearance_m": CLEAR_M,
        "kinds": KINDS,
        "flags": { "bridge": flags::BRIDGE, "road": flags::ROAD },
        "nodes": nodes,
        "edges": edges.iter().map(|((a, b), k)| serde_json::json!([a, b, k])).collect::<Vec<_>>(),
        "summary": {
            "nodes": g.nodes.len(),
            "edges": edges.len(),
            "length_m": total.round(),
            "length_by_kind_m": by_kind,
            "components": sizes.len(),
            "largest_component_nodes": sizes.first().copied().unwrap_or(0),
            "mapped_points": summary.mapped_points,
            "sidewalk_points": summary.sidewalk_points,
            "crossings": summary.crossings,
            "joins": summary.joins,
            "splits": summary.splits,
            "corners": summary.corners,
            "dropped_components": summary.dropped_components,
        },
        "sources": sources,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn street_field() -> Field {
        // A 7 m road along x, a 4 m pavement each side, a building beyond the north one.
        let road = shapes::stroke(&[[-40.0, 0.0], [40.0, 0.0]], 7.0, false);
        let north = shapes::stroke(&[[-40.0, -5.5], [40.0, -5.5]], 4.0, false);
        let south = shapes::stroke(&[[-40.0, 5.5], [40.0, 5.5]], 4.0, false);
        let building = shapes::polygon(&[vec![[-40.0, -7.5], [40.0, -7.5], [40.0, -20.0], [-40.0, -20.0]]]);
        Field::new(&[&north, &south], &road, &building, 64.0)
    }

    #[test]
    fn clearance_is_largest_in_the_middle_of_the_pavement() {
        let f = street_field();
        assert!(f.clear([0.0, 5.5]) > 1.7);
        assert!(f.clear([0.0, 3.8]) < 0.4);
        assert!(f.clear([0.0, 0.0]) < 0.01);
        assert!(f.clear([0.0, -10.0]) < 0.01);
    }

    #[test]
    fn sidewalk_sits_in_from_the_kerb() {
        let f = street_field();
        let p = sidewalk_point(&f, [0.0, 0.0], [0.0, 1.0], 3.5).unwrap();
        // Pavement starts at 3.5 m; clear from 4.0; a metre in from there.
        assert!((p[1] - 5.0).abs() < 0.3, "{p:?}");
        let n = sidewalk_point(&f, [0.0, 0.0], [0.0, -1.0], 3.5).unwrap();
        assert!(n[1] < -4.0 && n[1] > -6.6, "{n:?}");
    }

    #[test]
    fn no_sidewalk_where_a_wall_comes_first() {
        let road = shapes::stroke(&[[-40.0, 0.0], [40.0, 0.0]], 7.0, false);
        let wall = shapes::polygon(&[vec![[-40.0, 3.6], [40.0, 3.6], [40.0, 20.0], [-40.0, 20.0]]]);
        let f = Field::new(&[], &road, &wall, 64.0);
        assert!(sidewalk_point(&f, [0.0, 0.0], [0.0, 1.0], 3.5).is_none());
    }

    #[test]
    fn crossing_lines_meet_in_a_node() {
        let mut g = Graph::default();
        let a = g.add_node([-2.0, 0.0], 0);
        let b = g.add_node([2.0, 0.0], 0);
        let c = g.add_node([0.0, -2.0], 0);
        let d = g.add_node([0.0, 2.0], 0);
        g.add_edge(a, b, PATH);
        g.add_edge(c, d, PATH);
        assert!(segments_cross(g.nodes[a], g.nodes[b], g.nodes[c], g.nodes[d]).is_some());
        assert!(segments_cross([0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [1.0, 1.0]).is_none());
    }

    #[test]
    fn straight_runs_simplify_but_bends_stay() {
        let mut g = Graph::default();
        let pts = [[0.0, 0.0], [2.0, 0.05], [4.0, 0.0], [6.0, 0.0], [6.0, 3.0]];
        let ids: Vec<usize> = pts.iter().map(|p| g.add_node(*p, 0)).collect();
        for w in ids.windows(2) {
            g.add_edge(w[0], w[1], SIDEWALK);
        }
        let s = simplify(g);
        // The corner at (6, 0) and both ends remain.
        assert!(s.nodes.iter().any(|p| dist(*p, [6.0, 0.0]) < 1e-9));
        assert!(s.nodes.len() <= 4, "{}", s.nodes.len());
        assert_eq!(s.edges.len(), s.nodes.len() - 1);
    }

    #[test]
    fn whole_street_builds_two_sidewalks_and_a_crossing() {
        let f = street_field();
        let text = r#"{"elements":[
            {"type":"node","id":1,"lat":46.5555,"lon":15.6430},
            {"type":"node","id":2,"lat":46.5555,"lon":15.6431}
        ]}"#;
        let osm = crate::osm::read(text).unwrap();
        let mut osm = osm;
        osm.ways.push(crate::osm::Way {
            id: 9,
            tags: [("highway".to_string(), "tertiary".to_string()), ("width".to_string(), "7".to_string())].into_iter().collect(),
            pts: vec![[-40.0, 0.0], [40.0, 0.0]],
            closed: false,
        });
        let (g, s) = build(&osm, &f, &[([0.0, 0.0], [1.0, 0.0])], 64.0);
        assert!(s.sidewalk_points > 50, "{}", s.sidewalk_points);
        assert_eq!(s.crossings, 1);
        let label = g.components();
        assert!(label.iter().all(|&l| l == label[0]), "one network over the crossing");
        assert!(g.nodes.iter().filter(|p| p[1] > 3.0).count() >= 2);
        assert!(g.nodes.iter().filter(|p| p[1] < -3.0).count() >= 2);
    }
}
