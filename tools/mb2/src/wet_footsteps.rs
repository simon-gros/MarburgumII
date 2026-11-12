//! `mb2 generate wet-footsteps`: the steps on a wet street (ADR-061). Kenney's footstep_concrete_00N (CC0, decoded
//! beside it) with a puddle splash on top: a band-passed noise burst from the moment the foot lands and a few droplets
//! after it. Deterministic (fixed seed per variant), so the committed WAVs can be checked against a rerun.

use std::path::Path;

use crate::repo;

pub const SOURCE: &str = "SourceArt/Audio/Footsteps/KenneyImpactSounds";
pub const OUTPUT: &str = "SourceArt/Audio/Footsteps/Generated";
pub const VARIANTS: usize = 5;
const RATE: u32 = 44_100;

/// splitmix64, as the weather planner uses: the same numbers on every platform.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> f64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut x = self.0;
        x = (x ^ (x >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        x = (x ^ (x >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        x ^= x >> 31;
        (x >> 11) as f64 / (1u64 << 53) as f64
    }

    fn range(&mut self, low: f64, high: f64) -> f64 {
        low + (high - low) * self.next()
    }
}

/// Mono 16-bit PCM samples of a canonical 44-byte-header WAV (what ffmpeg's pcm_s16le writes, plus LIST chunks).
pub fn read_wav(bytes: &[u8]) -> Result<Vec<f64>, String> {
    if bytes.len() < 12 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return Err("not a RIFF WAVE file".into());
    }
    let mut at = 12;
    let mut format_ok = false;
    while at + 8 <= bytes.len() {
        let id = &bytes[at..at + 4];
        let size = u32::from_le_bytes(bytes[at + 4..at + 8].try_into().unwrap()) as usize;
        let body = at + 8;
        if body + size > bytes.len() {
            return Err("truncated chunk".into());
        }
        if id == b"fmt " {
            let format = u16::from_le_bytes([bytes[body], bytes[body + 1]]);
            let channels = u16::from_le_bytes([bytes[body + 2], bytes[body + 3]]);
            let rate = u32::from_le_bytes(bytes[body + 4..body + 8].try_into().unwrap());
            let bits = u16::from_le_bytes([bytes[body + 14], bytes[body + 15]]);
            format_ok = format == 1 && channels == 1 && rate == RATE && bits == 16;
        } else if id == b"data" {
            if !format_ok {
                return Err("expected mono 16-bit PCM at 44.1 kHz".into());
            }
            return Ok(bytes[body..body + size].chunks_exact(2).map(|s| i16::from_le_bytes([s[0], s[1]]) as f64 / 32768.0).collect());
        }
        at = body + size + (size & 1);
    }
    Err("no data chunk".into())
}

pub fn write_wav(samples: &[f64]) -> Vec<u8> {
    let data = samples.len() * 2;
    let mut out = Vec::with_capacity(44 + data);
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&((36 + data) as u32).to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes());
    out.extend_from_slice(&RATE.to_le_bytes());
    out.extend_from_slice(&(RATE * 2).to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&(data as u32).to_le_bytes());
    for &s in samples {
        out.extend_from_slice(&((s.clamp(-1.0, 1.0) * 32767.0).round() as i16).to_le_bytes());
    }
    out
}

/// One-pole low-pass coefficient for a cut-off frequency.
fn one_pole(cutoff: f64) -> f64 {
    1.0 - (-2.0 * std::f64::consts::PI * cutoff / RATE as f64).exp()
}

/// The dry step with a splash: what a foot on a wet street sounds like.
pub fn wet_step(dry: &[f64], seed: u64) -> Vec<f64> {
    let mut rng = Rng(seed);
    let peak = dry.iter().fold(0.0f64, |m, s| m.max(s.abs())).max(1e-6);
    // The foot lands where the dry step first reaches a tenth of its peak.
    let onset = dry.iter().position(|s| s.abs() >= 0.1 * peak).unwrap_or(0);
    let tail = (0.26 * RATE as f64) as usize;
    let mut out: Vec<f64> = dry.iter().map(|s| s * 0.85).collect();
    out.resize(onset.max(dry.len()) + tail, 0.0);

    // The splash: noise band-passed to roughly 0.9-5 kHz, a 3 ms attack and a decay of about 45 ms.
    let (high, low) = (one_pole(900.0), one_pole(rng.range(4200.0, 5600.0)));
    let decay = rng.range(0.038, 0.052);
    let level = 0.42 * peak;
    let (mut hp1, mut hp2, mut lp1, mut lp2) = (0.0, 0.0, 0.0, 0.0);
    for i in 0..tail {
        let t = i as f64 / RATE as f64;
        let envelope = (t / 0.003).min(1.0) * (-t / decay).exp();
        let noise = rng.next() * 2.0 - 1.0;
        hp1 += high * (noise - hp1);
        hp2 += high * (hp1 - hp2);
        let band = noise - hp2;
        lp1 += low * (band - lp1);
        lp2 += low * (lp1 - lp2);
        out[onset + i] += level * 2.2 * envelope * lp2;
    }
    // Droplets: short falling chirps after the splash.
    let drops = 5 + (rng.next() * 4.0) as usize;
    for _ in 0..drops {
        let start = onset + (rng.range(0.025, 0.2) * RATE as f64) as usize;
        let length = (rng.range(0.008, 0.016) * RATE as f64) as usize;
        let from = rng.range(1800.0, 4200.0);
        let amplitude = rng.range(0.05, 0.16) * peak;
        let mut phase = 0.0;
        for i in 0..length {
            let at = start + i;
            if at >= out.len() {
                break;
            }
            let t = i as f64 / length as f64;
            phase += 2.0 * std::f64::consts::PI * from * (1.0 - 0.35 * t) / RATE as f64;
            out[at] += amplitude * phase.sin() * (1.0 - t) * (1.0 - t);
        }
    }
    // Never louder than the dry step at its loudest.
    let wet_peak = out.iter().fold(0.0f64, |m, s| m.max(s.abs()));
    if wet_peak > peak {
        out.iter_mut().for_each(|s| *s *= peak / wet_peak);
    }
    out
}

pub fn run() -> u8 {
    let root = repo::root();
    if let Err(error) = generate(&root) {
        eprintln!("{error}");
        return 1;
    }
    0
}

fn generate(root: &Path) -> Result<(), String> {
    std::fs::create_dir_all(root.join(OUTPUT)).map_err(|e| e.to_string())?;
    for variant in 0..VARIANTS {
        let source = root.join(SOURCE).join(format!("footstep_concrete_{variant:03}.wav"));
        let dry = read_wav(&std::fs::read(&source).map_err(|e| format!("{}: {e}", source.display()))?)
            .map_err(|e| format!("{}: {e}", source.display()))?;
        let wet = wet_step(&dry, 0x5157_6E65_7400 + variant as u64);
        let target = root.join(OUTPUT).join(format!("footstep_concrete_wet_{variant:03}.wav"));
        std::fs::write(&target, write_wav(&wet)).map_err(|e| format!("{}: {e}", target.display()))?;
        println!("Wrote {} ({:.3} s)", target.strip_prefix(root).unwrap_or(&target).display(), wet.len() as f64 / RATE as f64);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wav_round_trip() {
        let samples = vec![0.0, 0.5, -0.5, 0.25];
        let back = read_wav(&write_wav(&samples)).unwrap();
        for (a, b) in samples.iter().zip(&back) {
            assert!((a - b).abs() < 1e-4);
        }
    }

    #[test]
    fn a_wet_step_is_the_step_with_a_tail_and_no_louder() {
        let mut dry = vec![0.0; 2000];
        for (i, s) in dry.iter_mut().enumerate().skip(100).take(800) {
            *s = 0.6 * ((i as f64) * 0.3).sin() * (-(i as f64 - 100.0) / 200.0).exp();
        }
        let wet = wet_step(&dry, 7);
        assert_eq!(wet, wet_step(&dry, 7));
        assert!(wet.len() > dry.len());
        let peak = |v: &[f64]| v.iter().fold(0.0f64, |m, s| m.max(s.abs()));
        assert!(peak(&wet) <= peak(&dry) + 1e-12);
        // The splash goes on after the dry step has ended.
        assert!(peak(&wet[dry.len()..]) > 0.01);
    }
}
