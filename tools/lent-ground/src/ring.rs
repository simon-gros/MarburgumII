//! The distant ring: the 2014 survey round the playable square, out to the edge of its nine
//! tiles, as a low-detail mesh, so the map ends in the rest of the city instead of a void.
//!
//! The ring is 4 m cells. Bare ground and tree canopies are one smooth surface; a building is
//! its cells raised as flat blocks to the median of their roof returns, with walls where it
//! stands above its neighbours; the Drava runs on as a water sheet over a bed. Its inner edge
//! takes the landscape's own heights, so the two meet without a step. Nothing in it is playable
//! and it has no collision.
//!
//! `ring.bin` is the paved mesh format (mesh.rs) with RING_SLOTS as its material slots.

use crate::grid::Grid;
use crate::mesh::PavedMesh;
use crate::proj;
use std::path::PathBuf;

/// Half the side of the ring (plan metres): the nine 1 km tiles reach at least this far from
/// the middle of Stari most on every side.
pub const RING_HALF: f64 = 1432.0;
pub const CELL: f64 = 4.0;
pub const RING_CHUNK: f64 = 256.0;
/// Material slots, in the order MarburgumRebuildGround gives the ring's meshes their materials.
pub const RING_SLOTS: [&str; 8] = ["ground", "trees", "roof_tile", "roof_flat", "wall_light", "wall_warm", "wall_grey", "water"];
const GROUND: u8 = 0;
const TREES: u8 = 1;
const ROOF_TILE: u8 = 2;
const ROOF_FLAT: u8 = 3;
const WALLS: u8 = 4;
const WATER: u8 = 7;
/// The Drava sheet's height in the square (ADR-045), which the ring's water eases away from.
const SQUARE_WATER: f64 = 0.06;
/// A cell is a building with at least this many roof returns this far above the ground, a tree
/// with canopy returns this far above it.
const BUILDING_RETURNS: usize = 3;
const BUILDING_RISE: f32 = 2.0;
const CANOPY_RISE: f32 = 2.0;
/// The river: cells whose ground returns spread less than RIVER_FLAT (interquartile, metres),
/// below RIVER_BELOW, in connected sheets of at least RIVER_AREA square metres.
const RIVER_FLAT: f32 = 0.08;
const RIVER_BELOW: f32 = 2.0;
const RIVER_AREA: f64 = 20_000.0;
/// Dry patches inside the river up to this many square metres are part of it.
const RIVER_HOLE: f64 = 1_500.0;

struct Cells {
    m: usize,
    ground: Vec<Vec<f32>>,
    roof: Vec<Vec<f32>>,
    water: Vec<Vec<f32>>,
    canopy: Vec<f32>,
    returns: Vec<u32>,
}

fn median(v: &mut [f32]) -> Option<f32> {
    if v.is_empty() {
        return None;
    }
    v.sort_unstable_by(|a, b| a.partial_cmp(b).unwrap());
    Some(v[v.len() / 2])
}

fn read(laz: &[PathBuf]) -> Result<Cells, String> {
    let m = (2.0 * RING_HALF / CELL).round() as usize;
    let mut cells = Cells { m, ground: vec![Vec::new(); m * m], roof: vec![Vec::new(); m * m], water: vec![Vec::new(); m * m], canopy: vec![f32::NEG_INFINITY; m * m], returns: vec![0; m * m] };
    for path in laz {
        let mut reader = las::Reader::from_path(path).map_err(|e| format!("{}: {e}", path.display()))?;
        for p in reader.points() {
            let p = p.map_err(|e| e.to_string())?;
            let (x, y) = proj::d96_to_plan(p.x, p.y);
            let (c, r) = (((x + RING_HALF) / CELL).floor(), ((y + RING_HALF) / CELL).floor());
            if r < 0.0 || c < 0.0 || r >= m as f64 || c >= m as f64 {
                continue;
            }
            let i = r as usize * m + c as usize;
            let z = (p.z - proj::HEIGHT_DATUM) as f32;
            match u8::from(p.classification) {
                2 => cells.ground[i].push(z),
                3..=5 => cells.canopy[i] = cells.canopy[i].max(z),
                6 => cells.roof[i].push(z),
                9 => cells.water[i].push(z),
                7 | 18 => continue,
                _ => {}
            }
            cells.returns[i] += 1;
        }
        eprintln!("ring: {} read", path.display());
    }
    Ok(cells)
}

#[derive(Clone, Copy, PartialEq)]
enum Kind {
    Inner,
    Ground,
    Tree,
    Building,
    Water,
}

/// Builds the ring from the survey tiles; `inner` is the landscape of the square (heights.bin,
/// `inner_half` metres either side). Writes ring.bin and returns a summary.
pub fn build(laz: &[PathBuf], inner: &Grid, inner_half: f64, out: &std::path::Path) -> Result<serde_json::Value, String> {
    let mut cells = read(laz)?;
    let m = cells.m;
    let centre = |k: usize| -RING_HALF + CELL * (k as f64 + 0.5);
    let in_square = |r: usize, c: usize| centre(c).abs() < inner_half && centre(r).abs() < inner_half;
    let template = Grid::new(m, RING_HALF - CELL / 2.0, 0.0);

    // Bare ground, filled where the survey saw none (water, under buildings).
    let mut ground = template.like(0.0);
    let mut known = template.like(0.0);
    let mut water_level = template.like(0.0);
    let mut water_known = template.like(0.0);
    let mut kind = vec![Kind::Ground; m * m];
    // The 2014 survey classes the Drava's surface as ground: a cell of it is dead flat, low, and
    // level with the cells round it.
    let mut flat = vec![false; m * m];
    for i in 0..m * m {
        let g = &mut cells.ground[i];
        if let Some(z) = median(g) {
            ground.v[i] = z;
            known.v[i] = 1.0;
            flat[i] = g.len() >= 3 && g[g.len() * 3 / 4] - g[g.len() / 4] < RIVER_FLAT && z < RIVER_BELOW;
        }
        if let Some(z) = median(&mut cells.water[i]) {
            water_level.v[i] = z;
            water_known.v[i] = 1.0;
        }
    }
    ground.inpaint(&known);
    let mut wet = vec![false; m * m];
    for r in 1..m - 1 {
        for c in 1..m - 1 {
            let i = r * m + c;
            wet[i] = water_known.v[i] > 0.5
                || (flat[i] && [i - 1, i + 1, i - m, i + m].iter().all(|&j| known.v[j] < 0.5 || (ground.v[j] - ground.v[i]).abs() < RIVER_FLAT * 2.0));
        }
    }
    // Only large connected sheets are the river (with the cells beside them that returned
    // nothing at all); a flat yard or a road is not.
    let mut seen = vec![false; m * m];
    let mut river = vec![false; m * m];
    for s in 0..m * m {
        if !wet[s] || seen[s] {
            continue;
        }
        let (mut stack, mut members) = (vec![s], Vec::new());
        seen[s] = true;
        while let Some(i) = stack.pop() {
            members.push(i);
            let (r, c) = (i / m, i % m);
            if r == 0 || c == 0 || r == m - 1 || c == m - 1 {
                continue;
            }
            for j in [i - 1, i + 1, i - m, i + m] {
                if !seen[j] && (wet[j] || cells.returns[j] == 0) {
                    seen[j] = true;
                    stack.push(j);
                }
            }
        }
        if members.len() as f64 * CELL * CELL >= RIVER_AREA {
            for &i in &members {
                river[i] = true;
                if known.v[i] > 0.5 {
                    water_level.v[i] = ground.v[i];
                    water_known.v[i] = 1.0;
                }
            }
        }
    }
    // Boats, buoys and gravel bars leave small dry holes in the sheet: closed in.
    let mut seen = river.clone();
    for s in 0..m * m {
        if seen[s] {
            continue;
        }
        let (mut stack, mut members, mut open) = (vec![s], Vec::new(), false);
        seen[s] = true;
        while let Some(i) = stack.pop() {
            members.push(i);
            let (r, c) = (i / m, i % m);
            if r == 0 || c == 0 || r == m - 1 || c == m - 1 || in_square(r, c) {
                open = true;
                continue;
            }
            for j in [i - 1, i + 1, i - m, i + m] {
                if !seen[j] {
                    seen[j] = true;
                    stack.push(j);
                }
            }
        }
        if !open && members.len() as f64 * CELL * CELL <= RIVER_HOLE {
            for &i in &members {
                river[i] = true;
            }
        }
    }
    let any_water = water_known.v.iter().any(|&k| k > 0.5);
    if any_water {
        water_level = water_level.blur(12.0, Some(&water_known));
        water_level.inpaint(&water_known);
    }
    for r in 0..m {
        for c in 0..m {
            let i = r * m + c;
            if in_square(r, c) {
                kind[i] = Kind::Inner;
                continue;
            }
            if river[i] {
                kind[i] = Kind::Water;
            } else if cells.roof[i].len() >= BUILDING_RETURNS && median(&mut cells.roof[i]).is_some_and(|z| z > ground.v[i] + BUILDING_RISE) {
                kind[i] = Kind::Building;
            } else if cells.canopy[i] > ground.v[i] + CANOPY_RISE {
                kind[i] = Kind::Tree;
            }
        }
    }
    // Lone wet cells in town are a missing return, not the river.
    for r in 1..m - 1 {
        for c in 1..m - 1 {
            let i = r * m + c;
            if kind[i] == Kind::Water && [i - 1, i + 1, i - m, i + m].iter().filter(|&&j| kind[j] == Kind::Water).count() < 2 {
                kind[i] = Kind::Ground;
            }
        }
    }
    let roof: Vec<f32> = (0..m * m).map(|i| if kind[i] == Kind::Building { median(&mut cells.roof[i]).unwrap() } else { f32::NAN }).collect();

    // Buildings: connected blocks, each with one wall colour and tile or flat by how much of its
    // roof slopes.
    let mut block = vec![u32::MAX; m * m];
    let mut blocks: Vec<(u8, u8)> = Vec::new();
    for s in 0..m * m {
        if kind[s] != Kind::Building || block[s] != u32::MAX {
            continue;
        }
        let id = blocks.len() as u32;
        let (mut stack, mut members) = (vec![s], Vec::new());
        block[s] = id;
        while let Some(i) = stack.pop() {
            members.push(i);
            let (r, c) = (i / m, i % m);
            for (rr, cc) in [(r.wrapping_sub(1), c), (r + 1, c), (r, c.wrapping_sub(1)), (r, c + 1)] {
                if rr < m && cc < m {
                    let j = rr * m + cc;
                    if kind[j] == Kind::Building && block[j] == u32::MAX {
                        block[j] = id;
                        stack.push(j);
                    }
                }
            }
        }
        let sloped = members
            .iter()
            .filter(|&&i| {
                let (r, c) = (i / m, i % m);
                [(r.wrapping_sub(1), c), (r + 1, c), (r, c.wrapping_sub(1)), (r, c + 1)]
                    .iter()
                    .any(|&(rr, cc)| rr < m && cc < m && kind[rr * m + cc] == Kind::Building && (roof[rr * m + cc] - roof[i]).abs() > 0.6 && (roof[rr * m + cc] - roof[i]).abs() < 3.0)
            })
            .count();
        let roof_slot = if sloped * 5 >= members.len() * 2 { ROOF_TILE } else { ROOF_FLAT };
        let hash = (s as u32).wrapping_mul(2_654_435_761) >> 16;
        blocks.push((roof_slot, WALLS + (hash % 3) as u8));
    }

    // The smooth surface at the cell corners: bare ground, canopies, the river bed.
    let wl = |i: usize, x: f64, y: f64| -> f64 {
        let level = if any_water { water_level.v[i] as f64 } else { SQUARE_WATER };
        let out = (x.abs().max(y.abs()) - inner_half).max(0.0);
        let t = (out / 150.0).min(1.0);
        let t = t * t * (3.0 - 2.0 * t);
        SQUARE_WATER + (level - SQUARE_WATER) * t
    };
    let surface = |i: usize, r: usize, c: usize| -> f64 {
        match kind[i] {
            Kind::Tree => (ground.v[i] + 0.8 * (cells.canopy[i] - ground.v[i]).min(25.0)) as f64,
            Kind::Water => wl(i, centre(c), centre(r)) - 1.5,
            _ => ground.v[i] as f64,
        }
    };
    let corner = |k: usize| -RING_HALF + CELL * k as f64;
    let mut height = vec![0.0f64; (m + 1) * (m + 1)];
    let mut water_at = vec![f64::NAN; (m + 1) * (m + 1)];
    for vr in 0..=m {
        for vc in 0..=m {
            let (mut sum, mut count, mut on_edge, mut wsum, mut wcount) = (0.0, 0.0, false, 0.0, 0.0);
            for (r, c) in [(vr.wrapping_sub(1), vc.wrapping_sub(1)), (vr.wrapping_sub(1), vc), (vr, vc.wrapping_sub(1)), (vr, vc)] {
                if r >= m || c >= m {
                    continue;
                }
                let i = r * m + c;
                if kind[i] == Kind::Inner {
                    on_edge = true;
                    continue;
                }
                sum += surface(i, r, c);
                count += 1.0;
                if kind[i] == Kind::Water {
                    wsum += wl(i, centre(c), centre(r));
                    wcount += 1.0;
                }
            }
            let (x, y) = (corner(vc), corner(vr));
            let v = vr * (m + 1) + vc;
            height[v] = if on_edge { inner.sample([x, y]) as f64 - 0.02 } else if count > 0.0 { sum / count } else { 0.0 };
            if wcount > 0.0 {
                water_at[v] = if on_edge { SQUARE_WATER } else { wsum / wcount };
            }
        }
    }

    let mut mesh = PavedMesh::new(RING_HALF, RING_CHUNK);
    let up = [0.0, 0.0, 1.0];
    let (mut n_ground, mut n_tree, mut n_building, mut n_water) = (0usize, 0usize, 0usize, 0usize);
    for r in 0..m {
        for c in 0..m {
            let i = r * m + c;
            let (x0, x1, y0, y1) = (corner(c), corner(c + 1), corner(r), corner(r + 1));
            let vid = |vr: usize, vc: usize| vr * (m + 1) + vc;
            let quad = |mesh: &mut PavedMesh, z: [f64; 4], slot: u8| {
                let p = [[x0, y0, z[0]], [x1, y0, z[1]], [x1, y1, z[2]], [x0, y1, z[3]]];
                let uv = p.map(|q| [q[0], q[1]]);
                mesh.add_slot([p[0], p[1], p[2]], [uv[0], uv[1], uv[2]], up, slot);
                mesh.add_slot([p[0], p[2], p[3]], [uv[0], uv[2], uv[3]], up, slot);
            };
            let ground_z = [height[vid(r, c)], height[vid(r, c + 1)], height[vid(r + 1, c + 1)], height[vid(r + 1, c)]];
            match kind[i] {
                Kind::Inner => {}
                Kind::Ground => {
                    quad(&mut mesh, ground_z, GROUND);
                    n_ground += 1;
                }
                Kind::Tree => {
                    quad(&mut mesh, ground_z, TREES);
                    n_tree += 1;
                }
                Kind::Water => {
                    quad(&mut mesh, ground_z, GROUND);
                    let w = [vid(r, c), vid(r, c + 1), vid(r + 1, c + 1), vid(r + 1, c)].map(|v| water_at[v]);
                    quad(&mut mesh, w, WATER);
                    n_water += 1;
                }
                Kind::Building => {
                    let z = roof[i] as f64;
                    let (roof_slot, wall_slot) = blocks[block[i] as usize];
                    quad(&mut mesh, [z; 4], roof_slot);
                    n_building += 1;
                    // Walls on each side the block stands above: down to a lower roof, or into
                    // the ground beside it.
                    let sides = [
                        (r.wrapping_sub(1), c, [x0, y0], [x1, y0], [0.0, -1.0], (vid(r, c), vid(r, c + 1))),
                        (r + 1, c, [x1, y1], [x0, y1], [0.0, 1.0], (vid(r + 1, c + 1), vid(r + 1, c))),
                        (r, c.wrapping_sub(1), [x0, y1], [x0, y0], [-1.0, 0.0], (vid(r + 1, c), vid(r, c))),
                        (r, c + 1, [x1, y0], [x1, y1], [1.0, 0.0], (vid(r, c + 1), vid(r + 1, c + 1))),
                    ];
                    for (rr, cc, a, b, facing, (va, vb)) in sides {
                        let neighbour = if rr < m && cc < m { Some(rr * m + cc) } else { None };
                        let bottom = match neighbour {
                            Some(j) if kind[j] == Kind::Building => roof[j] as f64,
                            _ => height[va].min(height[vb]) - 0.5,
                        };
                        if bottom >= z - 0.3 {
                            continue;
                        }
                        let p = [[a[0], a[1], bottom], [b[0], b[1], bottom], [b[0], b[1], z], [a[0], a[1], z]];
                        let uv = [[0.0, bottom], [CELL, bottom], [CELL, z], [0.0, z]];
                        let toward = [facing[0], facing[1], 0.0];
                        mesh.add_slot([p[0], p[1], p[2]], [uv[0], uv[1], uv[2]], toward, wall_slot);
                        mesh.add_slot([p[0], p[2], p[3]], [uv[0], uv[2], uv[3]], toward, wall_slot);
                    }
                }
            }
        }
    }
    mesh.write(out).map_err(|e| e.to_string())?;
    Ok(serde_json::json!({
        "half_extent_m": RING_HALF, "cell_m": CELL, "chunk_m": RING_CHUNK, "slots": RING_SLOTS,
        "cells": { "ground": n_ground, "trees": n_tree, "buildings": n_building, "water": n_water },
        "blocks": blocks.len(), "water_level_from_survey": any_water,
        "chunks": mesh.chunks.len(), "triangles": mesh.triangle_count(),
    }))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_median_is_the_middle_return() {
        assert_eq!(median(&mut [3.0, 1.0, 2.0]), Some(2.0));
        assert_eq!(median(&mut []), None);
    }
}
