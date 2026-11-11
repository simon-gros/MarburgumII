//! lent-ground: builds the ground of `/Game/World/lent` from survey data (ADR-040, ADR-045).
//!
//! The ground covers a square round the middle of Stari most: Lent and, since ADR-045, its eight
//! neighbouring windows of the original game (plan ±768 m).
//!
//! Inputs, all outside the repository or committed beside its output:
//! - the ARSO 2014 LiDAR tiles under the square (GKOT, classified point clouds);
//! - an OpenStreetMap extract of the square (Overpass JSON, ODbL);
//! - the building footprints (`merge-buildings` output) and the list of buildings the level places;
//! - the original game's GURS DOF025 orthophoto of each window, used only to tell grass from hard
//!   ground. Each `<window>_albedo.jpg` is placed by the bounds in the `<window>.json` beside it.
//!
//! `lent-ground merge-buildings` first joins the original's per-window footprints (buildings.rs).
//!
//! Outputs, in the output directory, for the `MarburgumRebuildGround` commandlet:
//! - `heights.bin`: landscape heights (f32, plan metres above the Drava), n × n, row 0 north;
//! - `top.bin`: the walkable top surface (landscape or paving), same grid, for seating actors;
//! - `surface.png`: RGBA surface weights for the landscape (grass, earth, gravel, river stones);
//! - `paved.bin`: road, kerb, pavement and marking meshes in chunks (see mesh.rs);
//! - `ground.json`: sources with digests, grid and chunk layout, counts and checks;
//! - with `--walk-graph <file>`, the walk graph street people follow (walk.rs, ADR-044).
//!
//! `--mode walk` builds only the walk graph, from the streets and the `top.bin` already in `--out`.

mod buildings;
mod grid;
mod mesh;
mod osm;
mod proj;
mod shapes;
mod walk;

use grid::Grid;
use mesh::{PavedMesh, Surface};
use osm::{Osm, Pt, Tags};
use sha2::{Digest, Sha256};
use shapes::Shapes;
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};

/// The extent and resolution, set once from the command line.
struct Extent {
    /// Half the side of the square, metres: the landscape spans −half..half in plan x and y.
    half: f64,
    /// Landscape grid points per side: a whole number of landscape components (2 × 63 quads), plus one.
    points: usize,
    /// Surface weight texture size.
    mask: usize,
}

static EXTENT: std::sync::OnceLock<Extent> = std::sync::OnceLock::new();

fn half() -> f64 {
    EXTENT.get().expect("extent set").half
}

/// Lent and its eight neighbours: 24 components of 126 quads over 1,536 m (0.508 m), and a
/// 4096² surface mask (0.375 m). Unreal builds a landscape from whole components only, so a grid
/// that is not a whole number of them loses its east and south edges.
const DEFAULT_HALF: f64 = 768.0;
const DEFAULT_POINTS: usize = 3025;
const DEFAULT_MASK: usize = 4096;

/// How far under the paving the landscape is pushed, so it never shows through.
const UNDER_PAVING: f32 = 0.15;
/// Kerb height above the carriageway, and at a dropped kerb.
const KERB: f64 = 0.12;
const KERB_DROPPED: f64 = 0.03;

struct Args {
    /// `walk`: only the walk graph, from the streets and the `top.bin` already in the output.
    walk_only: bool,
    laz: Vec<PathBuf>,
    osm: PathBuf,
    buildings: PathBuf,
    placed: PathBuf,
    ortho: Vec<PathBuf>,
    out: PathBuf,
    /// Where the walk graph goes (the game reads it from its content); none to skip it.
    walk_graph: Option<PathBuf>,
}

fn read_flags(args: impl Iterator<Item = String>) -> Result<HashMap<String, String>, String> {
    let mut map = HashMap::new();
    let mut it = args;
    while let Some(k) = it.next() {
        let v = it.next().ok_or(format!("{k} needs a value"))?;
        map.insert(k.trim_start_matches("--").to_string(), v);
    }
    Ok(map)
}

fn list(v: &str) -> Vec<PathBuf> {
    v.split(',').filter(|s| !s.is_empty()).map(PathBuf::from).collect()
}

fn parse_args(map: &HashMap<String, String>) -> Result<Args, String> {
    let get = |k: &str| map.get(k).cloned().ok_or(format!("missing --{k}"));
    let number = |k: &str, d: f64| -> Result<f64, String> { map.get(k).map_or(Ok(d), |v| v.parse::<f64>().map_err(|e| format!("--{k}: {e}"))) };
    let extent = Extent { half: number("half", DEFAULT_HALF)?, points: number("points", DEFAULT_POINTS as f64)? as usize, mask: number("mask", DEFAULT_MASK as f64)? as usize };
    if (extent.points - 1) % 126 != 0 || extent.half <= 0.0 || extent.mask < 2 {
        return Err("--points must be a whole number of 126-quad landscape components plus one, --half and --mask positive".into());
    }
    let _ = EXTENT.set(extent);
    let walk_only = map.get("mode").is_some_and(|m| m == "walk");
    let optional = |k: &str| if walk_only { Ok(map.get(k).cloned().unwrap_or_default()) } else { get(k) };
    Ok(Args {
        walk_only,
        laz: list(&optional("laz")?),
        osm: PathBuf::from(get("osm")?),
        buildings: PathBuf::from(get("buildings")?),
        placed: PathBuf::from(get("placed")?),
        ortho: list(&optional("ortho")?),
        out: PathBuf::from(get("out")?),
        walk_graph: map.get("walk-graph").map(PathBuf::from),
    })
}

pub(crate) fn sha256_file(p: &Path) -> String {
    let bytes = std::fs::read(p).unwrap_or_default();
    format!("{:x}", Sha256::digest(&bytes))
}

fn smoothstep(e0: f64, e1: f64, x: f64) -> f64 {
    let t = ((x - e0) / (e1 - e0)).clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn tag<'a>(t: &'a Tags, k: &str) -> Option<&'a str> {
    t.get(k).map(String::as_str)
}

// ---------------------------------------------------------------------------------------------
// Buildings

struct Building {
    #[allow(dead_code)]
    id: String,
    outline: Vec<Pt>,
    /// Height of the bottom of the walls, as the façade commandlet builds them.
    base: f64,
}

fn read_buildings(path: &Path, placed: &Path) -> Result<Vec<Building>, String> {
    let placed: HashSet<String> = std::fs::read_to_string(placed).map_err(|e| e.to_string())?.lines().map(|l| l.trim().to_string()).filter(|l| !l.is_empty()).collect();
    let root: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(path).map_err(|e| e.to_string())?).map_err(|e| e.to_string())?;
    let mut out = Vec::new();
    for b in root["buildings"].as_array().ok_or("no buildings")? {
        let id = b["id"].as_str().unwrap_or_default().to_string();
        if !placed.contains(&id) {
            continue;
        }
        let ground = b["ground"].as_f64().unwrap_or(0.0);
        let mut base = ground - 0.15;
        if let Some(min) = b["ground_contact"]["terrain_min"].as_f64() {
            let gap = ground - min;
            if gap > 0.05 && gap < 8.0 {
                base = min - 0.15;
            }
        }
        let outline: Vec<Pt> = b["outline"].as_array().into_iter().flatten().filter_map(|p| Some([p[0].as_f64()?, p[1].as_f64()?])).collect();
        out.push(Building { id, outline, base });
    }
    Ok(out)
}

// ---------------------------------------------------------------------------------------------
// LiDAR

struct Lidar {
    ground: Grid,
    known: Grid,
    vegetation: Grid,
    points_in_window: usize,
    ground_points: usize,
}

fn read_lidar(paths: &[PathBuf], bridges: &Shapes) -> Result<Lidar, String> {
    let n = EXTENT.get().expect("extent set").points;
    let half = half();
    let template = Grid::new(n, half, 0.0);
    let s = template.step();
    // Ground returns per grid point, as heights. A flat list sorted by cell afterwards: one small
    // vector per point would cost more than the returns themselves on the wide grid.
    let mut returns: Vec<(u32, f32)> = Vec::new();
    let mut veg = template.like(0.0);
    let mut deck = vec![f32::MIN; n * n];
    let (mut inside, mut ground_points) = (0usize, 0usize);
    // Bridge outlines by grid row, so each return tests only the outlines its row crosses.
    let bridge_rows: Vec<(f64, f64, usize)> = bridges.iter().enumerate().map(|(k, s)| {
        let (lo, hi) = s[0].iter().fold((f64::MAX, f64::MIN), |a, p| (a.0.min(p[1]), a.1.max(p[1])));
        (lo, hi, k)
    }).collect();
    for path in paths {
        let mut reader = las::Reader::from_path(path).map_err(|e| format!("{}: {e}", path.display()))?;
        for p in reader.points() {
            let p = p.map_err(|e| e.to_string())?;
            let (x, y) = proj::d96_to_plan(p.x, p.y);
            if x.abs() > half + s || y.abs() > half + s {
                continue;
            }
            inside += 1;
            let (r, c) = (((y + half) / s).round(), ((x + half) / s).round());
            if r < 0.0 || c < 0.0 || r >= n as f64 || c >= n as f64 {
                continue;
            }
            let i = r as usize * n + c as usize;
            let class = u8::from(p.classification);
            let z = (p.z - proj::HEIGHT_DATUM) as f32;
            let under_bridge = bridge_rows.iter().any(|&(lo, hi, k)| y >= lo && y <= hi && shapes::contains(&bridges[k..k + 1], [x, y]));
            if under_bridge {
                deck[i] = deck[i].max(z);
            }
            match class {
                2 => {
                    returns.push((i as u32, z));
                    ground_points += 1;
                }
                3..=5 => veg.v[i] += 1.0,
                _ => {}
            }
        }
        eprintln!("survey: {} read, {} returns in the square so far", path.display(), inside);
    }
    // The 2014 survey classed Stari most's deck as ground and saw almost nothing beneath it, so
    // every return inside a bridge outline is dropped and the ground there is filled in.
    returns.retain(|&(i, _)| deck[i as usize] == f32::MIN);
    returns.sort_unstable_by(|a, b| a.0.cmp(&b.0).then(a.1.partial_cmp(&b.1).unwrap()));
    let mut ground = template.like(0.0);
    let mut known = template.like(0.0);
    for run in returns.chunk_by(|a, b| a.0 == b.0) {
        let i = run[0].0 as usize;
        ground.v[i] = run[run.len() / 2].1;
        known.v[i] = 1.0;
    }
    Ok(Lidar { ground, known, vegetation: veg, points_in_window: inside, ground_points })
}

// ---------------------------------------------------------------------------------------------
// Streets

#[derive(Clone, Copy, PartialEq)]
enum WayKind {
    Carriageway { width: f64, centre_line: bool },
    Footway { width: f64 },
    Unpaved { width: f64 },
    Skip,
}

fn parse_width(t: &Tags) -> Option<f64> {
    tag(t, "width").and_then(|w| w.trim_end_matches('m').trim().replace(',', ".").parse::<f64>().ok()).filter(|w| *w > 0.5 && *w < 40.0)
}

fn classify_way(t: &Tags) -> WayKind {
    if tag(t, "bridge").is_some_and(|b| b != "no") || tag(t, "tunnel").is_some_and(|b| b != "no") || tag(t, "indoor") == Some("yes") {
        return WayKind::Skip;
    }
    if tag(t, "area") == Some("yes") {
        return WayKind::Skip; // handled as an area
    }
    let Some(hw) = tag(t, "highway") else { return WayKind::Skip };
    let oneway = matches!(tag(t, "oneway"), Some("yes") | Some("1") | Some("-1")) || tag(t, "junction") == Some("roundabout");
    let lanes = tag(t, "lanes").and_then(|l| l.parse::<f64>().ok());
    let unpaved = matches!(tag(t, "surface"), Some("unpaved" | "gravel" | "fine_gravel" | "dirt" | "ground" | "earth" | "grass" | "compacted" | "sand" | "mud" | "pebblestone" | "woodchips"));
    let road = |two_way: f64, one_way: f64, marked: bool| {
        let width = parse_width(t).or(lanes.map(|l| l * 3.1 + 0.6)).unwrap_or(if oneway { one_way } else { two_way });
        let marked = marked && !oneway && lanes.map_or(true, |l| l >= 2.0);
        WayKind::Carriageway { width, centre_line: marked }
    };
    match hw {
        "motorway" | "trunk" | "primary" | "secondary" => road(9.0, 6.5, true),
        "tertiary" => road(7.5, 5.5, true),
        "unclassified" => road(6.5, 4.5, true),
        "residential" => road(6.0, 4.0, false),
        "busway" => road(6.5, 3.6, false),
        "living_street" => road(5.0, 4.0, false),
        h if h.ends_with("_link") => road(6.0, 4.5, false),
        "service" => {
            let w = match tag(t, "service") {
                Some("parking_aisle") => 5.0,
                Some("driveway") | Some("alley") => 3.2,
                _ => 4.0,
            };
            if unpaved { WayKind::Unpaved { width: w } } else { road(w, w, false) }
        }
        "track" => WayKind::Unpaved { width: parse_width(t).unwrap_or(3.0) },
        "pedestrian" => WayKind::Footway { width: parse_width(t).unwrap_or(5.0) },
        "footway" if tag(t, "footway") == Some("crossing") => WayKind::Skip,
        "cycleway" if tag(t, "cycleway") == Some("crossing") => WayKind::Skip,
        "footway" | "cycleway" | "path" | "bridleway" => {
            let w = parse_width(t).unwrap_or(match hw {
                "cycleway" => 1.8,
                "path" => 1.6,
                _ if tag(t, "footway") == Some("sidewalk") => 2.2,
                _ => 2.0,
            });
            // A path with no surface tag in a park is gravel; footways and cycleways in town are paved.
            if unpaved || (hw == "path" && tag(t, "surface").is_none()) { WayKind::Unpaved { width: w } } else { WayKind::Footway { width: w } }
        }
        _ => WayKind::Skip,
    }
}

struct Streets {
    carriageway: Shapes,
    pavement: Shapes,
    plazas: Shapes,
    parking: Shapes,
    setts: Shapes,
    unpaved: Shapes,
    bridges: Shapes,
    water: Shapes,
    junctions: Vec<Pt>,
    centre_lines: Vec<Vec<Pt>>,
    road_paths: Vec<Vec<Pt>>,
    crossings: Vec<(Pt, bool)>,
}

fn area_shapes(osm: &Osm, keep: impl Fn(&Tags) -> bool) -> Shapes {
    let parts: Vec<Shapes> = osm.areas.iter().filter(|a| keep(&a.tags)).map(|a| shapes::polygon(&a.rings)).collect();
    shapes::union_all(&parts)
}

fn streets(osm: &Osm, buildings: &Shapes, window: &Shapes) -> Streets {
    let mut road_strokes = Vec::new();
    let mut walk_strokes = Vec::new();
    let mut unpaved_strokes = Vec::new();
    let mut centre_lines = Vec::new();
    let mut road_paths = Vec::new();
    let mut node_use: HashMap<(i64, i64), usize> = HashMap::new();
    let key = |p: &Pt| ((p[0] * 100.0).round() as i64, (p[1] * 100.0).round() as i64);
    let near = |pts: &[Pt]| pts.iter().any(|p| p[0].abs() < half() + 60.0 && p[1].abs() < half() + 60.0);
    for w in &osm.ways {
        if !near(&w.pts) {
            continue;
        }
        match classify_way(&w.tags) {
            WayKind::Carriageway { width, centre_line } => {
                road_strokes.push(shapes::stroke(&w.pts, width, true));
                // Sidewalks the map records only as a tag on the road.
                let sidewalk = tag(&w.tags, "sidewalk").or(tag(&w.tags, "sidewalk:both"));
                if matches!(sidewalk, Some("both" | "left" | "right" | "yes")) {
                    walk_strokes.push(shapes::stroke(&w.pts, width + 2.0 * 2.0, true));
                }
                if centre_line {
                    centre_lines.push(w.pts.clone());
                }
                road_paths.push(w.pts.clone());
                for p in [&w.pts[0], w.pts.last().unwrap()] {
                    *node_use.entry(key(p)).or_default() += 2;
                }
                for p in &w.pts[1..w.pts.len() - 1] {
                    *node_use.entry(key(p)).or_default() += 1;
                }
            }
            WayKind::Footway { width } => walk_strokes.push(shapes::stroke(&w.pts, width, true)),
            WayKind::Unpaved { width } => unpaved_strokes.push(shapes::stroke(&w.pts, width, true)),
            WayKind::Skip => {}
        }
    }
    // A point used by more than two road ends or passes is a junction.
    let mut junctions = Vec::new();
    for w in &osm.ways {
        if matches!(classify_way(&w.tags), WayKind::Carriageway { .. }) {
            for p in &w.pts {
                if node_use.get(&key(p)).copied().unwrap_or(0) >= 3 {
                    junctions.push(*p);
                }
            }
        }
    }

    let is_road_area = |t: &Tags| matches!(tag(t, "area:highway"), Some("primary" | "secondary" | "tertiary" | "residential" | "unclassified" | "service" | "junction" | "living_street" | "busway"));
    let road_areas = area_shapes(osm, is_road_area);
    let islands = area_shapes(osm, |t| tag(t, "area:highway") == Some("traffic_island"));
    let parking = area_shapes(osm, |t| {
        tag(t, "amenity") == Some("parking")
            && !matches!(tag(t, "parking"), Some("underground" | "multi-storey" | "rooftop"))
            && !matches!(tag(t, "surface"), Some("grass" | "gravel" | "unpaved" | "dirt" | "grass_paver" | "fine_gravel" | "compacted"))
    });
    let plazas = area_shapes(osm, |t| {
        (tag(t, "highway") == Some("pedestrian"))
            || tag(t, "place") == Some("square")
            || matches!(tag(t, "area:highway"), Some("footway" | "pedestrian" | "cycleway"))
            || tag(t, "leisure") == Some("outdoor_seating")
    });
    let setts = area_shapes(osm, |t| matches!(tag(t, "surface"), Some("sett" | "cobblestone" | "unhewn_cobblestone" | "paving_stones" | "granite")) && (t.contains_key("highway") || t.contains_key("place") || t.contains_key("area:highway")));
    let bridges = area_shapes(osm, |t| tag(t, "man_made") == Some("bridge"));
    let water = area_shapes(osm, |t| tag(t, "natural") == Some("water") || tag(t, "waterway") == Some("riverbank"));

    // Streets pass under bridges, so only buildings and the river cut them.
    let blocked = shapes::union_all(&[buildings.clone(), water.clone()]);
    let roads = shapes::union_all(&[shapes::union_all(&road_strokes), road_areas, parking.clone()]);
    // Kerb radii at junctions, then nothing narrower than a metre.
    let roads = shapes::open(&shapes::close(&roads, 4.0), 0.4);
    let roads = shapes::difference(&shapes::difference(&roads, &islands), &blocked);
    let carriageway = shapes::intersect(&roads, window);
    let carriageway_index = shapes::Index::new(&carriageway);

    // Pavement: mapped footways and sidewalks, plazas and islands, a kerb-and-verge band round every
    // carriageway, and the strip between a road and the house fronts when it is narrow.
    let walk = shapes::union_all(&walk_strokes);
    let band = shapes::offset(&carriageway, 0.6);
    let fronts = shapes::intersect(&shapes::offset(buildings, 6.0), &shapes::offset(&carriageway, 12.0));
    // Every house stands on a paved apron, so no wall rises straight out of a grass bank.
    let aprons = shapes::offset(buildings, 1.5);
    let pavement = shapes::union_all(&[walk, plazas.clone(), islands, band, fronts, aprons]);
    // Close gaps narrower than about 4 m, including between paving and a wall.
    let pavement = shapes::difference(&shapes::close(&shapes::union_all(&[pavement, buildings.clone()]), 2.0), buildings);
    let pavement = shapes::difference(&shapes::difference(&pavement, &carriageway), &blocked);
    // Opening approximates its arcs, so take the carriageway out once more afterwards.
    let pavement = shapes::difference(&shapes::open(&pavement, 0.25), &carriageway);
    let pavement = shapes::intersect(&pavement, window);

    let unpaved = shapes::difference(&shapes::union_all(&unpaved_strokes), &shapes::union_all(&[carriageway.clone(), pavement.clone(), blocked.clone()]));

    let crossings = osm
        .nodes
        .iter()
        .filter(|n| tag(&n.tags, "highway") == Some("crossing") && carriageway_index.contains(n.at))
        .map(|n| {
            let marked = matches!(tag(&n.tags, "crossing"), Some("zebra" | "marked" | "uncontrolled" | "traffic_signals"))
                || tag(&n.tags, "crossing_ref") == Some("zebra")
                || tag(&n.tags, "crossing:markings").is_some_and(|m| m != "no");
            (n.at, marked)
        })
        .collect();

    Streets { carriageway, pavement, plazas, parking, setts, unpaved, bridges, water, junctions, centre_lines, road_paths, crossings }
}

/// The direction of the nearest road at `p`.
fn road_direction(paths: &[Vec<Pt>], p: Pt) -> [f64; 2] {
    let mut best = (f64::MAX, [1.0, 0.0]);
    for path in paths {
        for s in path.windows(2) {
            let (a, b) = (s[0], s[1]);
            let d = [b[0] - a[0], b[1] - a[1]];
            let len2 = d[0] * d[0] + d[1] * d[1];
            if len2 < 1e-9 {
                continue;
            }
            let t = (((p[0] - a[0]) * d[0] + (p[1] - a[1]) * d[1]) / len2).clamp(0.0, 1.0);
            let q = [a[0] + d[0] * t, a[1] + d[1] * t];
            let dist = (q[0] - p[0]).powi(2) + (q[1] - p[1]).powi(2);
            if dist < best.0 {
                let l = len2.sqrt();
                best = (dist, [d[0] / l, d[1] / l]);
            }
        }
    }
    best.1
}

/// Zebra stripes across the carriageway at a marked crossing: 0.5 m bars, 0.5 m apart, 3 m long.
fn zebra(at: Pt, dir: [f64; 2], carriageway: &Shapes) -> Shapes {
    let across = [-dir[1], dir[0]];
    let mut bars = Vec::new();
    let mut k = -12.0;
    while k <= 12.0 {
        let c = [at[0] + across[0] * k, at[1] + across[1] * k];
        let a = [c[0] - dir[0] * 1.5, c[1] - dir[1] * 1.5];
        let b = [c[0] + dir[0] * 1.5, c[1] + dir[1] * 1.5];
        bars.push(shapes::stroke(&[a, b], 0.5, false));
        k += 1.0;
    }
    shapes::intersect(&shapes::union_all(&bars), &shapes::offset(carriageway, -0.35))
}

/// Dashed centre line: 3 m dashes, 6 m gaps, 12 cm wide.
fn centre_dashes(path: &[Pt]) -> Shapes {
    let mut dashes = Vec::new();
    let (mut s, dash, gap) = (0.0, 3.0, 6.0);
    let mut acc = 0.0;
    let seg_len: Vec<f64> = path.windows(2).map(|w| ((w[1][0] - w[0][0]).powi(2) + (w[1][1] - w[0][1]).powi(2)).sqrt()).collect();
    let total: f64 = seg_len.iter().sum();
    let point_at = |d: f64| -> Pt {
        let mut rem = d;
        for (i, l) in seg_len.iter().enumerate() {
            if rem <= *l || i == seg_len.len() - 1 {
                let t = if *l > 0.0 { (rem / l).clamp(0.0, 1.0) } else { 0.0 };
                return [path[i][0] + (path[i + 1][0] - path[i][0]) * t, path[i][1] + (path[i + 1][1] - path[i][1]) * t];
            }
            rem -= l;
        }
        *path.last().unwrap()
    };
    while s + dash <= total {
        let mut pts = vec![point_at(s)];
        // Keep the corners inside a dash.
        acc = 0.0;
        for (i, l) in seg_len.iter().enumerate() {
            acc += l;
            if acc > s && acc < s + dash {
                pts.push(path[i + 1]);
            }
        }
        pts.push(point_at(s + dash));
        dashes.push(shapes::stroke(&pts, 0.12, false));
        s += dash + gap;
    }
    let _ = acc;
    shapes::union_all(&dashes)
}

/// Connected groups of points with a positive value: where they are, how big, how deep.
fn clusters(g: &Grid, depth: &[f32]) -> Vec<serde_json::Value> {
    let n = g.n;
    let mut seen = vec![false; depth.len()];
    let mut out = Vec::new();
    for start in 0..depth.len() {
        if depth[start] <= 0.0 || seen[start] {
            continue;
        }
        let (mut stack, mut count, mut sx, mut sy, mut deepest) = (vec![start], 0usize, 0.0, 0.0, 0.0f32);
        seen[start] = true;
        while let Some(i) = stack.pop() {
            let p = g.pos(i / n, i % n);
            (count, sx, sy, deepest) = (count + 1, sx + p[0], sy + p[1], deepest.max(depth[i]));
            let (r, c) = (i / n, i % n);
            for (dr, dc) in [(0i64, 1i64), (0, -1), (1, 0), (-1, 0)] {
                let (rr, cc) = (r as i64 + dr, c as i64 + dc);
                if rr >= 0 && cc >= 0 && (rr as usize) < n && (cc as usize) < n {
                    let j = rr as usize * n + cc as usize;
                    if depth[j] > 0.0 && !seen[j] {
                        seen[j] = true;
                        stack.push(j);
                    }
                }
            }
        }
        let step = g.step();
        out.push(serde_json::json!({ "x": (sx / count as f64).round(), "y": (sy / count as f64).round(), "area_m2": (count as f64 * step * step).round(), "deepest_m": (deepest * 10.0).round() / 10.0 }));
    }
    out
}

// ---------------------------------------------------------------------------------------------

fn read_f32(path: &Path, n: usize, half: f64) -> Result<Grid, String> {
    let bytes = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
    if bytes.len() != n * n * 4 {
        return Err(format!("{}: {} bytes, expected {}", path.display(), bytes.len(), n * n * 4));
    }
    let mut g = Grid::new(n, half, 0.0);
    for (i, c) in bytes.chunks_exact(4).enumerate() {
        g.v[i] = f32::from_le_bytes([c[0], c[1], c[2], c[3]]);
    }
    Ok(g)
}

/// The walk graph street people follow (walk.rs), written beside the ground.
fn write_walk_graph(args: &Args, osm: &Osm, st: &Streets, footprints: &Shapes, top: &Grid) -> Result<(), String> {
    let Some(path) = &args.walk_graph else {
        return if args.walk_only { Err("--mode walk needs --walk-graph".into()) } else { Ok(()) };
    };
    let t0 = std::time::Instant::now();
    let walkable = [&st.pavement, &st.unpaved, &st.plazas];
    let blocked = shapes::union_all(&[footprints.clone(), st.water.clone()]);
    let field = walk::Field::new(&walkable, &st.carriageway, &blocked, half());
    let crossings: Vec<(Pt, [f64; 2])> = st.crossings.iter().map(|(at, _)| (*at, road_direction(&st.road_paths, *at))).collect();
    let (graph, summary) = walk::build(osm, &field, &crossings, half());
    let sources = serde_json::json!([
        { "role": "streets and paths", "name": format!("OpenStreetMap via Overpass, {}", osm.timestamp), "licence": "ODbL 1.0, © OpenStreetMap contributors", "sha256": sha256_file(&args.osm) },
        { "role": "building footprints", "name": "magna-raedarum-marburgum <window>_buildings.json, joined (lent-ground merge-buildings)", "sha256": sha256_file(&args.buildings) },
        { "role": "walkable top heights", "name": "top.bin (this tool)", "sha256": sha256_file(&args.out.join("top.bin")) },
    ]);
    let json = walk::to_json(&graph, top, &summary, sources);
    if let Some(dir) = path.parent() {
        std::fs::create_dir_all(dir).map_err(|e| e.to_string())?;
    }
    std::fs::write(path, serde_json::to_string(&json).unwrap() + "\n").map_err(|e| e.to_string())?;
    eprintln!("walk graph: {} ({:.1}s)", json["summary"], t0.elapsed().as_secs_f64());
    Ok(())
}

fn write_f32(path: &Path, g: &Grid) -> std::io::Result<()> {
    let mut out = Vec::with_capacity(g.v.len() * 4);
    for v in &g.v {
        out.extend_from_slice(&v.to_le_bytes());
    }
    std::fs::write(path, out)
}

/// One window's orthophoto and the plan square it covers.
struct Ortho {
    width: usize,
    height: usize,
    pixels: Vec<u8>,
    /// Plan x of the west edge and y of the north edge, and the side, metres.
    x0: f64,
    y0: f64,
    side: f64,
}

/// Reads `<window>_albedo.jpg` and places it by the bounds in the `<window>.json` beside it.
fn read_ortho(path: &Path) -> Result<Ortho, String> {
    let file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut decoder = jpeg_decoder::Decoder::new(std::io::BufReader::new(file));
    let pixels = decoder.decode().map_err(|e| e.to_string())?;
    let info = decoder.info().ok_or("no jpeg info")?;
    let stem = path.file_stem().and_then(|s| s.to_str()).unwrap_or_default();
    let window = path.with_file_name(format!("{}.json", stem.trim_end_matches("_albedo")));
    let bounds: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&window).map_err(|e| format!("{}: {e}", window.display()))?).map_err(|e| e.to_string())?;
    let get = |k: &str| bounds[k].as_f64().ok_or(format!("{}: no {k}", window.display()));
    let (west, east, north) = (get("west")?, get("east")?, get("north")?);
    let (x0, y0) = proj::d96_to_plan(west, north);
    Ok(Ortho { width: info.width as usize, height: info.height as usize, pixels, x0, y0, side: east - west })
}

const USAGE: &str = "usage:
  lent-ground merge-buildings --windows lent,center,... --terrain DIR --out buildings.json
  lent-ground --laz A.laz,B.laz,... --osm osm_lent.json.gz --buildings buildings.json --placed placed.txt
              --ortho DIR/lent_albedo.jpg,DIR/center_albedo.jpg,... --out DIR [--half 768 --points 3025 --mask 4096]
              [--walk-graph Content/Marburgum/Street/lent_walk_graph.json] [--mode walk]";

fn main() {
    let mut argv: Vec<String> = std::env::args().skip(1).collect();
    if argv.first().map(String::as_str) == Some("merge-buildings") {
        argv.remove(0);
        let result = read_flags(argv.into_iter()).and_then(|f| {
            let get = |k: &str| f.get(k).cloned().ok_or(format!("missing --{k}"));
            let terrain = PathBuf::from(get("terrain")?);
            let sources: Vec<(String, PathBuf)> = get("windows")?.split(',').map(|w| (w.to_string(), terrain.join(format!("{w}_buildings.json")))).collect();
            buildings::merge(&sources, Path::new(&get("out")?))
        });
        if let Err(e) = result {
            eprintln!("lent-ground: {e}\n{USAGE}");
            std::process::exit(2);
        }
        return;
    }
    let args = match read_flags(argv.into_iter()).and_then(|f| parse_args(&f)) {
        Ok(a) => a,
        Err(e) => {
            eprintln!("lent-ground: {e}\n{USAGE}");
            std::process::exit(2);
        }
    };
    if let Err(e) = run(&args) {
        eprintln!("lent-ground: {e}");
        std::process::exit(1);
    }
}

fn run(args: &Args) -> Result<(), String> {
    let t0 = std::time::Instant::now();
    std::fs::create_dir_all(&args.out).map_err(|e| e.to_string())?;
    let osm_text = {
        let bytes = std::fs::read(&args.osm).map_err(|e| e.to_string())?;
        if args.osm.extension().is_some_and(|e| e == "gz") {
            let mut text = String::new();
            std::io::Read::read_to_string(&mut flate2::read::GzDecoder::new(&bytes[..]), &mut text).map_err(|e| e.to_string())?;
            text
        } else {
            String::from_utf8(bytes).map_err(|e| e.to_string())?
        }
    };
    let osm = osm::read(&osm_text)?;
    let buildings = read_buildings(&args.buildings, &args.placed)?;
    let footprints = shapes::union_all(&buildings.iter().map(|b| shapes::polygon(&[b.outline.clone()])).collect::<Vec<_>>());
    // Paving stops a metre short of the landscape edge, inside its last row of quads.
    let window = shapes::square(half() - 1.0);
    let st = streets(&osm, &footprints, &window);
    eprintln!("streets: carriageway {:.0} m², pavement {:.0} m², {} crossings, {} junctions ({:.1}s)", shapes::area(&st.carriageway), shapes::area(&st.pavement), st.crossings.len(), st.junctions.len(), t0.elapsed().as_secs_f64());
    if args.walk_only {
        let top = read_f32(&args.out.join("top.bin"), EXTENT.get().expect("extent set").points, half())?;
        return write_walk_graph(args, &osm, &st, &footprints, &top);
    }

    // ---- bare ground ----------------------------------------------------------------------
    let lidar = read_lidar(&args.laz, &st.bridges)?;
    let mut ground = lidar.ground.clone();
    let mut known = lidar.known.clone();
    let mut water_mask = ground.like(0.0);
    for s in &st.water {
        water_mask.fill_rings(s, 1.0);
    }
    let land = {
        let mut m = water_mask.clone();
        for v in m.v.iter_mut() {
            *v = 1.0 - *v;
        }
        m
    };
    let from_shore = Grid::distance_to(&land);
    // The Drava's bed: no returns under water, so shape it from the shore: 0.4 m at the edge,
    // falling 1 in 6 to 2.6 m.
    for i in 0..ground.v.len() {
        if water_mask.v[i] > 0.5 {
            let bed = -(0.4 + from_shore.v[i] as f64 / 6.0).min(2.6) as f32;
            // Returns off the water surface come back as ground near 0; only real ground above
            // the water (a bank inside the mapped outline) is kept.
            if known.v[i] < 0.5 || ground.v[i] < 0.3 {
                ground.v[i] = bed;
                known.v[i] = 1.0;
            }
        }
    }
    // Under the bridges (both run north–south) the ground continues from either side: fill
    // each row across the outline from the nearest survey points east and west of it, rather
    // than letting the street levels at the bridge ends spread under the deck as a ramp.
    let mut bridge_mask = ground.like(0.0);
    for s in &st.bridges {
        bridge_mask.fill_rings(s, 1.0);
    }
    let n = ground.n;
    let reach = (40.0 / ground.step()) as usize;
    let mut filled_under_bridges = 0usize;
    for r in 0..n {
        let row_known: Vec<bool> = (0..n).map(|c| known.at(r, c) > 0.5).collect();
        for c in 0..n {
            if bridge_mask.at(r, c) < 0.5 || row_known[c] {
                continue;
            }
            let west = (c.saturating_sub(reach)..c).rev().find(|&k| row_known[k] && bridge_mask.at(r, k) < 0.5);
            let east = (c + 1..(c + reach).min(n)).find(|&k| row_known[k] && bridge_mask.at(r, k) < 0.5);
            if let (Some(w), Some(e)) = (west, east) {
                let t = (c - w) as f32 / (e - w) as f32;
                ground.set(r, c, ground.at(r, w) * (1.0 - t) + ground.at(r, e) * t);
                known.set(r, c, 1.0);
                filled_under_bridges += 1;
            }
        }
    }
    let measured = lidar.known.v.iter().filter(|&&k| k > 0.5).count();
    ground.inpaint(&known);
    // Blobs narrower than about 4 m standing more than 40 cm proud of the ground round them are
    // not ground (bridge piers, kiosks, returns the classifier missed): open them away. Wider
    // terraces and the steps between them stay.
    let opened = ground.opened(4);
    let mut blobs = 0usize;
    for i in 0..ground.v.len() {
        if ground.v[i] - opened.v[i] > 0.4 {
            ground.v[i] = opened.v[i];
            blobs += 1;
        }
    }
    // Hollows with no way out more than 3 m below their rim, away from the river and the streets,
    // are reported, not changed: most are courtyards and light wells inside building blocks, but
    // a few are building sites of the 2014 survey (the Minorite courtyard behind the puppet
    // theatre was dug 17 m deep) or later sunken squares that a 1990s map may not want.
    let filled = ground.depressions_filled();
    let mut keep_low = ground.like(0.0);
    for s in st.water.iter().chain(st.carriageway.iter()).chain(st.pavement.iter()) {
        keep_low.fill_rings(s, 1.0);
    }
    let mut hollow_depth = vec![0.0f32; ground.v.len()];
    for i in 0..ground.v.len() {
        let depth = filled.v[i] - ground.v[i];
        if depth > 3.0 && keep_low.v[i] < 0.5 {
            hollow_depth[i] = depth;
        }
    }
    let mut hollows = clusters(&ground, &hollow_depth);
    hollows.retain(|h| h["area_m2"].as_f64().unwrap_or(0.0) >= 100.0);
    hollows.sort_by(|a, b| b["area_m2"].as_f64().partial_cmp(&a["area_m2"].as_f64()).unwrap());
    // Light smoothing takes out survey noise; a stronger pass would round off the banks.
    let ground = ground.blur(0.55, None);

    // ---- paving heights ---------------------------------------------------------------------
    let mut c_mask = ground.like(0.0);
    for s in &st.carriageway {
        c_mask.fill_rings(s, 1.0);
    }
    let mut p_mask = ground.like(0.0);
    for s in &st.pavement {
        p_mask.fill_rings(s, 1.0);
    }
    let road = ground.blur(1.6, Some(&c_mask));
    let side = ground.blur(0.9, Some(&p_mask));
    let from_road = Grid::distance_to(&c_mask);
    let mut crossing_mask = ground.like(0.0);
    for (at, _) in &st.crossings {
        let (r, c) = crossing_mask.rc(*at);
        if r >= 0.0 && c >= 0.0 && (r as usize) < crossing_mask.n && (c as usize) < crossing_mask.n {
            crossing_mask.set(r.round() as usize, c.round() as usize, 1.0);
        }
    }
    let from_crossing = Grid::distance_to(&crossing_mask);
    let mut pave = ground.like(0.0);
    for i in 0..pave.v.len() {
        let d = from_road.v[i] as f64;
        let kerb = KERB_DROPPED + (KERB - KERB_DROPPED) * smoothstep(1.8, 3.6, from_crossing.v[i] as f64);
        let r = road.v[i] as f64;
        let near = (side.v[i] as f64).clamp(r + kerb, r + kerb + 0.12);
        let t = smoothstep(3.0, 9.0, d);
        pave.v[i] = (near * (1.0 - t) + (side.v[i] as f64 + 0.03) * t) as f32;
    }
    // Paving that meets a wall comes up to cover the wall's bottom edge, easing back over 2 m,
    // so no gap shows under a façade.
    let mut wall_lift = 0usize;
    let mut near_wall: Vec<(f32, f32)> = vec![(f32::MIN, 0.0); pave.v.len()];
    for b in &buildings {
        for (i, d) in ground.near_polygon(&[b.outline.clone()], 2.5).iter() {
            let d = d as f64;
            if d > 0.0 && d < 2.0 {
                let w = (1.0 - smoothstep(0.75, 2.0, d)) as f32;
                if w > near_wall[i].1 || (w == near_wall[i].1 && (b.base as f32) > near_wall[i].0) {
                    near_wall[i] = ((b.base + 0.03) as f32, w);
                }
            }
        }
    }
    for i in 0..pave.v.len() {
        let (need, w) = near_wall[i];
        if w > 0.0 && need > pave.v[i] {
            pave.v[i] += (need - pave.v[i]) * w;
            wall_lift += 1;
        }
    }
    let road_z = |p: Pt| road.sample(p) as f64;
    let pave_z = |p: Pt| pave.sample(p) as f64;

    // ---- meshes --------------------------------------------------------------------------------
    let kerb_band = shapes::difference(&shapes::intersect(&shapes::offset(&st.carriageway, 0.16), &st.pavement), &st.carriageway);
    let footway = shapes::difference(&st.pavement, &kerb_band);
    let mut mesh = PavedMesh::new(half(), 64.0);
    // Fine enough that markings, laid on the same fields, never dip under the asphalt.
    let c_tris = mesh::triangulate(&st.carriageway, 0.6);
    // Each of a few million triangles and kerb segments asks which surface it lies in.
    let (parking, setts, carriageway) = (shapes::Index::new(&st.parking), shapes::Index::new(&st.setts), shapes::Index::new(&st.carriageway));
    mesh.add_surface(&c_tris, &road_z, &|c| if parking.contains(c) { Surface::YardAsphalt } else if setts.contains(c) { Surface::Setts } else { Surface::Asphalt });
    let k_tris = mesh::triangulate(&kerb_band, 0.6);
    mesh.add_surface(&k_tris, &pave_z, &|_| Surface::Kerb);
    let f_tris = mesh::triangulate(&footway, 1.2);
    let squares = shapes::union_all(&[st.plazas.clone(), st.setts.clone()]);
    let squares = shapes::Index::new(&squares);
    mesh.add_surface(&f_tris, &pave_z, &|c| if squares.contains(c) { Surface::Setts } else { Surface::Pavers });
    // Kerb faces: from the road up to the pavement along every carriageway edge, facing the road.
    for contour in st.carriageway.iter().flatten() {
        mesh.add_wall(contour, true, &road_z, &pave_z, &|a, b| {
            let d = [b[0] - a[0], b[1] - a[1]];
            let l = (d[0] * d[0] + d[1] * d[1]).sqrt();
            // Interior on the left of i_overlay's contours: face the road.
            Some([-d[1] / l, d[0] / l])
        }, Surface::Kerb);
    }
    // Edging: down from the pavement's outer edge under the landscape, facing outwards.
    for contour in st.pavement.iter().flatten() {
        mesh.add_wall(contour, true, &|p| pave_z(p) - 0.35, &pave_z, &|a, b| {
            let d = [b[0] - a[0], b[1] - a[1]];
            let l = (d[0] * d[0] + d[1] * d[1]).sqrt();
            let out = [d[1] / l, -d[0] / l];
            let mid = [(a[0] + b[0]) / 2.0 + out[0] * 0.08, (a[1] + b[1]) / 2.0 + out[1] * 0.08];
            if carriageway.contains(mid) { None } else { Some(out) }
        }, Surface::Kerb);
    }
    // Carriageway edges with no pavement beside them (the window edge, bridge ends) get a skirt too.
    // Markings.
    let junction_zone = shapes::union_all(&st.junctions.iter().map(|j| shapes::stroke(&[*j, [j[0] + 0.01, j[1]]], 18.0, true)).collect::<Vec<_>>());
    let mut zebras = Vec::new();
    for (at, marked) in &st.crossings {
        if *marked {
            zebras.push(zebra(*at, road_direction(&st.road_paths, *at), &st.carriageway));
        }
    }
    let zebras = shapes::union_all(&zebras);
    let dashes = shapes::union_all(&st.centre_lines.iter().map(|p| centre_dashes(p)).collect::<Vec<_>>());
    let dashes = shapes::difference(&shapes::difference(&shapes::intersect(&dashes, &shapes::offset(&st.carriageway, -1.0)), &junction_zone), &shapes::offset(&zebras, 2.0));
    let paint = shapes::difference(&shapes::union_all(&[zebras, dashes]), &st.parking);
    let m_tris = mesh::triangulate(&paint, 0.4);
    mesh.add_surface(&m_tris, &|p| road_z(p) + 0.012, &|_| Surface::Paint);
    eprintln!("meshes: {} triangles ({:.1}s)", mesh.triangle_count(), t0.elapsed().as_secs_f64());

    // ---- landscape -----------------------------------------------------------------------------
    let mut landscape = ground.clone();
    let mut paved_any = c_mask.clone();
    for i in 0..paved_any.v.len() {
        paved_any.v[i] = paved_any.v[i].max(p_mask.v[i]);
    }
    // Under the paving the landscape goes below the meshes as they are drawn, triangle by
    // triangle, not below the fields they were sampled from.
    let drawn = mesh.top_over(&ground);
    let mut top = ground.clone();
    for i in 0..landscape.v.len() {
        let z = drawn.v[i];
        if !z.is_nan() {
            landscape.v[i] = landscape.v[i].min(z) - UNDER_PAVING;
            top.v[i] = z;
            paved_any.v[i] = 1.0;
        }
    }
    // A paved edge can fall between grid points; the landscape there is interpolated from the
    // points either side. Keep every point next to paving below the lowest paving beside it.
    let n = landscape.n;
    let mut next_to_paving = 0usize;
    let before = landscape.clone();
    for r in 0..n {
        for c in 0..n {
            let mut lowest = f32::INFINITY;
            for dr in -1i32..=1 {
                for dc in -1i32..=1 {
                    let (rr, cc) = (r as i32 + dr, c as i32 + dc);
                    if rr >= 0 && cc >= 0 && (rr as usize) < n && (cc as usize) < n {
                        let z = drawn.at(rr as usize, cc as usize);
                        if !z.is_nan() {
                            lowest = lowest.min(z);
                        }
                    }
                }
            }
            if lowest.is_finite() && before.at(r, c) > lowest - 0.05 {
                landscape.set(r, c, lowest - 0.05);
                next_to_paving += 1;
            }
        }
    }
    // Paving can dip between grid points (it is drawn finer than the landscape): every corner of
    // a landscape cell goes below the lowest paved corner that reaches into the cell.
    let lowest = mesh.lowest_in_cells(&landscape);
    let mut under_dips = 0usize;
    for r in 0..n - 1 {
        for c in 0..n - 1 {
            let low = lowest[r * n + c];
            if !low.is_finite() {
                continue;
            }
            for (rr, cc) in [(r, c), (r + 1, c), (r, c + 1), (r + 1, c + 1)] {
                if landscape.at(rr, cc) > low - 0.05 {
                    landscape.set(rr, cc, low - 0.05);
                    under_dips += 1;
                }
            }
        }
    }
    // Just outside the paving the landscape may not stand above it: a narrow strip can fall
    // between grid points, and a bank beside a path would otherwise lap over its edge.
    let from_paved = Grid::distance_to(&paved_any);
    let top_near = top.blur(0.5, Some(&paved_any));
    let mut edge_lowered = 0usize;
    for i in 0..landscape.v.len() {
        let d = from_paved.v[i];
        if d > 0.0 && d <= 0.8 && landscape.v[i] > top_near.v[i] - 0.04 {
            landscape.v[i] = top_near.v[i] - 0.04;
            top.v[i] = top.v[i].max(landscape.v[i]);
            edge_lowered += 1;
        }
    }
    // Seat every building: unpaved ground within 0.75 m of a wall comes up to cover the wall's
    // bottom edge, so no wall shows a gap or hangs over a dip.
    let mut wall_gaps = 0usize;
    let mut raised = 0usize;
    for b in &buildings {
        for (i, d) in ground.near_polygon(&[b.outline.clone()], 1.5).iter() {
            if d > 0.0 && d <= 0.75 {
                if paved_any.v[i] > 0.5 {
                    if (top.v[i] as f64) < b.base + 0.02 {
                        wall_gaps += 1;
                    }
                } else if from_paved.v[i] > 0.8 && (landscape.v[i] as f64) < b.base + 0.06 {
                    landscape.v[i] = (b.base + 0.06) as f32;
                    top.v[i] = landscape.v[i];
                    raised += 1;
                }
            }
        }
    }

    // ---- surface weights -------------------------------------------------------------------------
    let orthos = args.ortho.iter().map(|p| read_ortho(p)).collect::<Result<Vec<_>, _>>()?;
    let mask_points = EXTENT.get().expect("extent set").mask;
    let m = Grid::new(mask_points, half(), 0.0);
    let (mut grass, mut earth, mut gravel, mut stones) = (m.like(0.0), m.like(0.0), m.like(0.0), m.like(0.0));
    let grassy = |t: &Tags| {
        matches!(tag(t, "landuse"), Some("grass" | "meadow" | "village_green" | "flowerbed" | "recreation_ground"))
            || matches!(tag(t, "leisure"), Some("park" | "garden" | "playground"))
            || matches!(tag(t, "natural"), Some("grassland" | "heath"))
            || (tag(t, "leisure") == Some("pitch") && tag(t, "surface").map_or(true, |s| s == "grass"))
    };
    for s in &area_shapes(&osm, grassy) {
        grass.fill_rings(s, 1.0);
    }
    for s in &area_shapes(&osm, |t| matches!(tag(t, "landuse"), Some("forest" | "construction" | "brownfield")) || matches!(tag(t, "natural"), Some("wood" | "scrub"))) {
        earth.fill_rings(s, 1.0);
    }
    for s in &area_shapes(&osm, |t| matches!(tag(t, "surface"), Some("gravel" | "fine_gravel" | "compacted" | "pebblestone")) || tag(t, "landuse") == Some("railway") || (tag(t, "leisure") == Some("pitch") && tag(t, "surface").is_some_and(|s| s != "grass"))) {
        gravel.fill_rings(s, 1.0);
    }
    for s in &st.unpaved {
        gravel.fill_rings(s, 1.0);
    }
    let mut mwater = m.like(0.0);
    for s in &st.water {
        mwater.fill_rings(s, 1.0);
    }
    let mut mland = mwater.clone();
    for v in mland.v.iter_mut() {
        *v = 1.0 - *v;
    }
    let to_water = Grid::distance_to(&mwater);
    // Unmapped ground: green in the orthophoto is grass, the rest hard standing; vegetation the
    // survey saw over it adds leaf litter.
    // Each point reads the window whose photo covers it (the nearest one at the outer edge).
    let ortho_at = |p: Pt| -> [f32; 3] {
        let outside = |o: &Ortho| (o.x0 - p[0]).max(p[0] - o.x0 - o.side).max(o.y0 - p[1]).max(p[1] - o.y0 - o.side);
        let o = orthos.iter().min_by(|a, b| outside(a).partial_cmp(&outside(b)).unwrap()).expect("an orthophoto");
        let u = ((p[0] - o.x0) / o.side * o.width as f64).clamp(0.0, o.width as f64 - 1.0) as usize;
        let v = ((p[1] - o.y0) / o.side * o.height as f64).clamp(0.0, o.height as f64 - 1.0) as usize;
        let i = (v * o.width + u) * 3;
        [o.pixels[i] as f32, o.pixels[i + 1] as f32, o.pixels[i + 2] as f32]
    };
    let veg = lidar.vegetation.blur(1.5, None);
    for r in 0..m.n {
        for c in 0..m.n {
            let i = r * m.n + c;
            let p = m.pos(r, c);
            let d_water = to_water.v[i];
            if mwater.v[i] > 0.5 || d_water < 2.5 {
                stones.v[i] = 1.0;
                continue;
            }
            if grass.v[i] + earth.v[i] + gravel.v[i] > 0.0 {
                continue;
            }
            // Unmapped banks steeper than about 20 degrees are grassed, not hard standing.
            let (gx, gy) = (ground.sample([p[0] + 0.5, p[1]]) - ground.sample([p[0] - 0.5, p[1]]), ground.sample([p[0], p[1] + 0.5]) - ground.sample([p[0], p[1] - 0.5]));
            if (gx * gx + gy * gy).sqrt() > 0.36 {
                grass.v[i] = 1.0;
                continue;
            }
            let [rr, gg, bb] = ortho_at(p);
            let sum = (rr + gg + bb).max(1.0);
            let excess_green = (2.0 * gg - rr - bb) / sum;
            if excess_green > 0.05 {
                grass.v[i] = 1.0;
            } else if veg.sample(p) > 2.0 {
                earth.v[i] = 0.6;
            }
        }
    }
    let _ = mland;
    let (grass, earth, gravel, stones) = (grass.blur(0.35, None), earth.blur(0.5, None), gravel.blur(0.3, None), stones.blur(0.6, None));
    let mut rgba = Vec::with_capacity(mask_points * mask_points * 4);
    for i in 0..m.v.len() {
        let w = [grass.v[i], earth.v[i], gravel.v[i], stones.v[i]].map(|x| x.clamp(0.0, 1.0));
        let total: f32 = w.iter().sum();
        let scale = if total > 1.0 { 1.0 / total } else { 1.0 };
        for x in w {
            rgba.push((x * scale * 255.0).round() as u8);
        }
    }
    {
        let file = std::fs::File::create(args.out.join("surface.png")).map_err(|e| e.to_string())?;
        let mut enc = png::Encoder::new(std::io::BufWriter::new(file), mask_points as u32, mask_points as u32);
        enc.set_color(png::ColorType::Rgba);
        enc.set_depth(png::BitDepth::Eight);
        let mut w = enc.write_header().map_err(|e| e.to_string())?;
        w.write_image_data(&rgba).map_err(|e| e.to_string())?;
    }

    // ---- output ----------------------------------------------------------------------------------
    write_f32(&args.out.join("heights.bin"), &landscape).map_err(|e| e.to_string())?;
    write_f32(&args.out.join("top.bin"), &top).map_err(|e| e.to_string())?;
    mesh.write(&args.out.join("paved.bin")).map_err(|e| e.to_string())?;
    let by_surface = mesh.count_by_surface();
    let (lmin, lmax) = landscape.v.iter().fold((f32::MAX, f32::MIN), |a, &v| (a.0.min(v), a.1.max(v)));
    let points = EXTENT.get().expect("extent set").points;
    let file_name = |p: &PathBuf| p.file_name().map(|f| f.to_string_lossy().to_string());
    let mut sources: Vec<serde_json::Value> = args.laz.iter().map(|p| serde_json::json!({ "role": "bare ground", "name": format!("ARSO LiDAR 2014, GKOT tile {} (ground class)", p.file_stem().map(|f| f.to_string_lossy().to_string()).unwrap_or_default()), "licence": "ARSO/GURS open data, attribution", "file": file_name(p), "sha256": sha256_file(p) })).collect();
    sources.push(serde_json::json!({ "role": "streets and land use", "name": format!("OpenStreetMap via Overpass, {}", osm.timestamp), "licence": "ODbL 1.0, © OpenStreetMap contributors", "sha256": sha256_file(&args.osm) }));
    sources.push(serde_json::json!({ "role": "building footprints", "name": "magna-raedarum-marburgum <window>_buildings.json, joined (lent-ground merge-buildings)", "file": file_name(&args.buildings), "sha256": sha256_file(&args.buildings) }));
    for p in &args.ortho {
        sources.push(serde_json::json!({ "role": "grass versus hard ground only", "name": "GURS DOF025 orthophoto (2020–2022)", "licence": "CC BY 4.0, Geodetska uprava Republike Slovenije", "file": file_name(p), "sha256": sha256_file(p) }));
    }
    let report = serde_json::json!({
        "format": "marburgumii.lent_ground",
        "version": 2,
        "plan": "metres; x east and y south of the middle of Stari most (D96/TM E 549527.8, N 157564.0); z above the Drava (252.98 m)",
        "landscape": { "points": points, "half_extent_m": half(), "row0": "north", "col0": "west", "min_m": lmin, "max_m": lmax, "under_paving_m": UNDER_PAVING },
        "surface_mask": { "points": mask_points, "channels": ["grass", "earth", "gravel", "river_stones"], "remainder": "hard standing" },
        "paved": { "chunk_m": mesh.chunk, "chunks": mesh.chunks.len(), "triangles": mesh.triangle_count(), "surfaces": mesh::SURFACE_NAMES, "triangles_by_surface": by_surface },
        "streets": {
            "carriageway_m2": shapes::area(&st.carriageway).round(),
            "pavement_m2": shapes::area(&st.pavement).round(),
            "crossings": st.crossings.len(),
            "marked_crossings": st.crossings.iter().filter(|c| c.1).count(),
            "kerb_m": KERB, "dropped_kerb_m": KERB_DROPPED,
        },
        "survey": { "filled_under_bridges": filled_under_bridges, "blob_points_removed": blobs, "hollows_over_3m_deep_and_100m2": hollows, "points_in_window": lidar.points_in_window, "ground_points": lidar.ground_points, "measured_grid_points": measured, "grid_points": points * points },
        "paving_edges": { "landscape_points_lowered": edge_lowered, "points_next_to_paving_lowered": next_to_paving, "points_lowered_under_dips": under_dips },
        "buildings": { "seated": buildings.len(), "paving_points_lifted_to_walls": wall_lift, "landscape_points_raised": raised, "paved_points_below_wall_base": wall_gaps },
        "sources": sources,
    });
    std::fs::write(args.out.join("ground.json"), serde_json::to_string_pretty(&report).unwrap() + "\n").map_err(|e| e.to_string())?;
    write_walk_graph(args, &osm, &st, &footprints, &top)?;
    eprintln!("done in {:.1}s: {}", t0.elapsed().as_secs_f64(), serde_json::to_string(&report["paved"]).unwrap());
    eprintln!("buildings: {}", report["buildings"]);
    Ok(())
}
