//! GFX-00 (Docs/GRAPHICS_ROADMAP.md): the rendering baseline of /Game/World/lent.
//!
//! * `mb2 gfx-views` prints the cameras the graphics roadmap adds to the visual survey (ADR-072): low over the Drava
//!   towards the Lent frontage and towards Stari most, the north-bank quay looking upstream, and one above the Lent
//!   roofs. Plan metres (x east, y south of the middle of Stari most, z above the Drava) as in `survey-views`.
//! * `mb2 gfx-baseline <dir>` reads the scenes scripts/gfx-baseline-linux.sh wrote under `<dir>` (one survey per
//!   time of day and weather, each with stats.tsv, render_config.json and summary.txt, plus `<dir>/meta.env` and
//!   `<dir>/gpu-monitor.tsv`) and writes `<dir>/baseline.csv` (one row per scene and view) and `<dir>/baseline.json`
//!   (provenance, the renderer's configuration, and per scene the median and worst views and whether another Unreal
//!   process ran during it). It measures; it does not judge. Exit 0 when every scene was read and every view was
//!   shot, 1 otherwise; a contended scene is reported, not failed.

use std::collections::BTreeMap;
use std::path::Path;

use serde_json::{json, Map, Value};

/// Plan metres to Unreal centimetres, as in survey_views.
fn ue(east: f64, south: f64, up: f64) -> String {
    format!("{:.0},{:.0},{:.0}", -south * 100.0, east * 100.0, up * 100.0)
}

pub fn views() -> Vec<String> {
    [
        // (name, eye, target, horizontal fov)
        ("river_to_lent", (-120.0, 0.0, 2.5), (-120.0, -80.0, 6.0), 75.0),
        ("river_to_stari_most", (-160.0, 5.0, 2.5), (0.0, 0.0, 10.0), 70.0),
        ("quay_upstream", (-60.0, -58.0, 4.0), (-300.0, -20.0, 0.0), 75.0),
        ("roof_lent", (-60.0, -140.0, 28.0), (0.0, -20.0, 4.0), 70.0),
    ]
    .iter()
    .map(|(name, (ex, ey, ez), (tx, ty, tz), fov)| format!("{name}@{}@{}@{fov}", ue(*ex, *ey, *ez), ue(*tx, *ty, *tz)))
    .collect()
}

pub fn print_views() -> u8 {
    println!("{}", views().join(";"));
    0
}

fn read_lossy(path: &Path) -> Option<String> {
    std::fs::read(path).ok().map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
}

/// `key=value` lines (meta.env), and the space-separated `key=value` words of summary.txt's first two lines.
fn key_values(text: &str, words: bool) -> BTreeMap<String, String> {
    let mut out = BTreeMap::new();
    for line in text.lines() {
        let pieces: Vec<&str> = if words { line.split_whitespace().collect() } else { vec![line] };
        for piece in pieces {
            if let Some((key, value)) = piece.split_once('=') {
                out.insert(key.trim().to_string(), value.trim().to_string());
            }
        }
    }
    out
}

/// stats.tsv as rows of column name to text, in file order.
fn read_stats(text: &str) -> Vec<BTreeMap<String, String>> {
    let mut lines = text.lines().filter(|l| !l.trim().is_empty());
    let Some(header) = lines.next() else { return Vec::new() };
    let columns: Vec<&str> = header.split('\t').collect();
    lines
        .map(|line| columns.iter().zip(line.split('\t')).map(|(c, v)| (c.to_string(), v.to_string())).collect())
        .collect()
}

fn number(row: &BTreeMap<String, String>, column: &str) -> Option<f64> {
    row.get(column).and_then(|v| v.parse::<f64>().ok())
}

fn median(mut values: Vec<f64>) -> Option<f64> {
    if values.is_empty() {
        return None;
    }
    values.sort_by(|a, b| a.total_cmp(b));
    let mid = values.len() / 2;
    Some(if values.len() % 2 == 1 { values[mid] } else { (values[mid - 1] + values[mid]) / 2.0 })
}

/// The view with the largest value of `column`, as `{view, value}`.
fn worst(rows: &[BTreeMap<String, String>], column: &str) -> Value {
    rows.iter()
        .filter_map(|r| number(r, column).map(|v| (r.get("view").cloned().unwrap_or_default(), v)))
        .max_by(|a, b| a.1.total_cmp(&b.1))
        .map_or(Value::Null, |(view, value)| json!({ "view": view, "value": value }))
}

fn median_of(rows: &[BTreeMap<String, String>], column: &str) -> Value {
    median(rows.iter().filter_map(|r| number(r, column)).collect()).map_or(Value::Null, |v| json!((v * 100.0).round() / 100.0))
}

/// The settings that should be the same in every scene of one baseline; the scenes where they are not.
fn config_differences(first: &Value, other: &Value) -> Vec<String> {
    let mut out = Vec::new();
    for section in ["rhi", "build", "scalability", "cvars"] {
        if let (Some(a), Some(b)) = (first[section].as_object(), other[section].as_object()) {
            for (key, value) in a {
                // The viewport is the one RHI field a window manager may legitimately change.
                if key != "viewport" && b.get(key) != Some(value) {
                    out.push(format!("{section}.{key}"));
                }
            }
        }
    }
    out
}

/// gpu-monitor.tsv as (epoch, other Unreal processes); an absent or unreadable file is an empty monitor.
fn read_monitor(text: &str) -> Vec<(u64, u32)> {
    text.lines()
        .skip(1)
        .filter_map(|line| {
            let fields: Vec<&str> = line.split('\t').collect();
            Some((fields.first()?.parse().ok()?, fields.last()?.trim().parse().ok()?))
        })
        .collect()
}

/// How many monitor samples fell inside the scene's run, and in how many another Unreal process was running.
fn contention(monitor: &[(u64, u32)], start: Option<u64>, end: Option<u64>) -> Value {
    let (Some(start), Some(end)) = (start, end) else { return Value::Null };
    let inside: Vec<u32> = monitor.iter().filter(|(t, _)| (start..=end).contains(t)).map(|(_, n)| *n).collect();
    let busy = inside.iter().filter(|n| **n > 0).count();
    json!({ "samples": inside.len(), "with_other_unreal": busy, "contended": busy > 0 })
}

pub fn summarize(dir: &Path) -> Result<(Value, String, bool), String> {
    let meta = key_values(&read_lossy(&dir.join("meta.env")).ok_or("no meta.env (run scripts/gfx-baseline-linux.sh)")?, false);
    let mut scenes: Vec<_> = std::fs::read_dir(dir)
        .map_err(|e| e.to_string())?
        .filter_map(Result::ok)
        .filter(|e| e.path().join("summary.txt").is_file())
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .collect();
    scenes.sort();
    if scenes.is_empty() {
        return Err("no scene directories with a summary.txt".into());
    }
    let monitor = read_monitor(&read_lossy(&dir.join("gpu-monitor.tsv")).unwrap_or_default());

    let mut ok = true;
    let mut csv_rows: Vec<Vec<String>> = Vec::new();
    let mut columns: Vec<String> = Vec::new();
    let mut scene_out = Vec::new();
    let mut config: Option<Value> = None;
    for scene in &scenes {
        let path = dir.join(scene);
        let summary_text = read_lossy(&path.join("summary.txt")).unwrap_or_default();
        let summary = key_values(&summary_text.lines().take(2).collect::<Vec<_>>().join("\n"), true);
        let rows = read_stats(&read_lossy(&path.join("stats.tsv")).unwrap_or_default());
        let scene_config: Value = read_lossy(&path.join("render_config.json"))
            .and_then(|t| serde_json::from_str(&t).ok())
            .unwrap_or(Value::Null);
        let views: usize = summary.get("views").and_then(|v| v.parse().ok()).unwrap_or(0);
        let shots: usize = summary.get("shots").and_then(|v| v.parse().ok()).unwrap_or(0);
        let complete = views > 0 && shots == views && rows.len() == views && summary.get("client_exit").map(String::as_str) == Some("0");
        ok &= complete;

        let differs = match (&config, &scene_config) {
            (_, Value::Null) => vec!["render_config.json missing".to_string()],
            (None, c) => {
                config = Some(c.clone());
                Vec::new()
            }
            (Some(first), c) => config_differences(first, c),
        };

        if columns.is_empty() && !rows.is_empty() {
            // The CSV keeps stats.tsv's column order (the rows' maps are sorted by name).
            let header = read_lossy(&path.join("stats.tsv")).unwrap_or_default();
            columns = header.lines().next().unwrap_or_default().split('\t').map(str::to_string).collect();
        }
        for row in &rows {
            let mut line = vec![scene.clone()];
            line.extend(columns.iter().map(|c| row.get(c).cloned().unwrap_or_default()));
            csv_rows.push(line);
        }

        let fixed: Vec<_> = rows.iter().filter(|r| r.get("player_view").map(String::as_str) != Some("1")).cloned().collect();
        let scene_meshes = &scene_config["scene"]["static_meshes"];
        scene_out.push(json!({
            "scene": scene,
            "clock": summary.get("clock"),
            "weather": summary.get("weather"),
            "start": summary.get("start"),
            "client_args": summary.get("client_args"),
            "views": views,
            "shots": shots,
            "complete": complete,
            "client_exit": summary.get("client_exit"),
            "contention": contention(&monitor, summary.get("epoch_start").and_then(|v| v.parse().ok()), summary.get("epoch_end").and_then(|v| v.parse().ok())),
            "gpu_at_start": summary_text.lines().find(|l| l.starts_with("gpu_at_start=")).map(|l| l.trim_start_matches("gpu_at_start=")),
            "frame_ms_median": median_of(&fixed, "frame_ms"),
            "frame_p99_worst": worst(&rows, "frame_p99"),
            "game_p95_worst": worst(&rows, "game_p95"),
            "render_p95_worst": worst(&rows, "render_p95"),
            "gpu_p50_median": median_of(&fixed, "gpu_p50"),
            "gpu_p95_worst": worst(&rows, "gpu_p95"),
            "gpu_p99_worst": worst(&rows, "gpu_p99"),
            "vram_used_mb_max": worst(&rows, "vram_used_mb"),
            "resident_mb_max": worst(&rows, "resident_mb"),
            "lights": scene_config["scene"]["lights"],
            "directional_lights": scene_config["scene"]["directional_lights"],
            "lod0_triangles": scene_meshes["lod0_triangles"],
            "config_differs_from_first_scene": differs,
        }));
    }

    let mut csv = String::new();
    let mut header = vec!["scene".to_string()];
    header.extend(columns.iter().cloned());
    for line in std::iter::once(header).chain(csv_rows) {
        csv.push_str(&line.iter().map(|f| if f.contains(',') { format!("\"{f}\"") } else { f.clone() }).collect::<Vec<_>>().join(","));
        csv.push('\n');
    }

    let mode = meta.get("mode").cloned().unwrap_or_default();
    let meta_json: Map<String, Value> = meta.iter().map(|(k, v)| (k.clone(), json!(v))).collect();
    let doc = json!({
        "schema": 1,
        "kind": "marburgumii.gfx00_baseline",
        // GRAPHICS_ROADMAP: editor -game numbers compare with each other, never with a package or a frame budget.
        "evidence_class": if mode == "staged" { "packaged" } else { "editor_game_mode" },
        "complete": ok,
        "meta": meta_json,
        "render_config": config.unwrap_or(Value::Null),
        "scenes": scene_out,
    });
    Ok((doc, csv, ok))
}

pub fn run(dir: &str) -> u8 {
    let dir = Path::new(dir);
    match summarize(dir) {
        Ok((doc, csv, ok)) => {
            let text = serde_json::to_string_pretty(&doc).unwrap_or_default();
            if let Err(error) = std::fs::write(dir.join("baseline.json"), format!("{text}\n")).and_then(|_| std::fs::write(dir.join("baseline.csv"), csv)) {
                eprintln!("gfx-baseline: cannot write into {}: {error}", dir.display());
                return 2;
            }
            for scene in doc["scenes"].as_array().into_iter().flatten() {
                let busy = &scene["contention"];
                println!(
                    "{:<14} views={}/{} frame_ms_median={} gpu_p50_median={} gpu_p99_worst={} vram_max={}{}",
                    scene["scene"].as_str().unwrap_or(""),
                    scene["shots"],
                    scene["views"],
                    scene["frame_ms_median"],
                    scene["gpu_p50_median"],
                    scene["gpu_p99_worst"],
                    scene["vram_used_mb_max"],
                    if busy["contended"] == json!(true) {
                        format!(" CONTENDED ({}/{} samples with another Unreal process)", busy["with_other_unreal"], busy["samples"])
                    } else {
                        String::new()
                    }
                );
            }
            println!("baseline: {} ({})", dir.join("baseline.json").display(), doc["evidence_class"].as_str().unwrap_or(""));
            if ok {
                0
            } else {
                1
            }
        }
        Err(error) => {
            eprintln!("gfx-baseline: {error}");
            2
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const HEADER: &str = "view\tframes\tframe_ms\tgame_ms\trender_ms\tgpu_ms\tdraw_calls\tprimitives\tframe_p50\tframe_p95\tframe_p99\tgame_p95\tgame_p99\trender_p95\trender_p99\trhi_ms\trhi_p95\tgpu_p50\tgpu_p95\tgpu_p99\tvram_used_mb\tvram_budget_mb\ttexture_streaming_mb\ttexture_nonstreaming_mb\tresident_mb\tplayer_view";

    fn row(view: &str, gpu: f64, player: bool) -> String {
        format!("{view}\t200\t6.0\t2.0\t2.0\t{gpu}\t900\t1000000\t5.9\t7.0\t9.5\t2.5\t3.0\t2.5\t3.0\t1.0\t1.2\t{gpu}\t{}\t{}\t2100\t14000\t600\t300\t5200\t{}", gpu + 1.0, gpu + 2.0, u8::from(player))
    }

    fn scene(dir: &Path, name: &str, gpus: &[f64], aa: &str) {
        let path = dir.join(name);
        std::fs::create_dir_all(&path).unwrap();
        let views = gpus.len() + 1;
        std::fs::write(path.join("summary.txt"), format!("mode=editor clock=2026-06-18T10:00:00Z weather=clear_dry start=default settle_s=4 client_exit=0 views={views} shots={views} epoch_start=1000 epoch_end=1100\ngpu_at_start=3 %, 1500 MiB other_unreal_processes=0\n")).unwrap();
        let mut stats = vec![HEADER.to_string(), row("player_hud", 9.0, true)];
        stats.extend(gpus.iter().enumerate().map(|(i, g)| row(&format!("v{i}"), *g, false)));
        std::fs::write(path.join("stats.tsv"), stats.join("\n")).unwrap();
        std::fs::write(path.join("render_config.json"), json!({ "rhi": { "name": "Vulkan", "viewport": "1920x1080" }, "cvars": { "r.AntiAliasingMethod": aa }, "scene": { "static_meshes": { "lod0_triangles": 9.6e6 } } }).to_string()).unwrap();
    }

    #[test]
    fn medians_skip_the_player_view_and_worst_names_the_view() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta.env"), "mode=editor\nsource_sha=abc\n").unwrap();
        scene(dir.path(), "a_noon", &[3.0, 2.0, 8.0], "4");
        let (doc, csv, ok) = summarize(dir.path()).unwrap();
        assert!(ok);
        let noon = &doc["scenes"][0];
        assert_eq!(noon["gpu_p50_median"], json!(3.0), "the HUD view's 9 ms is not part of the median");
        assert_eq!(noon["gpu_p99_worst"]["view"], "player_hud");
        assert_eq!(doc["evidence_class"], "editor_game_mode");
        assert_eq!(csv.lines().count(), 5);
        assert!(csv.starts_with("scene,view,frames,frame_ms"));
    }

    #[test]
    fn a_setting_that_changes_between_scenes_is_named() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta.env"), "mode=staged\n").unwrap();
        scene(dir.path(), "a_noon", &[3.0], "4");
        scene(dir.path(), "b_night", &[3.0], "2");
        let (doc, _, _) = summarize(dir.path()).unwrap();
        assert_eq!(doc["scenes"][1]["config_differs_from_first_scene"], json!(["cvars.r.AntiAliasingMethod"]));
        assert_eq!(doc["evidence_class"], "packaged");
    }

    #[test]
    fn a_missing_shot_makes_the_baseline_incomplete() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta.env"), "mode=editor\n").unwrap();
        scene(dir.path(), "a_noon", &[3.0, 4.0], "4");
        let summary = dir.path().join("a_noon/summary.txt");
        let text = std::fs::read_to_string(&summary).unwrap().replace("shots=3", "shots=2");
        std::fs::write(summary, text).unwrap();
        assert!(!summarize(dir.path()).unwrap().2);
    }

    #[test]
    fn another_unreal_process_inside_the_scene_marks_it_contended() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("meta.env"), "mode=staged\n").unwrap();
        scene(dir.path(), "a_noon", &[3.0], "4");
        // Samples before (990) and after (1110) the scene's run do not count.
        std::fs::write(dir.path().join("gpu-monitor.tsv"), "epoch\tgpu_util\tgpu_mem_mb\tother_unreal\n990\t95\t3000\t3\n1000\t90\t1500\t0\n1050\t92\t3100\t2\n1100\t91\t1500\t0\n1110\t90\t3000\t3\n").unwrap();
        let (doc, _, ok) = summarize(dir.path()).unwrap();
        assert!(ok, "contention is reported, not failed");
        assert_eq!(doc["scenes"][0]["contention"], json!({ "samples": 3, "with_other_unreal": 1, "contended": true }));
    }

    #[test]
    fn the_gfx_views_are_well_formed() {
        for view in views() {
            let parts: Vec<&str> = view.split('@').collect();
            assert_eq!(parts.len(), 4, "{view}");
            assert_eq!(parts[1].split(',').count(), 3);
            assert_eq!(parts[2].split(',').count(), 3);
        }
    }
}
