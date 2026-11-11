//! Paved-surface meshes: constrained Delaunay triangulation of plan shapes, refined so the surface
//! can follow the ground, split into square chunks and written for the Unreal commandlet.

use crate::osm::Pt;
use crate::shapes::Shapes;
use spade::{AngleLimit, ConstrainedDelaunayTriangulation, Point2, RefinementParameters, Triangulation};
use std::collections::HashMap;
use std::io::Write;

/// Surface materials, in the slot order the commandlet gives the meshes.
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u8)]
pub enum Surface {
    Asphalt = 0,
    YardAsphalt = 1,
    Pavers = 2,
    Setts = 3,
    Kerb = 4,
    Paint = 5,
}
pub const SURFACE_NAMES: [&str; 6] = ["asphalt", "yard_asphalt", "pavers", "setts", "kerb", "paint"];

/// Triangles covering `shapes`, with interior points added until no triangle is larger than
/// `max_area` square metres (so the surface can bend with the ground) and none has an angle
/// under 20°.
pub fn triangulate(shapes: &Shapes, max_area: f64) -> Vec<[Pt; 3]> {
    let mut cdt: ConstrainedDelaunayTriangulation<Point2<f64>> = ConstrainedDelaunayTriangulation::new();
    for contour in shapes.iter().flatten() {
        let handles: Vec<_> = contour.iter().filter_map(|p| cdt.insert(Point2::new(p[0], p[1])).ok()).collect();
        for i in 0..handles.len() {
            let (a, b) = (handles[i], handles[(i + 1) % handles.len()]);
            if a != b {
                cdt.try_add_constraint(a, b);
            }
        }
    }
    if cdt.num_vertices() < 3 {
        return Vec::new();
    }
    // Every face is refined, not only those inside the constraint loops: one constraint that
    // could not be added (crossing another) opens a loop, and the faces behind it would stay
    // coarse and cut through the ground. The faces outside are dropped below.
    let params = RefinementParameters::<f64>::new()
        .with_max_allowed_area(max_area)
        .with_min_required_area(0.02)
        .with_angle_limit(AngleLimit::from_deg(20.0))
        .with_max_additional_vertices(2_000_000);
    cdt.refine(params);
    let inside = crate::shapes::Index::new(shapes);
    let tris: Vec<[Pt; 3]> = cdt.inner_faces()
        .filter_map(|f| {
            let v = f.vertices().map(|v| {
                let p = v.position();
                [p.x, p.y]
            });
            let centroid = [(v[0][0] + v[1][0] + v[2][0]) / 3.0, (v[0][1] + v[1][1] + v[2][1]) / 3.0];
            inside.contains(centroid).then_some(v)
        })
        .collect();
    // Whatever refinement could not reach (it stops at its vertex budget, and keeps long slivers
    // along constraint edges) is split here: no triangle may be larger than the limit or longer
    // than 1.5 m, so none can bridge a dip in the ground and pass under the landscape.
    const MAX_EDGE_SQ: f64 = 1.5 * 1.5;
    let mut out = Vec::with_capacity(tris.len());
    let mut stack = tris;
    while let Some(t) = stack.pop() {
        let d = |a: Pt, b: Pt| (a[0] - b[0]).powi(2) + (a[1] - b[1]).powi(2);
        let edges = [d(t[0], t[1]), d(t[1], t[2]), d(t[2], t[0])];
        let longest = edges.iter().cloned().fold(0.0, f64::max);
        if crate::shapes::ring_area(&t).abs() <= max_area * 1.5 && longest <= MAX_EDGE_SQ {
            out.push(t);
            continue;
        }
        let k = (0..3).max_by(|&a, &b| edges[a].partial_cmp(&edges[b]).unwrap()).unwrap();
        let (a, b, c) = (t[k], t[(k + 1) % 3], t[(k + 2) % 3]);
        let m = [(a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0];
        stack.push([a, m, c]);
        stack.push([m, b, c]);
    }
    out
}

#[derive(Default)]
pub struct Chunk {
    pub verts: Vec<[f32; 3]>,
    pub uvs: Vec<[f32; 2]>,
    pub tris: Vec<[u32; 3]>,
    pub mats: Vec<u8>,
    index: HashMap<(i64, i64, i64, i64, i64), u32>,
}

impl Chunk {
    fn vertex(&mut self, p: [f64; 3], uv: [f64; 2]) -> u32 {
        let q = |x: f64| (x * 1000.0).round() as i64;
        let key = (q(p[0]), q(p[1]), q(p[2]), q(uv[0]), q(uv[1]));
        if let Some(&i) = self.index.get(&key) {
            return i;
        }
        let i = self.verts.len() as u32;
        self.verts.push([p[0] as f32, p[1] as f32, p[2] as f32]);
        self.uvs.push([uv[0] as f32, uv[1] as f32]);
        self.index.insert(key, i);
        i
    }
}

/// Every paved triangle, bucketed into square chunks of `chunk` metres by centroid.
pub struct PavedMesh {
    pub half: f64,
    pub chunk: f64,
    pub chunks: HashMap<(i32, i32), Chunk>,
}

/// Unreal's front face for triangle A, B, C has normal (C − A) × (B − A). Plan coordinates
/// (x east, y south, z up) are a proper rotation of Unreal's, so the rule carries over.
fn ue_normal(a: [f64; 3], b: [f64; 3], c: [f64; 3]) -> [f64; 3] {
    let u = [c[0] - a[0], c[1] - a[1], c[2] - a[2]];
    let v = [b[0] - a[0], b[1] - a[1], b[2] - a[2]];
    [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
}

impl PavedMesh {
    pub fn new(half: f64, chunk: f64) -> Self {
        PavedMesh { half, chunk, chunks: HashMap::new() }
    }

    fn chunk_of(&self, p: Pt) -> (i32, i32) {
        let n = (2.0 * self.half / self.chunk).ceil() as i32;
        let i = (((p[0] + self.half) / self.chunk).floor() as i32).clamp(0, n - 1);
        let j = (((p[1] + self.half) / self.chunk).floor() as i32).clamp(0, n - 1);
        (i, j)
    }

    /// Adds a triangle facing `toward` (a direction), with per-vertex UVs.
    pub fn add(&mut self, p: [[f64; 3]; 3], uv: [[f64; 2]; 3], toward: [f64; 3], mat: Surface) {
        let n = ue_normal(p[0], p[1], p[2]);
        let (order, uvo) = if n[0] * toward[0] + n[1] * toward[1] + n[2] * toward[2] >= 0.0 {
            (p, uv)
        } else {
            ([p[0], p[2], p[1]], [uv[0], uv[2], uv[1]])
        };
        let centroid = [(p[0][0] + p[1][0] + p[2][0]) / 3.0, (p[0][1] + p[1][1] + p[2][1]) / 3.0];
        let key = self.chunk_of(centroid);
        let chunk = self.chunks.entry(key).or_default();
        let idx = [chunk.vertex(order[0], uvo[0]), chunk.vertex(order[1], uvo[1]), chunk.vertex(order[2], uvo[2])];
        if idx[0] != idx[1] && idx[1] != idx[2] && idx[0] != idx[2] {
            chunk.tris.push(idx);
            chunk.mats.push(mat as u8);
        }
    }

    /// Adds a horizontal-ish surface: plan triangles lifted by `z`, UVs in plan metres, facing up.
    pub fn add_surface(&mut self, tris: &[[Pt; 3]], z: &dyn Fn(Pt) -> f64, mat: &dyn Fn(Pt) -> Surface) {
        for t in tris {
            let p = t.map(|q| [q[0], q[1], z(q)]);
            let uv = t.map(|q| [q[0], q[1]]);
            let centroid = [(t[0][0] + t[1][0] + t[2][0]) / 3.0, (t[0][1] + t[1][1] + t[2][1]) / 3.0];
            self.add(p, uv, [0.0, 0.0, 1.0], mat(centroid));
        }
    }

    /// Adds a vertical strip along a polyline between `bottom` and `top` heights, facing the
    /// side given by `facing` (a plan direction function of the segment midpoint).
    pub fn add_wall(&mut self, path: &[Pt], closed: bool, bottom: &dyn Fn(Pt) -> f64, top: &dyn Fn(Pt) -> f64, facing: &dyn Fn(Pt, Pt) -> Option<[f64; 2]>, mat: Surface) {
        let n = path.len();
        let segments = if closed { n } else { n.saturating_sub(1) };
        let mut along = 0.0;
        for i in 0..segments {
            let (a, b) = (path[i], path[(i + 1) % n]);
            let len = ((b[0] - a[0]).powi(2) + (b[1] - a[1]).powi(2)).sqrt();
            if len < 1e-4 {
                continue;
            }
            // Densify so the strip follows the ground.
            let steps = (len / 0.75).ceil().max(1.0) as usize;
            for s in 0..steps {
                let (t0, t1) = (s as f64 / steps as f64, (s + 1) as f64 / steps as f64);
                let p0 = [a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0];
                let p1 = [a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1];
                let Some(dir) = facing(p0, p1) else { continue };
                let (u0, u1) = (along + len * t0, along + len * t1);
                let (b0, b1, h0, h1) = (bottom(p0), bottom(p1), top(p0), top(p1));
                if h0 - b0 < 0.005 && h1 - b1 < 0.005 {
                    continue;
                }
                let q = [[p0[0], p0[1], b0], [p1[0], p1[1], b1], [p1[0], p1[1], h1], [p0[0], p0[1], h0]];
                let uv = [[u0, -b0], [u1, -b1], [u1, -h1], [u0, -h0]];
                let toward = [dir[0], dir[1], 0.0];
                self.add([q[0], q[1], q[2]], [uv[0], uv[1], uv[2]], toward, mat);
                self.add([q[0], q[2], q[3]], [uv[0], uv[2], uv[3]], toward, mat);
            }
            along += len;
        }
    }

    /// For each landscape cell (between grid points r, r + 1 and c, c + 1, stored at r × n + c),
    /// the lowest corner of any upward-facing paved triangle that reaches into it, or +∞. A
    /// landscape quad whose four corners are below that is below every paved point in the cell,
    /// even where the paving dips between grid points.
    pub fn lowest_in_cells(&self, grid: &crate::grid::Grid) -> Vec<f32> {
        let n = grid.n;
        let mut out = vec![f32::INFINITY; n * n];
        let s = grid.step();
        for chunk in self.chunks.values() {
            for t in &chunk.tris {
                let p = t.map(|i| chunk.verts[i as usize].map(|x| x as f64));
                if ue_normal(p[0], p[1], p[2])[2] <= 0.0 {
                    continue;
                }
                let low = p.iter().map(|q| q[2]).fold(f64::MAX, f64::min) as f32;
                let (x0, x1) = (p.iter().map(|q| q[0]).fold(f64::MAX, f64::min), p.iter().map(|q| q[0]).fold(f64::MIN, f64::max));
                let (y0, y1) = (p.iter().map(|q| q[1]).fold(f64::MAX, f64::min), p.iter().map(|q| q[1]).fold(f64::MIN, f64::max));
                let cell = |v: f64| ((v + grid.half) / s).floor().clamp(0.0, (n - 2) as f64) as usize;
                for r in cell(y0)..=cell(y1) {
                    for c in cell(x0)..=cell(x1) {
                        let o = &mut out[r * n + c];
                        *o = o.min(low);
                    }
                }
            }
        }
        out
    }

    /// The height of the highest upward-facing paved triangle over each grid point, or NaN
    /// where there is none. This is the surface exactly as the meshes draw it.
    pub fn top_over(&self, grid: &crate::grid::Grid) -> crate::grid::Grid {
        let mut out = grid.like(f32::NAN);
        let s = grid.step();
        for chunk in self.chunks.values() {
            for t in &chunk.tris {
                let p = t.map(|i| chunk.verts[i as usize].map(|x| x as f64));
                if ue_normal(p[0], p[1], p[2])[2] <= 0.0 {
                    continue;
                }
                let det = (p[1][1] - p[2][1]) * (p[0][0] - p[2][0]) + (p[2][0] - p[1][0]) * (p[0][1] - p[2][1]);
                if det.abs() < 1e-12 {
                    continue;
                }
                let (x0, x1) = (p.iter().map(|q| q[0]).fold(f64::MAX, f64::min), p.iter().map(|q| q[0]).fold(f64::MIN, f64::max));
                let (y0, y1) = (p.iter().map(|q| q[1]).fold(f64::MAX, f64::min), p.iter().map(|q| q[1]).fold(f64::MIN, f64::max));
                let c0 = (((x0 + grid.half) / s).ceil().max(0.0)) as usize;
                let c1 = (((x1 + grid.half) / s).floor().min((grid.n - 1) as f64)) as isize;
                let r0 = (((y0 + grid.half) / s).ceil().max(0.0)) as usize;
                let r1 = (((y1 + grid.half) / s).floor().min((grid.n - 1) as f64)) as isize;
                if c1 < 0 || r1 < 0 {
                    continue;
                }
                for r in r0..=r1 as usize {
                    for c in c0..=c1 as usize {
                        let q = grid.pos(r, c);
                        let a = ((p[1][1] - p[2][1]) * (q[0] - p[2][0]) + (p[2][0] - p[1][0]) * (q[1] - p[2][1])) / det;
                        let b = ((p[2][1] - p[0][1]) * (q[0] - p[2][0]) + (p[0][0] - p[2][0]) * (q[1] - p[2][1])) / det;
                        let g = 1.0 - a - b;
                        if a < -1e-9 || b < -1e-9 || g < -1e-9 {
                            continue;
                        }
                        let z = (a * p[0][2] + b * p[1][2] + g * p[2][2]) as f32;
                        let cur = out.at(r, c);
                        if cur.is_nan() || z > cur {
                            out.set(r, c, z);
                        }
                    }
                }
            }
        }
        out
    }

    pub fn triangle_count(&self) -> usize {
        self.chunks.values().map(|c| c.tris.len()).sum()
    }

    pub fn count_by_surface(&self) -> [usize; 6] {
        let mut out = [0; 6];
        for c in self.chunks.values() {
            for &m in &c.mats {
                out[m as usize] += 1;
            }
        }
        out
    }

    /// Little-endian: "MBGM", version, chunk count; per chunk: column, row, vertex count, xyz
    /// (plan metres, f32), uv (f32), triangle count, indices (u32 × 3), material (u8 per triangle).
    pub fn write(&self, path: &std::path::Path) -> std::io::Result<()> {
        let mut keys: Vec<_> = self.chunks.keys().copied().collect();
        keys.sort();
        let mut out = Vec::new();
        out.write_all(b"MBGM")?;
        out.write_all(&1u32.to_le_bytes())?;
        out.write_all(&(keys.len() as u32).to_le_bytes())?;
        for k in keys {
            let c = &self.chunks[&k];
            out.write_all(&k.0.to_le_bytes())?;
            out.write_all(&k.1.to_le_bytes())?;
            out.write_all(&(c.verts.len() as u32).to_le_bytes())?;
            for v in &c.verts {
                for x in v {
                    out.write_all(&x.to_le_bytes())?;
                }
            }
            for uv in &c.uvs {
                for x in uv {
                    out.write_all(&x.to_le_bytes())?;
                }
            }
            out.write_all(&(c.tris.len() as u32).to_le_bytes())?;
            for t in &c.tris {
                for x in t {
                    out.write_all(&x.to_le_bytes())?;
                }
            }
            out.write_all(&c.mats)?;
        }
        std::fs::write(path, out)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::shapes::{area, stroke};

    #[test]
    fn triangulation_covers_the_shape_and_respects_the_size_limit() {
        let s = stroke(&[[-20.0, 0.0], [20.0, 5.0]], 6.0, true);
        let tris = triangulate(&s, 1.0);
        let covered: f64 = tris.iter().map(|t| crate::shapes::ring_area(&t.to_vec()).abs()).sum();
        assert!((covered - area(&s)).abs() / area(&s) < 0.01, "{covered} vs {}", area(&s));
        assert!(tris.iter().all(|t| crate::shapes::ring_area(&t.to_vec()).abs() <= 1.0 + 1e-9));
    }

    #[test]
    fn surfaces_face_up_in_unreal_winding() {
        let mut m = PavedMesh::new(10.0, 10.0);
        m.add_surface(&[[[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]]], &|_| 0.0, &|_| Surface::Asphalt);
        let c = m.chunks.values().next().unwrap();
        let t = c.tris[0];
        let p = t.map(|i| c.verts[i as usize].map(|x| x as f64));
        assert!(ue_normal(p[0], p[1], p[2])[2] > 0.0);
    }
}
