//! Polygon booleans and offsets on plan shapes, through i_overlay.

use crate::osm::Pt;
use i_overlay::core::fill_rule::FillRule;
use i_overlay::core::overlay_rule::OverlayRule;
use i_overlay::float::single::SingleFloatOverlay;
use i_overlay::mesh::outline::offset::OutlineOffset;
use i_overlay::mesh::stroke::offset::StrokeOffset;
use i_overlay::mesh::style::{LineCap, LineJoin, OutlineStyle, StrokeStyle};

/// Shapes: each shape is an outer contour followed by its holes.
pub type Shapes = Vec<Vec<Vec<Pt>>>;

const ROUND: f64 = 0.25;

fn contours(s: &Shapes) -> Vec<Vec<Pt>> {
    s.iter().flatten().cloned().collect()
}

pub fn union_all(parts: &[Shapes]) -> Shapes {
    let subject: Vec<Vec<Pt>> = parts.iter().flat_map(contours).collect();
    if subject.is_empty() {
        return Vec::new();
    }
    let empty: Vec<Vec<Pt>> = Vec::new();
    subject.overlay(&empty, OverlayRule::Subject, FillRule::NonZero)
}

pub fn difference(a: &Shapes, b: &Shapes) -> Shapes {
    if a.is_empty() || b.is_empty() {
        return a.clone();
    }
    contours(a).overlay(&contours(b), OverlayRule::Difference, FillRule::NonZero)
}

pub fn intersect(a: &Shapes, b: &Shapes) -> Shapes {
    if a.is_empty() || b.is_empty() {
        return Vec::new();
    }
    contours(a).overlay(&contours(b), OverlayRule::Intersect, FillRule::NonZero)
}

/// A band of the given width along an open polyline, with round joins and caps.
pub fn stroke(path: &[Pt], width: f64, round_caps: bool) -> Shapes {
    if path.len() < 2 || width <= 0.0 {
        return Vec::new();
    }
    let cap = if round_caps { LineCap::Round(ROUND) } else { LineCap::Butt };
    let style = StrokeStyle::new(width).line_join(LineJoin::Round(ROUND)).start_cap(cap.clone()).end_cap(cap);
    path.to_vec().stroke(style, false)
}

/// Grows (positive) or shrinks (negative) shapes by `d` metres with round corners.
pub fn offset(s: &Shapes, d: f64) -> Shapes {
    if s.is_empty() || d == 0.0 {
        return s.clone();
    }
    let style = OutlineStyle::new(d).line_join(LineJoin::Round(ROUND));
    s.outline(&style)
}

/// Morphological closing: fills inside corners with radius `r` (kerb radii at junctions).
pub fn close(s: &Shapes, r: f64) -> Shapes {
    offset(&offset(s, r), -r)
}

/// Morphological opening: drops slivers narrower than `2r`.
pub fn open(s: &Shapes, r: f64) -> Shapes {
    offset(&offset(s, -r), r)
}

pub fn polygon(rings: &[Vec<Pt>]) -> Shapes {
    // Even-odd over the rings, so inner rings become holes whatever their winding.
    let rings: Vec<Vec<Pt>> = rings.iter().map(|r| strip_closing(r)).filter(|r| r.len() >= 3).collect();
    if rings.is_empty() {
        return Vec::new();
    }
    let empty: Vec<Vec<Pt>> = Vec::new();
    rings.overlay(&empty, OverlayRule::Subject, FillRule::EvenOdd)
}

pub fn strip_closing(r: &[Pt]) -> Vec<Pt> {
    let mut v = r.to_vec();
    if v.len() > 1 && v[0] == *v.last().unwrap() {
        v.pop();
    }
    v
}

pub fn square(half: f64) -> Shapes {
    vec![vec![vec![[-half, -half], [half, -half], [half, half], [-half, half]]]]
}

pub fn area(s: &Shapes) -> f64 {
    s.iter().flatten().map(|c| ring_area(c)).sum::<f64>().abs()
}

pub fn ring_area(c: &[Pt]) -> f64 {
    let n = c.len();
    (0..n).map(|i| c[i][0] * c[(i + 1) % n][1] - c[(i + 1) % n][0] * c[i][1]).sum::<f64>() * 0.5
}

/// Even-odd point in shapes.
pub fn contains(s: &[Vec<Vec<Pt>>], p: Pt) -> bool {
    let mut inside = false;
    for c in s.iter().flatten() {
        let n = c.len();
        let mut j = n - 1;
        for i in 0..n {
            let (a, b) = (c[i], c[j]);
            if (a[1] > p[1]) != (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0] {
                inside = !inside;
            }
            j = i;
        }
    }
    inside
}

/// Point-in-shapes queries against many points: the edges sorted into horizontal bands, so a query
/// tests only the edges its band crosses. Same even-odd rule as `contains`.
pub struct Index {
    y0: f64,
    band: f64,
    bands: Vec<Vec<(Pt, Pt)>>,
}

impl Index {
    pub fn new(s: &[Vec<Vec<Pt>>]) -> Index {
        let band = 2.0;
        let (lo, hi) = s.iter().flatten().flatten().fold((f64::MAX, f64::MIN), |a, p| (a.0.min(p[1]), a.1.max(p[1])));
        if lo > hi {
            return Index { y0: 0.0, band, bands: Vec::new() };
        }
        let count = ((hi - lo) / band).floor() as usize + 1;
        let mut bands = vec![Vec::new(); count];
        for c in s.iter().flatten() {
            let n = c.len();
            for i in 0..n {
                let (a, b) = (c[i], c[(i + n - 1) % n]);
                let (b0, b1) = (((a[1].min(b[1]) - lo) / band).floor() as usize, ((a[1].max(b[1]) - lo) / band).floor() as usize);
                for k in b0..=b1.min(count - 1) {
                    bands[k].push((a, b));
                }
            }
        }
        Index { y0: lo, band, bands }
    }

    pub fn contains(&self, p: Pt) -> bool {
        let k = (p[1] - self.y0) / self.band;
        if k < 0.0 || k as usize >= self.bands.len() {
            return false;
        }
        let mut inside = false;
        for &(a, b) in &self.bands[k as usize] {
            if (a[1] > p[1]) != (b[1] > p[1]) && p[0] < (b[0] - a[0]) * (p[1] - a[1]) / (b[1] - a[1]) + a[0] {
                inside = !inside;
            }
        }
        inside
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_index_agrees_with_contains() {
        let s = union_all(&[stroke(&[[-30.0, 0.0], [30.0, 7.0]], 6.0, true), polygon(&[vec![[0.0, -20.0], [9.0, -20.0], [9.0, 20.0], [0.0, 20.0]], vec![[2.0, -5.0], [6.0, -5.0], [6.0, 5.0], [2.0, 5.0]]])]);
        let index = Index::new(&s);
        let mut checked = 0;
        for i in 0..80 {
            for j in 0..60 {
                let p = [-40.0 + i as f64 * 1.013, -30.0 + j as f64 * 1.007];
                assert_eq!(index.contains(p), contains(&s, p), "{p:?}");
                checked += 1;
            }
        }
        assert_eq!(checked, 4800);
    }

    #[test]
    fn crossing_strokes_union_into_one_shape() {
        let a = stroke(&[[-10.0, 0.0], [10.0, 0.0]], 4.0, true);
        let b = stroke(&[[0.0, -10.0], [0.0, 10.0]], 4.0, true);
        let u = union_all(&[a, b]);
        assert_eq!(u.len(), 1);
        let expected = 2.0 * 20.0 * 4.0 - 16.0 + 2.0 * std::f64::consts::PI * 4.0;
        assert!((area(&u) - expected).abs() < 2.0, "area {}", area(&u));
    }

    #[test]
    fn closing_rounds_the_inside_corners_of_a_junction() {
        let a = stroke(&[[-10.0, 0.0], [10.0, 0.0]], 4.0, false);
        let b = stroke(&[[0.0, 0.0], [0.0, 10.0]], 4.0, false);
        let u = union_all(&[a, b]);
        let c = close(&u, 3.0);
        assert!(area(&c) > area(&u) + 2.0);
        assert!(contains(&c, [2.3, 2.3]));
        assert!(!contains(&u, [2.3, 2.3]));
    }
}

