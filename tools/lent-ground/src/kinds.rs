//! The landscape's surface map (ADR-061): `surface.png` reduced to one surface kind per cell, which the
//! game reads at run time to tell what a foot or a tyre stands on where the ground is landscape rather
//! than paving (the paving carries physical materials instead).
//!
//! `lent_surface.bin`: `MBSK`, u32 version 1, u32 cells per side n, f32 half extent (plan metres), then
//! n × n kind bytes, row 0 north, column 0 west. Kinds: 0 hard standing, 1 grass, 2 earth, 3 gravel,
//! 4 river stones (`MarburgumSurface.h`). A cell takes the largest of the mask's mean weights over it,
//! hard standing being what the four weights leave.

use std::path::Path;

pub const KINDS: [&str; 5] = ["hard_standing", "grass", "earth", "gravel", "river_stones"];

/// The dominant kind of one set of mean weights (grass, earth, gravel, river stones).
pub fn dominant(weights: [f64; 4]) -> u8 {
    let hard = (1.0 - weights.iter().sum::<f64>()).max(0.0);
    let mut best = (0u8, hard);
    for (index, &w) in weights.iter().enumerate() {
        if w > best.1 {
            best = (index as u8 + 1, w);
        }
    }
    best.0
}

/// Reduces an RGBA mask of `size` × `size` pixels over ±`half` metres to cells of `cell` metres.
pub fn reduce(rgba: &[u8], size: usize, half: f64, cell: f64) -> Result<(usize, Vec<u8>), String> {
    if rgba.len() != size * size * 4 || size == 0 {
        return Err(format!("surface mask is not {size}x{size} RGBA"));
    }
    let n = (2.0 * half / cell).round() as usize;
    if n == 0 || ((n as f64) * cell - 2.0 * half).abs() > 1e-6 {
        return Err(format!("{cell} m cells do not tile ±{half} m"));
    }
    let mut sums = vec![[0.0f64; 4]; n * n];
    let mut counts = vec![0u32; n * n];
    for row in 0..size {
        // Pixel centres, as the material samples them: u east, v south, over the whole square.
        let r = (((row as f64 + 0.5) / size as f64) * n as f64) as usize;
        for col in 0..size {
            let c = (((col as f64 + 0.5) / size as f64) * n as f64) as usize;
            let at = (row * size + col) * 4;
            let cell_index = r.min(n - 1) * n + c.min(n - 1);
            for channel in 0..4 {
                sums[cell_index][channel] += rgba[at + channel] as f64 / 255.0;
            }
            counts[cell_index] += 1;
        }
    }
    let kinds = sums
        .iter()
        .zip(&counts)
        .map(|(sum, &count)| {
            let k = count.max(1) as f64;
            dominant([sum[0] / k, sum[1] / k, sum[2] / k, sum[3] / k])
        })
        .collect();
    Ok((n, kinds))
}

pub fn build(surface: &Path, half: f64, cell: f64, out: &Path) -> Result<String, String> {
    let file = std::fs::File::open(surface).map_err(|e| format!("{}: {e}", surface.display()))?;
    let mut decoder = png::Decoder::new(std::io::BufReader::new(file));
    decoder.set_transformations(png::Transformations::EXPAND);
    let mut reader = decoder.read_info().map_err(|e| e.to_string())?;
    let mut rgba = vec![0u8; reader.output_buffer_size().ok_or("surface mask too large")?];
    let info = reader.next_frame(&mut rgba).map_err(|e| e.to_string())?;
    if info.color_type != png::ColorType::Rgba || info.bit_depth != png::BitDepth::Eight || info.width != info.height {
        return Err("surface mask must be square 8-bit RGBA".into());
    }
    rgba.truncate(info.buffer_size());
    let (n, kinds) = reduce(&rgba, info.width as usize, half, cell)?;

    let mut bytes = Vec::with_capacity(16 + kinds.len());
    bytes.extend_from_slice(b"MBSK");
    bytes.extend_from_slice(&1u32.to_le_bytes());
    bytes.extend_from_slice(&(n as u32).to_le_bytes());
    bytes.extend_from_slice(&(half as f32).to_le_bytes());
    bytes.extend_from_slice(&kinds);
    if let Some(parent) = out.parent() {
        std::fs::create_dir_all(parent).map_err(|e| e.to_string())?;
    }
    std::fs::write(out, &bytes).map_err(|e| format!("{}: {e}", out.display()))?;

    let mut tally = [0usize; 5];
    for &k in &kinds {
        tally[k as usize] += 1;
    }
    let shares: Vec<String> =
        KINDS.iter().zip(tally).map(|(name, count)| format!("{name} {:.1}%", 100.0 * count as f64 / kinds.len() as f64)).collect();
    Ok(format!("{n}x{n} cells of {cell} m, {}", shares.join(", ")))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn hard_standing_is_what_the_weights_leave() {
        assert_eq!(dominant([0.0, 0.0, 0.0, 0.0]), 0);
        assert_eq!(dominant([0.3, 0.1, 0.0, 0.0]), 0);
        assert_eq!(dominant([0.6, 0.1, 0.0, 0.0]), 1);
        assert_eq!(dominant([0.1, 0.5, 0.2, 0.0]), 2);
        assert_eq!(dominant([0.0, 0.0, 0.0, 0.9]), 4);
    }

    #[test]
    fn cells_take_the_mean_of_their_pixels() {
        // 4 × 4 pixels over ±2 m into 2 m cells: the north-west cell all grass, the south-east gravel.
        let mut rgba = vec![0u8; 4 * 4 * 4];
        for (row, col) in [(0, 0), (0, 1), (1, 0), (1, 1)] {
            rgba[(row * 4 + col) * 4] = 255;
        }
        for (row, col) in [(2, 2), (2, 3), (3, 2), (3, 3)] {
            rgba[(row * 4 + col) * 4 + 2] = 255;
        }
        let (n, kinds) = reduce(&rgba, 4, 2.0, 2.0).unwrap();
        assert_eq!(n, 2);
        assert_eq!(kinds, vec![1, 0, 0, 3]);
        assert!(reduce(&rgba, 4, 2.0, 3.0).is_err());
    }
}
