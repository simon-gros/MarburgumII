//! The trees and bushes of the map (GFX-01, VQ-022): the original game's surveyed plants, kept only where they stand
//! on grass, in one file the server and the client read at run time (`MarburgumVegetation.h`).
//!
//! The original's `tools/build_plants.py` found every tree and bush of a window in the ARSO 2014 survey, gave each a
//! species from its ecological region (`vegetation.json`) and fitted its crown to the walls round it, into
//! `<window>_plants.json`. It stood a trunk on whatever the survey put under the crown: a pavement, a car park, a yard,
//! a square. The owner's rule for this map is stricter: **a plant grows only on grass**, ground that is not paved,
//! built on, water or a path. So a plant is kept only when
//!
//! - the landscape is drawn as grass at its trunk and nearly all round it (`surface.png`, grass the dominant weight);
//! - no paving is drawn within reach of its trunk (`top.bin` stands over `heights.bin` wherever a paved mesh is);
//! - it is clear of the buildings, the river, the bridges and the street people's walk graph (ADR-044), a tree by
//!   more than a bush, since a tree's trunk blocks and a bush's leaves only hide;
//! - it is inside the square.
//!
//! A plant that fails is dropped, never moved: the original already settled each trunk and pruned each crown to its
//! site, and the owner asked for fewer plants rather than displaced ones. Every refusal is counted by reason.
//!
//! The rows carry what the game draws: plan position, ground height (`heights.bin`, the landscape, which is the
//! ground wherever there is grass), species, yaw and scale. Yaw, the ±6 % height jitter and the scale follow the
//! original's Unreal import (`MarburgumImportVegetationPlacementsCommandlet.cpp`): height over the species' natural
//! height, crown radius over its natural radius. Positions are the 2014 survey's, not the 1990s' (the original's
//! roadmap, section 2); species are inferred by region, not measured.

use crate::grid::Grid;
use crate::kinds;
use crate::lamps::{walk_segments, Segments};
use crate::osm::Pt;
use crate::sha256_file;
use crate::shapes::{self, Index, Shapes};
use serde_json::{json, Value};
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// The nine windows of ADR-045, whose `<window>_plants.json` cover the square.
pub const WINDOWS: [&str; 9] = ["lent", "ljudski_vrt", "center", "avtobusna", "pristan", "titov_most", "kurilnica", "magdalena", "pobreska"];

/// Paving is drawn where the walkable top stands this far over the landscape (the landscape goes 0.15 m under it).
const PAVED_RISE_M: f32 = 0.1;
/// How far round a trunk the ground must be grass and unpaved: a tree's roots and a bush's spread.
const TREE_GROUND_M: f64 = 1.0;
const BUSH_GROUND_M: f64 = 0.5;
/// Of the nine points tested round a trunk (the trunk and eight round it), at least this many must be grass.
const GRASS_POINTS: usize = 8;
/// A trunk clear of a building by this much; a bush by this.
const TREE_BUILDING_CLEAR_M: f64 = 1.0;
const BUSH_BUILDING_CLEAR_M: f64 = 0.3;
/// A trunk clear of the walk graph's lines by this much (a walker is 0.35 m from their line to their side, and a trunk
/// blocks); a bush by this share of its crown radius, within these bounds, so nobody walks through its leaves.
const TREE_WALK_CLEAR_M: f64 = 1.0;
const BUSH_WALK_SHARE: f64 = 0.6;
const BUSH_WALK_CLEAR_M: [f64; 2] = [0.6, 3.0];
/// Inside the square by this much, as the lamps are.
const EDGE_M: f64 = 1.5;
/// The original's import: height jitter, and how a plant's yaw and jitter are drawn from its position.
const HEIGHT_JITTER: f64 = 0.06;

/// The original's `Scatter`, so a plant turns and grows exactly as it did there.
fn scatter(x: f64, z: f64, salt: f64) -> f64 {
    let v = (x * 12.9898 + z * 78.233 + salt * 37.719).sin() * 43758.5453;
    v - v.floor()
}

/// One species as the game draws it.
#[derive(Clone)]
struct Species {
    id: String,
    tree: bool,
    natural_height: f64,
    natural_radius: f64,
    trunk_radius: f64,
    trunk_height: f64,
}

/// What the ground tool knows of the square.
pub struct Inputs<'a> {
    pub footprints: &'a Shapes,
    pub water: &'a Shapes,
    pub bridges: &'a Shapes,
    pub heights: &'a Grid,
    pub top: &'a Grid,
    pub half: f64,
}

/// The landscape's surface weights (`surface.png`), as the material samples them.
pub struct Surface {
    size: usize,
    half: f64,
    rgba: Vec<u8>,
}

impl Surface {
    pub fn read(path: &Path, half: f64) -> Result<Surface, String> {
        let file = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut decoder = png::Decoder::new(std::io::BufReader::new(file));
        decoder.set_transformations(png::Transformations::EXPAND);
        let mut reader = decoder.read_info().map_err(|e| e.to_string())?;
        let mut rgba = vec![0u8; reader.output_buffer_size().ok_or("surface mask too large")?];
        let info = reader.next_frame(&mut rgba).map_err(|e| e.to_string())?;
        if info.color_type != png::ColorType::Rgba || info.bit_depth != png::BitDepth::Eight || info.width != info.height {
            return Err("surface mask must be square 8-bit RGBA".into());
        }
        rgba.truncate(info.buffer_size());
        Ok(Surface { size: info.width as usize, half, rgba })
    }

    /// Whether the landscape is drawn as grass at p: grass the largest weight, hard standing being what the four leave.
    fn grass(&self, p: Pt) -> bool {
        let u = ((p[0] + self.half) / (2.0 * self.half) * self.size as f64).floor();
        let v = ((p[1] + self.half) / (2.0 * self.half) * self.size as f64).floor();
        if u < 0.0 || v < 0.0 || u >= self.size as f64 || v >= self.size as f64 {
            return false;
        }
        let at = (v as usize * self.size + u as usize) * 4;
        let w = [0, 1, 2, 3].map(|c| self.rgba[at + c] as f64 / 255.0);
        kinds::dominant(w) == 1
    }
}

struct Ground<'a> {
    inp: &'a Inputs<'a>,
    surface: &'a Surface,
    tree_blocked: Index,
    bush_blocked: Index,
    water: Index,
    bridges: Index,
    walk: &'a Segments,
}

impl Ground<'_> {
    /// Why a plant at p of crown radius r cannot stand, or None when it can.
    fn refuse(&self, p: Pt, tree: bool, radius: f64) -> Option<&'static str> {
        let inside = self.inp.half - EDGE_M;
        if p[0].abs() > inside || p[1].abs() > inside {
            return Some("outside");
        }
        if self.water.contains(p) {
            return Some("water");
        }
        if self.bridges.contains(p) {
            return Some("bridge");
        }
        if (if tree { &self.tree_blocked } else { &self.bush_blocked }).contains(p) {
            return Some("building");
        }
        let reach = if tree { TREE_GROUND_M } else { BUSH_GROUND_M };
        let ring: Vec<Pt> = std::iter::once(p)
            .chain((0..8).map(|k| {
                let a = k as f64 * std::f64::consts::FRAC_PI_4;
                [p[0] + reach * a.cos(), p[1] + reach * a.sin()]
            }))
            .collect();
        if ring.iter().any(|&q| self.inp.top.sample(q) - self.inp.heights.sample(q) > PAVED_RISE_M) {
            return Some("paved");
        }
        if !self.surface.grass(p) || ring.iter().filter(|&&q| self.surface.grass(q)).count() < GRASS_POINTS {
            return Some("not_grass");
        }
        let clear = if tree { TREE_WALK_CLEAR_M } else { (BUSH_WALK_SHARE * radius).clamp(BUSH_WALK_CLEAR_M[0], BUSH_WALK_CLEAR_M[1]) };
        if self.walk.nearest(p, clear).is_some() {
            return Some("walkway");
        }
        None
    }
}

/// The species the original's catalogue describes, by id.
fn read_catalogue(path: &Path) -> Result<HashMap<String, Value>, String> {
    let root: Value = serde_json::from_str(&std::fs::read_to_string(path).map_err(|e| format!("{}: {e}", path.display()))?).map_err(|e| format!("{}: {e}", path.display()))?;
    let list = root["species"].as_array().ok_or(format!("{}: no species", path.display()))?;
    Ok(list.iter().filter_map(|s| Some((s["id"].as_str()?.to_string(), s.clone()))).collect())
}

/// A placed plant: plan position, ground, species index into the output's list, yaw degrees, crown and height scale.
struct Plant {
    at: Pt,
    z: f64,
    kind: usize,
    yaw: f64,
    spread: f64,
    grow: f64,
}

fn round(v: f64, places: i32) -> f64 {
    let f = 10f64.powi(places);
    (v * f).round() / f
}

/// Places the plants of the nine windows and writes them to `out`; returns the summary.
pub fn build(inp: &Inputs, surface_path: &Path, terrain: &Path, catalogue_path: &Path, walk_graph: &Path, out: &Path) -> Result<Value, String> {
    let surface = Surface::read(surface_path, inp.half)?;
    let catalogue = read_catalogue(catalogue_path)?;
    let (walk, _) = walk_segments(walk_graph)?;
    let ground = Ground {
        inp,
        surface: &surface,
        tree_blocked: Index::new(&shapes::offset(inp.footprints, TREE_BUILDING_CLEAR_M)),
        bush_blocked: Index::new(&shapes::offset(inp.footprints, BUSH_BUILDING_CLEAR_M)),
        water: Index::new(inp.water),
        bridges: Index::new(inp.bridges),
        walk: &walk,
    };

    let mut species: Vec<Species> = Vec::new();
    let mut plants: Vec<Plant> = Vec::new();
    let mut refused: HashMap<&'static str, [usize; 2]> = HashMap::new();
    let mut read = [0usize; 2];
    let mut duplicates = 0usize;
    let mut seen: std::collections::HashSet<(i64, i64)> = std::collections::HashSet::new();
    let mut sources = Vec::new();
    for window in WINDOWS {
        let path: PathBuf = terrain.join(format!("{window}_plants.json"));
        let root: Value = serde_json::from_str(&std::fs::read_to_string(&path).map_err(|e| format!("{}: {e}", path.display()))?).map_err(|e| format!("{}: {e}", path.display()))?;
        let names: Vec<String> = root["species"].as_array().ok_or(format!("{}: no species", path.display()))?.iter().map(|s| s.as_str().unwrap_or_default().to_string()).collect();
        let heights: Vec<f64> = root["natural_heights"].as_array().ok_or(format!("{}: no natural_heights", path.display()))?.iter().map(|v| v.as_f64().unwrap_or(0.0)).collect();
        let radii: Vec<f64> = root["natural_radii"].as_array().ok_or(format!("{}: no natural_radii", path.display()))?.iter().map(|v| v.as_f64().unwrap_or(0.0)).collect();
        if names.len() != heights.len() || names.len() != radii.len() {
            return Err(format!("{}: species, natural_heights and natural_radii differ in length", path.display()));
        }
        // This window's palette, as indices into ours.
        let mut local = Vec::new();
        for (i, name) in names.iter().enumerate() {
            let index = match species.iter().position(|s| &s.id == name) {
                Some(index) => index,
                None => {
                    let entry = catalogue.get(name).ok_or(format!("{}: species {name} is not in the catalogue", path.display()))?;
                    let form = &entry["form"];
                    species.push(Species {
                        id: name.clone(),
                        tree: entry["category"].as_str() == Some("tree"),
                        natural_height: heights[i],
                        natural_radius: radii[i],
                        trunk_radius: form["trunk_radius"].as_f64().unwrap_or(0.0),
                        trunk_height: form["trunk_height"].as_f64().unwrap_or(0.0),
                    });
                    species.len() - 1
                }
            };
            if (species[index].natural_height - heights[i]).abs() > 1e-6 || (species[index].natural_radius - radii[i]).abs() > 1e-6 {
                return Err(format!("{}: {name}'s natural size differs from another window's", path.display()));
            }
            local.push(index);
        }
        for (list, tree) in [("trees", true), ("bushes", false)] {
            for row in root[list].as_array().map(Vec::as_slice).unwrap_or(&[]) {
                let (Some(x), Some(y), Some(height), Some(radius), Some(kind)) = (row[0].as_f64(), row[1].as_f64(), row[3].as_f64(), row[4].as_f64(), row[5].as_u64()) else {
                    return Err(format!("{}: a {list} row is not [x, z, ground, height, radius, species, region]", path.display()));
                };
                let kind = *local.get(kind as usize).ok_or(format!("{}: species index {kind} outside the palette", path.display()))?;
                // The original draws a plant on a window edge once in each window that lists it.
                if !seen.insert(((x * 100.0).round() as i64, (y * 100.0).round() as i64)) {
                    duplicates += 1;
                    continue;
                }
                if x.abs() > inp.half || y.abs() > inp.half {
                    continue;
                }
                let s = &species[kind];
                let is_tree = tree && s.tree;
                read[!is_tree as usize] += 1;
                let at = [x, y];
                if let Some(why) = ground.refuse(at, is_tree, radius) {
                    refused.entry(why).or_default()[!is_tree as usize] += 1;
                    continue;
                }
                let jitter = 1.0 + (scatter(x, y, 3.0) - 0.5) * 2.0 * HEIGHT_JITTER;
                let grow = (height * jitter / s.natural_height.max(0.1)).max(0.1);
                let spread = if radius > 0.01 && s.natural_radius > 0.01 { radius / s.natural_radius } else { grow };
                plants.push(Plant { at, z: inp.heights.sample(at) as f64, kind, yaw: scatter(x, y, 1.0) * 360.0, spread, grow });
            }
        }
        sources.push(json!({ "role": format!("{window} plants"), "name": format!("magna-raedarum-marburgum project/assets/terrain/{window}_plants.json (ARSO 2014 LiDAR, species by region)"), "sha256": sha256_file(&path) }));
    }
    plants.sort_by(|a, b| (a.kind, a.at[1], a.at[0]).partial_cmp(&(b.kind, b.at[1], b.at[0])).unwrap());

    let mut by_species = serde_json::Map::new();
    for (i, s) in species.iter().enumerate() {
        by_species.insert(s.id.clone(), json!(plants.iter().filter(|p| p.kind == i).count()));
    }
    let kept = [plants.iter().filter(|p| species[p.kind].tree).count(), plants.iter().filter(|p| !species[p.kind].tree).count()];
    let mut reasons: Vec<_> = refused.iter().collect();
    reasons.sort();
    let summary = json!({
        "plants": plants.len(),
        "trees": kept[0],
        "bushes": kept[1],
        "read": { "trees": read[0], "bushes": read[1] },
        "window_edge_duplicates": duplicates,
        "refused": reasons.into_iter().map(|(k, v)| (k.to_string(), json!({ "trees": v[0], "bushes": v[1] }))).collect::<serde_json::Map<_, _>>(),
        "by_species": by_species,
    });
    sources.push(json!({ "role": "species forms", "name": "magna-raedarum-marburgum project/data/vegetation.json", "sha256": sha256_file(catalogue_path) }));
    sources.push(json!({ "role": "where the landscape is grass", "name": "surface.png (this tool)", "sha256": sha256_file(surface_path) }));
    sources.push(json!({ "role": "where people walk", "name": "lent_walk_graph.json (lent-ground, ADR-044)", "sha256": sha256_file(walk_graph) }));
    let doc = json!({
        "format": "marburgumii.vegetation",
        "version": 1,
        "plan": "metres; x east and y south of the middle of Stari most; z the landscape above the Drava (heights.bin), which the game grounds; yaw degrees about the vertical; spread scales the crown across, grow the height, both of the species mesh at its natural size",
        "period": "Positions come from the ARSO 2014 survey, not the 1990s; species are assigned by ecological region, not measured.",
        "species": species.iter().map(|s| json!({
            "id": s.id, "tree": s.tree, "natural_height_m": s.natural_height, "natural_radius_m": s.natural_radius,
            "trunk_radius_m": s.trunk_radius, "trunk_height_m": s.trunk_height,
        })).collect::<Vec<_>>(),
        "rules": {
            "only_on_grass": true, "paved_rise_m": PAVED_RISE_M, "tree_ground_m": TREE_GROUND_M, "bush_ground_m": BUSH_GROUND_M, "grass_points_of_9": GRASS_POINTS,
            "tree_building_clear_m": TREE_BUILDING_CLEAR_M, "bush_building_clear_m": BUSH_BUILDING_CLEAR_M,
            "tree_walk_clear_m": TREE_WALK_CLEAR_M, "bush_walk_share_of_radius": BUSH_WALK_SHARE, "bush_walk_clear_m": BUSH_WALK_CLEAR_M,
            "height_jitter": HEIGHT_JITTER,
        },
        "sources_used": sources,
        "summary": summary,
        "fields": ["x", "y", "z", "species", "yaw", "spread", "grow"],
        "plants": plants.iter().map(|p| json!([round(p.at[0], 2), round(p.at[1], 2), round(p.z, 2), p.kind, round(p.yaw, 1), round(p.spread, 3), round(p.grow, 3)])).collect::<Vec<_>>(),
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
    fn scatter_matches_the_original() {
        // The original's Scatter(0, 0, 1): frac(sin(37.719) * 43758.5453).
        let expected = {
            let v = (37.719f64).sin() * 43758.5453;
            v - v.floor()
        };
        assert!((scatter(0.0, 0.0, 1.0) - expected).abs() < 1e-12);
        assert!((0.0..1.0).contains(&scatter(-123.25, 456.75, 3.0)));
    }

    /// An 8 m square of ground: grass in the west half, hard standing in the east, paving along the south edge, a
    /// building in the north-west corner and a walk line across the south-west.
    fn fixture() -> (Grid, Grid, Surface) {
        let heights = Grid::new(81, 4.0, 0.0);
        let mut top = heights.clone();
        for r in 0..81 {
            for c in 0..81 {
                if heights.pos(r, c)[1] > 2.0 {
                    top.set(r, c, 0.15);
                }
            }
        }
        let size = 80;
        let mut rgba = vec![0u8; size * size * 4];
        for v in 0..size {
            for u in 0..size / 2 {
                rgba[(v * size + u) * 4] = 255;
            }
        }
        (heights, top, Surface { size, half: 4.0, rgba })
    }

    #[test]
    fn only_on_grass_away_from_paving_buildings_and_people() {
        let (heights, top, surface) = fixture();
        let empty: Shapes = Vec::new();
        let house = shapes::polygon(&[vec![[-2.5, -2.5], [-1.5, -2.5], [-1.5, -1.5], [-2.5, -1.5]]]);
        let inp = Inputs { footprints: &house, water: &empty, bridges: &empty, heights: &heights, top: &top, half: 4.0 };
        let mut walk = Segments::new(4.0);
        walk.add_line(&[[-4.0, 1.0], [0.0, 1.0]]);
        let ground = Ground {
            inp: &inp,
            surface: &surface,
            tree_blocked: Index::new(&shapes::offset(&house, TREE_BUILDING_CLEAR_M)),
            bush_blocked: Index::new(&shapes::offset(&house, BUSH_BUILDING_CLEAR_M)),
            water: Index::new(&empty),
            bridges: Index::new(&empty),
            walk: &walk,
        };
        assert_eq!(ground.refuse([-1.0, -0.4], false, 0.5), None);
        assert_eq!(ground.refuse([-1.2, -0.4], true, 3.0), None);
        assert_eq!(ground.refuse([1.0, -0.4], false, 0.5), Some("not_grass"));
        assert_eq!(ground.refuse([-0.3, -0.4], true, 3.0), Some("not_grass"), "a tree's roots reach the hard standing");
        assert_eq!(ground.refuse([-1.0, 1.7], false, 0.5), Some("paved"));
        assert_eq!(ground.refuse([-1.0, 1.5], false, 1.0), Some("walkway"));
        assert_eq!(ground.refuse([-1.0, 0.2], true, 3.0), Some("walkway"), "a trunk keeps a metre off the walk line");
        assert_eq!(ground.refuse([-1.3, -1.3], false, 0.5), Some("building"));
        assert_eq!(ground.refuse([-0.8, -1.2], false, 0.5), None, "a bush may stand close to a wall");
        assert_eq!(ground.refuse([-0.8, -1.2], true, 3.0), Some("building"));
        assert_eq!(ground.refuse([-3.0, 0.0], false, 0.5), Some("outside"));
    }
}
