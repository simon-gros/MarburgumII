//! `mb2 survey-views <walk graph>`: the fixed camera set of the visual survey of /Game/World/lent
//! (scripts/visual-survey-linux.sh, Docs/VISUAL_QUALITY_BACKLOG.md), printed as one MB2ViewShots string
//! (`name@eye@target@fov;...`, Unreal centimetres). The views depend only on the walk graph and the seed, so two
//! builds can be compared shot for shot:
//!
//! * the Lent angler and the Stari most deck;
//! * an oblique aerial over each of the 25 512 m windows (ADR-045, ADR-090), the middle nine first;
//! * three eye-height views per window from the street people's walk graph, each looking 60 m down the pavement it
//!   follows;
//! * the four map edges from 60 m up, looking out at the distant ring (ADR-054).
//!
//! `--windows 9` keeps the camera set of the nine-window map (aerials and edges at ±768 m).

use serde_json::Value;
use std::path::Path;

/// The windows' centres (plan metres east, south). The middle nine of ADR-045 come first, so their views are
/// drawn in the same order as before ADR-090 added the outer ring of sixteen.
const WINDOWS: [(&str, f64, f64); 25] = [
    ("ljudski_vrt", -512.0, -512.0),
    ("center", 0.0, -512.0),
    ("avtobusna", 512.0, -512.0),
    ("pristan", -512.0, 0.0),
    ("lent", 0.0, 0.0),
    ("titov_most", 512.0, 0.0),
    ("kurilnica", -512.0, 512.0),
    ("magdalena", 0.0, 512.0),
    ("pobreska", 512.0, 512.0),
    ("vrbanska", -1024.0, -1024.0),
    ("vinarska", -512.0, -1024.0),
    ("mestni_park", 0.0, -1024.0),
    ("tomsiceva", 512.0, -1024.0),
    ("praprotnikova", 1024.0, -1024.0),
    ("smetanova", -1024.0, -512.0),
    ("zelezniska", 1024.0, -512.0),
    ("studenci", -1024.0, 0.0),
    ("greenwich", 1024.0, 0.0),
    ("preradoviceva", -1024.0, 512.0),
    ("mejna", 1024.0, 512.0),
    ("radvanjska", -1024.0, 1024.0),
    ("cesta_zmage", -512.0, 1024.0),
    ("tabor", 0.0, 1024.0),
    ("nasipna", 512.0, 1024.0),
    ("crnogorska", 1024.0, 1024.0),
];
/// The middle nine windows' map edge (ADR-045) and the five by five's (ADR-090), plan metres.
const HALF_9_M: f64 = 768.0;
const HALF_25_M: f64 = 1280.0;
const EYE_HEIGHT_M: f64 = 1.7;
const PER_WINDOW: usize = 3;
const WALK_M: f64 = 60.0;
const MIN_WALK_M: f64 = 35.0;
/// A walk round a small block can end where it began (ADR-090 tomsiceva), and the camera would look straight down.
const MIN_SIGHT_M: f64 = 20.0;
const BRIDGE_FLAG: i64 = 1;

/// SplitMix64: small, seedable and the same on every platform.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    fn unit(&mut self) -> f64 {
        (self.next() >> 11) as f64 / (1u64 << 53) as f64
    }

    fn shuffle<T>(&mut self, items: &mut [T]) {
        for i in (1..items.len()).rev() {
            let j = (self.next() % (i as u64 + 1)) as usize;
            items.swap(i, j);
        }
    }
}

#[derive(Clone, Copy)]
struct Node {
    east: f64,
    south: f64,
    up: f64,
    flags: i64,
}

/// Plan metres (x east, y south of the middle of Stari most) to Unreal centimetres (X north, Y east).
fn ue(east: f64, south: f64, up: f64) -> [f64; 3] {
    [-south * 100.0, east * 100.0, up * 100.0]
}

fn view(name: &str, eye: [f64; 3], target: [f64; 3], fov: f64) -> String {
    let f = |v: [f64; 3]| format!("{:.0},{:.0},{:.0}", v[0], v[1], v[2]);
    format!("{name}@{}@{}@{fov}", f(eye), f(target))
}

/// Follows the graph from `start` without turning back, always taking the straightest way on, until `distance`
/// metres of path; returns where it ended and how far it got.
fn walk_ahead(
    nodes: &[Node],
    adjacency: &[Vec<usize>],
    start: usize,
    distance: f64,
    rng: &mut Rng,
) -> (usize, f64) {
    let (mut current, mut previous, mut heading, mut walked) =
        (start, usize::MAX, None::<f64>, 0.0);
    while walked < distance {
        let here = nodes[current];
        let mut best: Option<(f64, usize)> = None;
        for &next in &adjacency[current] {
            if next == previous || nodes[next].flags & BRIDGE_FLAG != 0 {
                continue;
            }
            let angle = (nodes[next].south - here.south).atan2(nodes[next].east - here.east);
            let turn = match heading {
                None => rng.unit(),
                Some(h) => ((angle - h + std::f64::consts::PI).rem_euclid(std::f64::consts::TAU)
                    - std::f64::consts::PI)
                    .abs(),
            };
            if best.map_or(true, |(b, _)| turn < b) {
                best = Some((turn, next));
            }
        }
        let Some((_, next)) = best else { break };
        heading = Some((nodes[next].south - here.south).atan2(nodes[next].east - here.east));
        walked += (nodes[next].east - here.east).hypot(nodes[next].south - here.south);
        previous = current;
        current = next;
    }
    (current, walked)
}

pub fn views(graph: &Value, seed: u64, windows: usize) -> Result<Vec<String>, String> {
    let number = |v: &Value| {
        v.as_f64()
            .ok_or_else(|| "a walk-graph node field is not a number".to_string())
    };
    let nodes = graph["nodes"]
        .as_array()
        .ok_or("the walk graph has no nodes")?
        .iter()
        .map(|n| {
            Ok(Node {
                east: number(&n[0])?,
                south: number(&n[1])?,
                up: number(&n[2])?,
                flags: n[3].as_i64().unwrap_or(0),
            })
        })
        .collect::<Result<Vec<_>, String>>()?;
    let mut adjacency = vec![Vec::new(); nodes.len()];
    for edge in graph["edges"]
        .as_array()
        .ok_or("the walk graph has no edges")?
    {
        let (a, b) = (
            edge[0].as_u64().ok_or("bad edge")? as usize,
            edge[1].as_u64().ok_or("bad edge")? as usize,
        );
        if a < nodes.len() && b < nodes.len() {
            adjacency[a].push(b);
            adjacency[b].push(a);
        }
    }
    let mut rng = Rng(seed);
    let mut out = vec![
        view(
            "angler_close",
            [7642.0, 1563.0, 386.0],
            [7042.0, 1813.0, 266.0],
            60.0,
        ),
        // Along the deck from the north bank's end towards the south bank, a little above eye height.
        view(
            "stari_most_deck",
            ue(-1.0, -90.0, 16.5),
            ue(-1.0, 60.0, 15.5),
            70.0,
        ),
    ];
    for &(window, cx, cy) in WINDOWS.iter().take(windows) {
        out.push(view(
            &format!("air_{window}"),
            ue(cx, cy + 300.0, 220.0),
            ue(cx, cy - 40.0, 0.0),
            60.0,
        ));
        let mut inside: Vec<usize> = (0..nodes.len())
            .filter(|&i| {
                let n = nodes[i];
                (n.east - cx).abs() < 230.0
                    && (n.south - cy).abs() < 230.0
                    && n.flags & BRIDGE_FLAG == 0
                    && adjacency[i].len() >= 2
            })
            .collect();
        rng.shuffle(&mut inside);
        let mut taken = 0;
        for start in inside {
            if taken == PER_WINDOW {
                break;
            }
            let (end, walked) = walk_ahead(&nodes, &adjacency, start, WALK_M, &mut rng);
            if walked < MIN_WALK_M {
                continue;
            }
            let (s, e) = (nodes[start], nodes[end]);
            if (e.east - s.east).hypot(e.south - s.south) < MIN_SIGHT_M {
                continue;
            }
            out.push(view(
                &format!("street_{window}_{taken}"),
                ue(s.east, s.south, s.up + EYE_HEIGHT_M),
                ue(e.east, e.south, e.up + 1.2),
                75.0,
            ));
            taken += 1;
        }
    }
    // 28 m inside the edge, looking at the ground 632 m (nine windows) or 1,120 m (25) beyond it; the five by five's
    // north edge runs over the Kalvarija slopes, up to 83 m, so its cameras stand at 120 m.
    let (edge, out_to, up) = if windows <= 9 { (HALF_9_M - 28.0, 1400.0, 60.0) } else { (HALF_25_M - 28.0, 2400.0, 120.0) };
    for (name, east, south, look_east, look_south) in [
        ("edge_north", 0.0, -edge, 0.0, -out_to),
        ("edge_south", 0.0, edge, 0.0, out_to),
        ("edge_west", -edge, 0.0, -out_to, 0.0),
        ("edge_east", edge, 0.0, out_to, 0.0),
    ] {
        out.push(view(
            name,
            ue(east, south, up),
            ue(look_east, look_south, 0.0),
            70.0,
        ));
    }
    Ok(out)
}

pub fn run(graph_path: &str, seed: Option<&str>, windows: Option<&str>) -> u8 {
    let windows = match windows.map(str::parse::<usize>) {
        None => WINDOWS.len(),
        Some(Ok(n @ (9 | 25))) => n,
        Some(_) => {
            eprintln!("survey-views: --windows is 9 or 25");
            return 2;
        }
    };
    let seed = match seed.map(str::parse::<u64>) {
        None => 1990,
        Some(Ok(seed)) => seed,
        Some(Err(_)) => {
            eprintln!("survey-views: the seed must be a whole number");
            return 2;
        }
    };
    let graph = match std::fs::read_to_string(Path::new(graph_path))
        .map_err(|e| e.to_string())
        .and_then(|t| serde_json::from_str::<Value>(&t).map_err(|e| e.to_string()))
    {
        Ok(graph) => graph,
        Err(error) => {
            eprintln!("survey-views: cannot read {graph_path}: {error}");
            return 2;
        }
    };
    match views(&graph, seed, windows) {
        Ok(views) => {
            println!("{}", views.join(";"));
            0
        }
        Err(error) => {
            eprintln!("survey-views: {error}");
            1
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn grid() -> Value {
        // A straight 100 m pavement through the middle of Lent's window, nodes every 10 m.
        let nodes: Vec<Value> = (0..11)
            .map(|i| json!([-50.0 + i as f64 * 10.0, 0.0, 2.0, 0, 0, 0.5]))
            .collect();
        let edges: Vec<Value> = (0..10).map(|i| json!([i, i + 1, 0])).collect();
        json!({ "nodes": nodes, "edges": edges })
    }

    #[test]
    fn the_same_seed_gives_the_same_views() {
        assert_eq!(views(&grid(), 7, 25).unwrap(), views(&grid(), 7, 25).unwrap());
    }

    #[test]
    fn a_street_view_looks_down_the_pavement_at_eye_height() {
        let all = views(&grid(), 1990, 25).unwrap();
        let street: Vec<&String> = all
            .iter()
            .filter(|v| v.starts_with("street_lent_"))
            .collect();
        assert!(!street.is_empty());
        for v in street {
            let parts: Vec<&str> = v.split('@').collect();
            let eye: Vec<f64> = parts[1].split(',').map(|x| x.parse().unwrap()).collect();
            let target: Vec<f64> = parts[2].split(',').map(|x| x.parse().unwrap()).collect();
            assert_eq!(eye[2], 370.0, "eye 1.7 m above the 2 m pavement");
            assert!(
                (eye[1] - target[1]).abs() >= MIN_WALK_M * 100.0 - 1.0,
                "looks at least 35 m along"
            );
            assert_eq!(eye[0], 0.0);
        }
        // 25 aerials, four edges, the angler and the deck, whatever the graph.
        assert_eq!(all.iter().filter(|v| v.starts_with("air_")).count(), 25);
        assert_eq!(all.iter().filter(|v| v.starts_with("edge_")).count(), 4);
    }

    #[test]
    fn the_nine_window_set_is_the_first_nine_aerials_and_the_old_edges() {
        let nine = views(&grid(), 1990, 9).unwrap();
        let all = views(&grid(), 1990, 25).unwrap();
        assert_eq!(nine.iter().filter(|v| v.starts_with("air_")).count(), 9);
        assert_eq!(nine[..nine.len() - 4], all[..nine.len() - 4], "the middle nine come first and draw the same views");
        let edge_north = nine.iter().find(|v| v.starts_with("edge_north@")).unwrap();
        assert!(edge_north.contains("@74000,0,6000@"), "{edge_north}");
    }

    #[test]
    fn a_walk_round_a_block_back_to_its_start_is_not_a_view() {
        // A 15 m square in Lent's window: every walk goes once round it, 60 m, and ends where it began.
        let nodes: Vec<Value> = [(0.0, 0.0), (15.0, 0.0), (15.0, 15.0), (0.0, 15.0)]
            .iter()
            .map(|&(east, south)| json!([east, south, 2.0, 0, 0, 0.5]))
            .collect();
        let graph = json!({ "nodes": nodes, "edges": [[0, 1, 0], [1, 2, 0], [2, 3, 0], [3, 0, 0]] });
        assert!(!views(&graph, 1990, 25).unwrap().iter().any(|v| v.starts_with("street_")));
    }

    #[test]
    fn a_window_without_pavement_has_only_its_aerial() {
        let all = views(&grid(), 1990, 25).unwrap();
        assert!(!all.iter().any(|v| v.starts_with("street_pobreska_")));
    }
}
