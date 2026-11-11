//! A square raster over the Lent window in plan metres. Row 0 is the north edge (plan y = −half),
//! column 0 the west edge (plan x = −half); values sit at the grid points, so a grid of `n` points
//! spans `n − 1` cells. Rasterising, distance transforms and smoothing all work on this grid.

use crate::osm::Pt;

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
fn edt_1d(f: &[f64], d: &mut [f64]) {
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
