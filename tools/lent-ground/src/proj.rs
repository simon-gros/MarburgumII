//! WGS84 / ETRS89 latitude and longitude to D96/TM (EPSG:3794), and D96/TM to Lent plan metres.
//!
//! D96/TM is a transverse Mercator on GRS80 with central meridian 15° E, scale 0.9999, false easting
//! 500 000 m and false northing −5 000 000 m. D96 is ETRS89, so WGS84 coordinates from
//! OpenStreetMap need no datum shift at the accuracy that matters here. The forward projection is
//! Krüger's series to third order, good to well under a millimetre this close to the meridian.

const A: f64 = 6_378_137.0;
const F: f64 = 1.0 / 298.257_222_101;
const K0: f64 = 0.9999;
const LON0: f64 = 15.0;
const FALSE_EASTING: f64 = 500_000.0;
const FALSE_NORTHING: f64 = -5_000_000.0;

/// The middle of Stari most. Plan x is metres east of it, plan y metres south of it, as in the
/// original game's terrain and building data.
pub const ORIGIN_EAST: f64 = 549_527.8;
pub const ORIGIN_NORTH: f64 = 157_564.0;

/// The Drava's surface above sea level; plan heights are measured from it.
pub const HEIGHT_DATUM: f64 = 252.98;

pub fn to_d96tm(lat_deg: f64, lon_deg: f64) -> (f64, f64) {
    let n = F / (2.0 - F);
    let big_a = A / (1.0 + n) * (1.0 + n * n / 4.0 + n.powi(4) / 64.0);
    let alpha = [
        n / 2.0 - 2.0 * n * n / 3.0 + 5.0 * n.powi(3) / 16.0,
        13.0 * n * n / 48.0 - 3.0 * n.powi(3) / 5.0,
        61.0 * n.powi(3) / 240.0,
    ];
    let phi = lat_deg.to_radians();
    let dlon = (lon_deg - LON0).to_radians();
    let c = 2.0 * n.sqrt() / (1.0 + n);
    let t = (phi.sin().atanh() - c * (c * phi.sin()).atanh()).sinh();
    let xi = (t / dlon.cos()).atan();
    let eta = (dlon.sin() / (1.0 + t * t).sqrt()).atanh();
    let mut e = eta;
    let mut nn = xi;
    for (j, a) in alpha.iter().enumerate() {
        let k = 2.0 * (j as f64 + 1.0);
        e += a * (k * xi).cos() * (k * eta).sinh();
        nn += a * (k * xi).sin() * (k * eta).cosh();
    }
    (FALSE_EASTING + K0 * big_a * e, FALSE_NORTHING + K0 * big_a * nn)
}

/// D96/TM easting and northing to plan metres (x east, y south of Stari most).
pub fn d96_to_plan(east: f64, north: f64) -> (f64, f64) {
    (east - ORIGIN_EAST, ORIGIN_NORTH - north)
}

pub fn wgs84_to_plan(lat: f64, lon: f64) -> (f64, f64) {
    let (e, n) = to_d96tm(lat, lon);
    d96_to_plan(e, n)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn central_meridian_maps_to_false_easting() {
        let (e, _) = to_d96tm(46.0, 15.0);
        assert!((e - 500_000.0).abs() < 1e-6);
    }

    #[test]
    fn maribor_lands_in_its_lidar_tile() {
        // Stari most, Maribor, falls in ARSO tile TM_549_157 (E 549–550 km, N 157–158 km).
        let (e, n) = to_d96tm(46.5555, 15.6430);
        assert!((549_000.0..550_000.0).contains(&e), "east {e}");
        assert!((157_000.0..158_000.0).contains(&n), "north {n}");
    }
}
