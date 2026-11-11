//! A square raster over the Lent window in plan metres. Row 0 is the north edge (plan y = −half),
//! column 0 the west edge (plan x = −half); values sit at the grid points, so a grid of `n` points
//! spans `n − 1` cells. Rasterising, distance transforms and smoothing all work on this grid.

use crate::osm::Pt;

/// Distances over a rectangular patch of a grid (see `Grid::near_polygon`).
pub struct Patch {
    n: usize,
    r0: usize,
    c0: usize,
    rows: usize,
    cols: usize,
    d: Vec<f32>,
}

impl Patch {
    /// (index into the whole grid, distance) for every point of the patch.
    pub fn iter(&self) -> impl Iterator<Item = (usize, f32)> + '_ {
        (0..self.rows * self.cols).map(move |k| ((self.r0 + k / self.cols) * self.n + self.c0 + k % self.cols, self.d[k]))
    }
}

#[derive(Clone)]
pub struct Grid {
    pub n: usize,
    pub half: f64,
    pub v: Vec<f32>,
}

impl Grid {
    pub fn new(n: usize, half: f64, fill: f32) -> Self {
        Grid { n, half, v: vec![fill; n * n] }
    }
    pub fn like(&self, fill: f32) -> Self {
        Grid::new(self.n, self.half, fill)
    }
    pub fn step(&self) -> f64 {
        2.0 * self.half / (self.n - 1) as f64
    }
    #[inline]
    pub fn at(&self, r: usize, c: usize) -> f32 {
        self.v[r * self.n + c]
    }
    #[inline]
    pub fn set(&mut self, r: usize, c: usize, value: f32) {
        self.v[r * self.n + c] = value;
    }
    /// Plan position of a grid point.
    pub fn pos(&self, r: usize, c: usize) -> Pt {
        let s = self.step();
        [-self.half + c as f64 * s, -self.half + r as f64 * s]
    }
    /// Fractional (row, column) of a plan position.
    pub fn rc(&self, p: Pt) -> (f64, f64) {
        let s = self.step();
        ((p[1] + self.half) / s, (p[0] + self.half) / s)
    }
    /// Bilinear sample, clamped to the edge.
    pub fn sample(&self, p: Pt) -> f32 {
        let (r, c) = self.rc(p);
        let max = (self.n - 1) as f64;
        let (r, c) = (r.clamp(0.0, max), c.clamp(0.0, max));
        let (r0, c0) = (r.floor().min(max - 1.0) as usize, c.floor().min(max - 1.0) as usize);
        let (fr, fc) = ((r - r0 as f64) as f32, (c - c0 as f64) as f32);
        let a = self.at(r0, c0) * (1.0 - fc) + self.at(r0, c0 + 1) * fc;
        let b = self.at(r0 + 1, c0) * (1.0 - fc) + self.at(r0 + 1, c0 + 1) * fc;
        a * (1.0 - fr) + b * fr
    }

    /// Sets every grid point inside the polygon (even-odd over all rings) to `value`.
    pub fn fill_rings(&mut self, rings: &[Vec<Pt>], value: f32) {
        let s = self.step();
        for r in 0..self.n {
            let y = -self.half + r as f64 * s;
            let mut xs: Vec<f64> = Vec::new();
            for ring in rings {
                let m = ring.len();
                for i in 0..m {
                    let (a, b) = (ring[i], ring[(i + 1) % m]);
                    if (a[1] <= y) != (b[1] <= y) {
                        xs.push(a[0] + (y - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
                    }
                }
            }
            xs.sort_by(|a, b| a.partial_cmp(b).unwrap());
            for pair in xs.chunks(2) {
                if pair.len() < 2 {
                    continue;
                }
                let c0 = ((pair[0] + self.half) / s).ceil().max(0.0) as usize;
                let c1 = ((pair[1] + self.half) / s).floor();
                if c1 < 0.0 {
                    continue;
                }
                let c1 = (c1 as usize).min(self.n - 1);
                for c in c0..=c1 {
                    if c < self.n {
                        self.set(r, c, value);
                    }
                }
            }
        }
    }

    /// Distance (metres) from the grid points within `reach` metres of one polygon's bounding box
    /// to the polygon, which counts as distance 0 inside. Only that patch of the grid is touched,
    /// so a thousand footprints cost a thousand patches rather than a thousand whole grids.
    pub fn near_polygon(&self, rings: &[Vec<Pt>], reach: f64) -> Patch {
        let s = self.step();
        let (mut x0, mut y0, mut x1, mut y1) = (f64::MAX, f64::MAX, f64::MIN, f64::MIN);
        for p in rings.iter().flatten() {
            (x0, y0, x1, y1) = (x0.min(p[0]), y0.min(p[1]), x1.max(p[0]), y1.max(p[1]));
        }
        let last = (self.n - 1) as f64;
        let idx = |v: f64, f: fn(f64) -> f64| f((v + self.half) / s).clamp(0.0, last) as usize;
        let (r0, r1) = (idx(y0 - reach, f64::floor), idx(y1 + reach, f64::ceil));
        let (c0, c1) = (idx(x0 - reach, f64::floor), idx(x1 + reach, f64::ceil));
        let (rows, cols) = (r1 - r0 + 1, c1 - c0 + 1);
        let inf = 1e20f64;
        let mut f = vec![inf; rows * cols];
        for r in 0..rows {
            let y = -self.half + (r0 + r) as f64 * s;
            let mut xs: Vec<f64> = Vec::new();
            for ring in rings {
                let m = ring.len();
                for i in 0..m {
                    let (a, b) = (ring[i], ring[(i + 1) % m]);
                    if (a[1] <= y) != (b[1] <= y) {
                        xs.push(a[0] + (y - a[1]) / (b[1] - a[1]) * (b[0] - a[0]));
                    }
                }
            }
            xs.sort_by(|a, b| a.partial_cmp(b).unwrap());
            for pair in xs.chunks(2) {
                if pair.len() < 2 {
                    continue;
                }
                let a = ((pair[0] + self.half) / s).ceil().max(c0 as f64) as usize;
                let b = ((pair[1] + self.half) / s).floor().min(c1 as f64);
                if b < c0 as f64 {
                    continue;
                }
                for c in a..=b as usize {
                    f[r * cols + (c - c0)] = 0.0;
                }
            }
        }
        let mut line = vec![0.0; rows.max(cols)];
        let mut out = vec![0.0; rows.max(cols)];
        for c in 0..cols {
            for r in 0..rows {
                line[r] = f[r * cols + c];
            }
            edt_1d(&line[..rows], &mut out[..rows]);
            for r in 0..rows {
                f[r * cols + c] = out[r];
            }
        }
        for r in 0..rows {
            line[..cols].copy_from_slice(&f[r * cols..(r + 1) * cols]);
            edt_1d(&line[..cols], &mut out[..cols]);
            f[r * cols..(r + 1) * cols].copy_from_slice(&out[..cols]);
        }
        Patch { n: self.n, r0, c0, rows, cols, d: f.iter().map(|d| (d.sqrt() * s) as f32).collect() }
    }

    /// Euclidean distance (metres) from every grid point to the nearest point where `mask` is set.
    pub fn distance_to(mask: &Grid) -> Grid {
        let n = mask.n;
        let inf = 1e20f64;
        let mut f: Vec<f64> = mask.v.iter().map(|&m| if m > 0.5 { 0.0 } else { inf }).collect();
        let mut line = vec![0.0; n];
        let mut out = vec![0.0; n];
        for c in 0..n {
            for r in 0..n {
                line[r] = f[r * n + c];
            }
            edt_1d(&line, &mut out);
            for r in 0..n {
                f[r * n + c] = out[r];
            }
        }
        for r in 0..n {
            line.copy_from_slice(&f[r * n..(r + 1) * n]);
            edt_1d(&line, &mut out);
            f[r * n..(r + 1) * n].copy_from_slice(&out);
        }
        let s = mask.step();
        Grid { n, half: mask.half, v: f.iter().map(|d| (d.sqrt() * s) as f32).collect() }
    }

    /// Gaussian blur with standard deviation `sigma` metres. With `weights`, a normalised
    /// convolution: only points with weight contribute, so a road's height is smoothed along
    /// the road without pulling in the verge.
    pub fn blur(&self, sigma: f64, weights: Option<&Grid>) -> Grid {
        let s = self.step();
        let radius = ((3.0 * sigma / s).ceil() as isize).max(1);
        let kernel: Vec<f64> = (-radius..=radius).map(|i| (-0.5 * (i as f64 * s / sigma).powi(2)).exp()).collect();
        let n = self.n;
        let w: Vec<f64> = match weights {
            Some(g) => g.v.iter().map(|&x| x as f64).collect(),
            None => vec![1.0; n * n],
        };
        let mut num: Vec<f64> = self.v.iter().zip(&w).map(|(&v, &w)| v as f64 * w).collect();
        let mut den = w.clone();
        for pass in 0..2 {
            let (mut num2, mut den2) = (vec![0.0; n * n], vec![0.0; n * n]);
            for r in 0..n {
                for c in 0..n {
                    let (mut a, mut b) = (0.0, 0.0);
                    for (k, kv) in kernel.iter().enumerate() {
                        let o = k as isize - radius;
                        let (rr, cc) = if pass == 0 { (r as isize, c as isize + o) } else { (r as isize + o, c as isize) };
                        if rr < 0 || cc < 0 || rr >= n as isize || cc >= n as isize {
                            continue;
                        }
                        let i = rr as usize * n + cc as usize;
                        a += kv * num[i];
                        b += kv * den[i];
                    }
                    num2[r * n + c] = a;
                    den2[r * n + c] = b;
                }
            }
            num = num2;
            den = den2;
        }
        let v = self
            .v
            .iter()
            .enumerate()
            .map(|(i, &orig)| if den[i] > 1e-9 { (num[i] / den[i]) as f32 } else { orig })
            .collect();
        Grid { n, half: self.half, v }
    }

    /// Grey-scale opening with a square of half-width `radius` points: erosion then dilation.
    /// Removes raised features narrower than the square (piers, kiosks, misclassified returns)
    /// and keeps wider plateaus and steps.
    pub fn opened(&self, radius: usize) -> Grid {
        let eroded = self.window(radius, f32::min, f32::MAX);
        eroded.window(radius, f32::max, f32::MIN)
    }

    /// Every hollow with no way out filled to the level at which it would spill over (priority
    /// flood from the edge). Ground that drains to the edge, such as a river bed running out of the
    /// square, keeps its height.
    pub fn depressions_filled(&self) -> Grid {
        use std::cmp::Reverse;
        use std::collections::BinaryHeap;
        let n = self.n;
        // f32 heights as ordered integers: flip the sign bit, and all bits of negatives.
        let key = |v: f32| {
            let b = v.to_bits();
            if b >> 31 == 1 { !b } else { b | 0x8000_0000 }
        };
        let mut out = self.v.clone();
        let mut done = vec![false; n * n];
        let mut heap = BinaryHeap::new();
        for i in 0..n {
            for j in [i, (n - 1) * n + i, i * n, i * n + n - 1] {
                if !done[j] {
                    done[j] = true;
                    heap.push(Reverse((key(out[j]), j as u32)));
                }
            }
        }
        while let Some(Reverse((_, i))) = heap.pop() {
            let i = i as usize;
            let (r, c) = (i / n, i % n);
            for (dr, dc) in [(0i64, 1i64), (0, -1), (1, 0), (-1, 0)] {
                let (rr, cc) = (r as i64 + dr, c as i64 + dc);
                if rr < 0 || cc < 0 || rr as usize >= n || cc as usize >= n {
                    continue;
                }
                let j = rr as usize * n + cc as usize;
                if done[j] {
                    continue;
                }
                done[j] = true;
                out[j] = out[j].max(out[i]);
                heap.push(Reverse((key(out[j]), j as u32)));
            }
        }
        Grid { n, half: self.half, v: out }
    }

    fn window(&self, radius: usize, pick: fn(f32, f32) -> f32, identity: f32) -> Grid {
        let n = self.n;
        let mut tmp = self.v.clone();
        for r in 0..n {
            for c in 0..n {
                let (lo, hi) = (c.saturating_sub(radius), (c + radius).min(n - 1));
                tmp[r * n + c] = (lo..=hi).fold(identity, |a, k| pick(a, self.v[r * n + k]));
            }
        }
        let mut out = tmp.clone();
        for r in 0..n {
            for c in 0..n {
                let (lo, hi) = (r.saturating_sub(radius), (r + radius).min(n - 1));
                out[r * n + c] = (lo..=hi).fold(identity, |a, k| pick(a, tmp[k * n + c]));
            }
        }
        Grid { n, half: self.half, v: out }
    }

    /// Fills points where `known` is unset with the harmonic (smoothest) surface through the
    /// known points, coarse to fine, so large holes under buildings, trees and the bridge get no
    /// bumps of their own.
    pub fn inpaint(&mut self, known: &Grid) {
        let n = self.n;
        let mut levels: Vec<(usize, Vec<f64>, Vec<bool>)> = vec![(n, self.v.iter().map(|&x| x as f64).collect(), known.v.iter().map(|&k| k > 0.5).collect())];
        while levels.last().unwrap().0 > 9 {
            let (m, v, k) = levels.last().unwrap();
            let h = (m + 1) / 2;
            let (mut v2, mut k2) = (vec![0.0; h * h], vec![false; h * h]);
            for r in 0..h {
                for c in 0..h {
                    let (mut sum, mut cnt) = (0.0, 0);
                    for dr in 0..2 {
                        for dc in 0..2 {
                            let (rr, cc) = (2 * r + dr, 2 * c + dc);
                            if rr < *m && cc < *m && k[rr * m + cc] {
                                sum += v[rr * m + cc];
                                cnt += 1;
                            }
                        }
                    }
                    if cnt > 0 {
                        v2[r * h + c] = sum / cnt as f64;
                        k2[r * h + c] = true;
                    }
                }
            }
            levels.push((h, v2, k2));
        }
        // Coarsest level: start unknowns at the mean of the knowns.
        {
            let (m, v, k) = levels.last_mut().unwrap();
            let (sum, cnt) = v.iter().zip(k.iter()).filter(|(_, &k)| k).fold((0.0, 0), |a, (x, _)| (a.0 + x, a.1 + 1));
            let mean = if cnt > 0 { sum / cnt as f64 } else { 0.0 };
            for i in 0..v.len() {
                if !k[i] {
                    v[i] = mean;
                }
            }
            relax(*m, v, k, 400);
        }
        for li in (0..levels.len() - 1).rev() {
            let (coarse_m, coarse_v) = (levels[li + 1].0, levels[li + 1].1.clone());
            let (m, v, k) = &mut levels[li];
            for r in 0..*m {
                for c in 0..*m {
                    if !k[r * *m + c] {
                        let (cr, cc) = ((r as f64 / 2.0).min((coarse_m - 1) as f64), (c as f64 / 2.0).min((coarse_m - 1) as f64));
                        let (r0, c0) = (cr.floor() as usize, cc.floor() as usize);
                        let (r1, c1) = ((r0 + 1).min(coarse_m - 1), (c0 + 1).min(coarse_m - 1));
                        let (fr, fc) = (cr - r0 as f64, cc - c0 as f64);
                        let g = |a: usize, b: usize| coarse_v[a * coarse_m + b];
                        v[r * *m + c] = (g(r0, c0) * (1.0 - fc) + g(r0, c1) * fc) * (1.0 - fr) + (g(r1, c0) * (1.0 - fc) + g(r1, c1) * fc) * fr;
                    }
                }
            }
            relax(*m, v, k, 150);
        }
        self.v = levels[0].1.iter().map(|&x| x as f32).collect();
    }
}

fn relax(m: usize, v: &mut [f64], k: &[bool], iterations: usize) {
    for _ in 0..iterations {
        for r in 0..m {
            for c in 0..m {
                let i = r * m + c;
                if k[i] {
                    continue;
                }
                let (mut sum, mut cnt) = (0.0, 0.0);
                if r > 0 { sum += v[i - m]; cnt += 1.0; }
                if r + 1 < m { sum += v[i + m]; cnt += 1.0; }
                if c > 0 { sum += v[i - 1]; cnt += 1.0; }
                if c + 1 < m { sum += v[i + 1]; cnt += 1.0; }
                v[i] = sum / cnt;
            }
        }
    }
}

/// Felzenszwalb–Huttenlocher squared distance transform of one line.
pub(crate) fn edt_1d(f: &[f64], d: &mut [f64]) {
    let n = f.len();
    let mut v = vec![0usize; n];
    let mut z = vec![0.0f64; n + 1];
    let mut k = 0usize;
    z[0] = f64::NEG_INFINITY;
    z[1] = f64::INFINITY;
    let intersect = |q: usize, p: usize| ((f[q] + (q * q) as f64) - (f[p] + (p * p) as f64)) / (2.0 * q as f64 - 2.0 * p as f64);
    for q in 1..n {
        let mut s = intersect(q, v[k]);
        while s <= z[k] {
            k -= 1;
            s = intersect(q, v[k]);
        }
        k += 1;
        v[k] = q;
        z[k] = s;
        z[k + 1] = f64::INFINITY;
    }
    k = 0;
    for q in 0..n {
        while z[k + 1] < q as f64 {
            k += 1;
        }
        let p = v[k];
        d[q] = (q as f64 - p as f64).powi(2) + f[p];
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn distance_transform_is_euclidean() {
        let mut m = Grid::new(11, 5.0, 0.0);
        m.set(5, 5, 1.0);
        let d = Grid::distance_to(&m);
        assert!((d.at(5, 8) - 3.0).abs() < 1e-5);
        assert!((d.at(8, 9) - 5.0).abs() < 1e-5);
    }

    #[test]
    fn a_patch_round_a_polygon_matches_the_whole_grid_near_it() {
        let g = Grid::new(101, 25.0, 0.0);
        let ring = vec![[-3.2, -1.1], [4.1, -2.0], [5.3, 3.7], [-1.0, 4.4]];
        let mut mask = g.like(0.0);
        mask.fill_rings(&[ring.clone()], 1.0);
        let whole = Grid::distance_to(&mask);
        let patch = g.near_polygon(&[ring], 2.0);
        let mut seen = 0;
        for (i, d) in patch.iter() {
            if d <= 2.0 {
                assert!((whole.v[i] - d).abs() < 1e-4, "point {i}: {d} against {}", whole.v[i]);
                seen += 1;
            }
        }
        assert!(seen > 100);
    }

    #[test]
    fn a_closed_pit_is_filled_to_its_rim_and_a_channel_to_the_edge_is_not() {
        let mut g = Grid::new(21, 10.0, 5.0);
        for r in 8..12 {
            for c in 3..6 {
                g.set(r, c, -4.0); // a pit
            }
        }
        g.set(9, 4, 4.0);
        for c in 12..21 {
            g.set(10, c, 1.0); // a channel running out of the east edge
        }
        let f = g.depressions_filled();
        assert_eq!(f.at(9, 3), 5.0);
        assert_eq!(f.at(10, 15), 1.0);
        assert_eq!(f.at(0, 0), 5.0);
    }

    #[test]
    fn inpainting_a_plane_recovers_the_plane() {
        let mut g = Grid::new(33, 16.0, 0.0);
        let mut known = g.like(1.0);
        for r in 0..33 {
            for c in 0..33 {
                g.set(r, c, (0.1 * r as f64 + 0.05 * c as f64) as f32);
            }
        }
        let truth = g.clone();
        for r in 8..25 {
            for c in 8..25 {
                known.set(r, c, 0.0);
                g.set(r, c, 99.0);
            }
        }
        g.inpaint(&known);
        for i in 0..g.v.len() {
            assert!((g.v[i] - truth.v[i]).abs() < 0.02, "{i}: {} vs {}", g.v[i], truth.v[i]);
        }
    }

    #[test]
    fn fill_rings_respects_holes() {
        let mut g = Grid::new(21, 10.0, 0.0);
        let outer = vec![[-8.0, -8.0], [8.0, -8.0], [8.0, 8.0], [-8.0, 8.0]];
        let hole = vec![[-3.0, -3.0], [3.0, -3.0], [3.0, 3.0], [-3.0, 3.0]];
        g.fill_rings(&[outer, hole], 1.0);
        assert_eq!(g.sample([0.0, 0.0]), 0.0);
        assert_eq!(g.sample([6.0, 0.0]), 1.0);
        assert_eq!(g.sample([9.5, 0.0]), 0.0);
    }
}
