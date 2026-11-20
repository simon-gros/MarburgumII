//! `mb2 survey-views <walk graph>`: the fixed camera set of the visual survey of /Game/World/lent
//! (scripts/visual-survey-linux.sh, Docs/VISUAL_QUALITY_BACKLOG.md), printed as one MB2ViewShots string
//! (`name@eye@target@fov;...`, Unreal centimetres). The views depend only on the walk graph and the seed, so two
//! builds can be compared shot for shot:
//!
//! * the Lent angler and the Stari most deck;
//! * an oblique aerial over each of the nine 512 m windows (ADR-045);
//! * three eye-height views per window from the street people's walk graph, each looking 60 m down the pavement it
//!   follows;
//! * the four map edges from 60 m up, looking out at the distant ring (ADR-054).

use serde_json::Value;
use std::path::Path;

const WINDOWS: [(&str, f64, f64); 9] = [
    ("ljudski_vrt", -512.0, -512.0),
    ("center", 0.0, -512.0),
    ("avtobusna", 512.0, -512.0),
    ("pristan", -512.0, 0.0),
    ("lent", 0.0, 0.0),
    ("titov_most", 512.0, 0.0),
    ("kurilnica", -512.0, 512.0),
    ("magdalena", 0.0, 512.0),
    ("pobreska", 512.0, 512.0),
];
const EYE_HEIGHT_M: f64 = 1.7;
const PER_WINDOW: usize = 3;
const WALK_M: f64 = 60.0;
const MIN_WALK_M: f64 = 35.0;
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

pub fn views(graph: &Value, seed: u64) -> Result<Vec<String>, String> {
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
    for (window, cx, cy) in WINDOWS {
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
            out.push(view(
                &format!("street_{window}_{taken}"),
                ue(s.east, s.south, s.up + EYE_HEIGHT_M),
                ue(e.east, e.south, e.up + 1.2),
                75.0,
            ));
            taken += 1;
        }
    }
    for (name, east, south, look_east, look_south) in [
        ("edge_north", 0.0, -740.0, 0.0, -1400.0),
        ("edge_south", 0.0, 740.0, 0.0, 1400.0),
        ("edge_west", -740.0, 0.0, -1400.0, 0.0),
        ("edge_east", 740.0, 0.0, 1400.0, 0.0),
    ] {
        out.push(view(
            name,
            ue(east, south, 60.0),
            ue(look_east, look_south, 0.0),
            70.0,
        ));
    }
    Ok(out)
}

pub fn run(graph_path: &str, seed: Option<&str>) -> u8 {
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
    match views(&graph, seed) {
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
        assert_eq!(views(&grid(), 7).unwrap(), views(&grid(), 7).unwrap());
    }

    #[test]
    fn a_street_view_looks_down_the_pavement_at_eye_height() {
        let all = views(&grid(), 1990).unwrap();
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
        // Nine aerials, four edges, the angler and the deck, whatever the graph.
        assert_eq!(all.iter().filter(|v| v.starts_with("air_")).count(), 9);
        assert_eq!(all.iter().filter(|v| v.starts_with("edge_")).count(), 4);
    }

    #[test]
    fn a_window_without_pavement_has_only_its_aerial() {
        let all = views(&grid(), 1990).unwrap();
        assert!(!all.iter().any(|v| v.starts_with("street_pobreska_")));
    }
}
