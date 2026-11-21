//! Trees and bushes (GFX-01): the plants file tools/lent-ground writes and the shared C++ reader agree on its format
//! and version; every plant is a well-formed row in the square, of a species the file lists; every species drawn has
//! its mesh in the content; and the file is staged and the meshes cooked into packaged builds, since server and client
//! both put the plants up from them.

use crate::repo::{read, read_json, Problems};

const PLANTS: &str = "Content/Marburgum/Vegetation/lent_plants.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumVegetation.h";
const READER: &str = "Source/MarburgumIIShared/Private/MarburgumVegetation.cpp";
const GAME_INI: &str = "Config/DefaultGame.ini";
/// Half the side of the ground's square, metres (lent-ground's default extent).
const HALF_M: f64 = 768.0;

pub fn check(root: &std::path::Path, problems: &mut Problems) -> Vec<String> {
    let plants = read_json(root, PLANTS, problems);
    let contract = read(root, CONTRACT, problems);
    let reader = read(root, READER, problems);
    let ini = read(root, GAME_INI, problems);

    problems.expect(
        plants["format"].as_str() == Some("marburgumii.vegetation"),
        format!("{PLANTS} is not a vegetation file"),
    );
    problems.expect(
        reader.contains("\"marburgumii.vegetation\"") && reader.contains("Version != 1"),
        "the C++ reader does not read format marburgumii.vegetation version 1",
    );
    problems.expect(
        plants["version"].as_i64() == Some(1),
        format!("{PLANTS} is not version 1"),
    );
    problems.expect(
        contract.contains("Marburgum/Vegetation/lent_plants.json"),
        "the contract does not name the plants file",
    );
    problems.expect(
        plants["rules"]["only_on_grass"].as_bool() == Some(true),
        format!("{PLANTS} was not placed by the only-on-grass rule"),
    );

    let species: Vec<String> = plants["species"]
        .as_array()
        .map(|a| {
            a.iter()
                .filter_map(|s| s["id"].as_str().map(str::to_string))
                .collect()
        })
        .unwrap_or_default();
    problems.expect(!species.is_empty(), format!("{PLANTS} lists no species"));

    let rows = plants["plants"].as_array().cloned().unwrap_or_default();
    let mut bad = 0usize;
    let mut outside = 0usize;
    let mut by_species = vec![0usize; species.len()];
    for row in &rows {
        let values: Vec<f64> = row
            .as_array()
            .map(|r| r.iter().filter_map(|v| v.as_f64()).collect())
            .unwrap_or_default();
        if values.len() != 7 || values.iter().any(|v| !v.is_finite()) {
            bad += 1;
            continue;
        }
        let kind = values[3];
        if kind.fract() != 0.0
            || kind < 0.0
            || kind as usize >= species.len()
            || values[5] <= 0.0
            || values[6] <= 0.0
        {
            bad += 1;
            continue;
        }
        by_species[kind as usize] += 1;
        if values[0].abs() > HALF_M || values[1].abs() > HALF_M {
            outside += 1;
        }
    }
    problems.expect(
        bad == 0,
        format!("{bad} plants are not [east, south, ground, species, yaw, spread, grow] rows"),
    );
    problems.expect(
        outside == 0,
        format!("{outside} plants stand outside the ±{HALF_M} m square"),
    );
    problems.expect(rows.len() >= 1000, format!("only {} plants", rows.len()));
    problems.expect(
        plants["summary"]["plants"].as_u64() == Some(rows.len() as u64),
        "the plants summary does not match its rows",
    );
    for (id, count) in species.iter().zip(&by_species) {
        let mesh = format!("Content/Vegetation/Species/{id}/SM_{id}.uasset");
        problems.expect(
            *count == 0 || root.join(&mesh).is_file(),
            format!("{count} plants of {id}, but no {mesh}"),
        );
    }
    problems.expect(
        ini.contains("+DirectoriesToAlwaysStageAsUFS=(Path=\"Marburgum/Vegetation\")"),
        "the plants file is not staged into packaged builds",
    );
    problems.expect(
        ini.contains("+DirectoriesToAlwaysCook=(Path=\"/Game/Vegetation\")"),
        "the species meshes are not always cooked (they are loaded by path)",
    );

    vec![format!(
        "Plants OK: {} plants of {} species in {PLANTS}, every species' mesh present, staged and cooked.",
        rows.len(),
        by_species.iter().filter(|n| **n > 0).count()
    )]
}
