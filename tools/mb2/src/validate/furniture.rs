//! Street furniture (GFX-01): the furniture file tools/lent-ground writes and the shared C++ reader agree on its
//! format, version, pieces and sources, in order; every piece is a well-formed row in the square; every piece has its
//! mesh in the content; and the file is staged and the kit cooked into packaged builds, since server and client both
//! put the furniture up from them.

use crate::repo::{read, read_json, Problems};

const FURNITURE: &str = "Content/Marburgum/Furniture/lent_street_furniture.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumStreetFurniture.h";
const READER: &str = "Source/MarburgumIIShared/Private/MarburgumStreetFurniture.cpp";
const GAME_INI: &str = "Config/DefaultGame.ini";
/// The kit's asset names, in the contract's EPiece order.
const MESHES: [&str; 7] = ["StopPost", "Shelter", "Bench", "FencePanel", "WallPanel", "Bollard", "QuayRailing"];
/// Half the side of the ground's square, metres (lent-ground's default extent).
const HALF_M: f64 = 1280.0;

/// The enumerators of `enum class <name>`, lower-case and joined with underscores at word breaks, without `Count`.
fn enumerators(contract: &str, name: &str) -> Vec<String> {
    let start = format!("enum class {name} : uint8");
    let Some(at) = contract.find(&start) else {
        return Vec::new();
    };
    let body = &contract[at..];
    let (Some(open), Some(close)) = (body.find('{'), body.find('}')) else {
        return Vec::new();
    };
    let mut out = Vec::new();
    for line in body[open + 1..close].lines() {
        let line = line.trim();
        if line.starts_with("/**") || line.starts_with('*') || line.starts_with("//") || line.is_empty() {
            continue;
        }
        let ident = line.trim_end_matches(',');
        if ident == "Count" {
            continue;
        }
        let mut snake = String::new();
        for (i, c) in ident.chars().enumerate() {
            if c.is_uppercase() && i > 0 {
                snake.push('_');
            }
            snake.push(c.to_ascii_lowercase());
        }
        out.push(snake);
    }
    out
}

pub fn check(root: &std::path::Path, problems: &mut Problems) -> Vec<String> {
    let file = read_json(root, FURNITURE, problems);
    let contract = read(root, CONTRACT, problems);
    let reader = read(root, READER, problems);
    let ini = read(root, GAME_INI, problems);

    problems.expect(
        file["format"].as_str() == Some("marburgumii.street_furniture"),
        format!("{FURNITURE} is not a street furniture file"),
    );
    problems.expect(
        reader.contains("\"marburgumii.street_furniture\"") && reader.contains("Version != 1"),
        "the C++ reader does not read format marburgumii.street_furniture version 1",
    );
    problems.expect(
        file["version"].as_i64() == Some(1),
        format!("{FURNITURE} is not version 1"),
    );
    problems.expect(
        contract.contains("Marburgum/Furniture/lent_street_furniture.json"),
        "the contract does not name the furniture file",
    );
    let names = |v: &serde_json::Value| -> Vec<String> {
        v.as_array()
            .map(|a| a.iter().filter_map(|s| s.as_str().map(str::to_string)).collect())
            .unwrap_or_default()
    };
    let pieces = names(&file["pieces"]);
    let sources = names(&file["sources"]);
    let contract_pieces = enumerators(&contract, "EPiece");
    let contract_sources = enumerators(&contract, "ESource");
    problems.expect(
        !pieces.is_empty() && pieces == contract_pieces,
        format!("furniture pieces: file {pieces:?} vs contract {contract_pieces:?}"),
    );
    problems.expect(
        !sources.is_empty() && sources == contract_sources,
        format!("furniture sources: file {sources:?} vs contract {contract_sources:?}"),
    );

    let rows = file["furniture"].as_array().cloned().unwrap_or_default();
    let mut bad = 0usize;
    let mut outside = 0usize;
    let mut by_piece = vec![0usize; pieces.len()];
    for row in &rows {
        let values: Vec<f64> = row
            .as_array()
            .map(|r| r.iter().filter_map(|v| v.as_f64()).collect())
            .unwrap_or_default();
        if values.len() != 6 || values.iter().any(|v| !v.is_finite()) {
            bad += 1;
            continue;
        }
        let (piece, source) = (values[3], values[5]);
        if piece.fract() != 0.0
            || source.fract() != 0.0
            || piece < 0.0
            || source < 0.0
            || piece as usize >= pieces.len()
            || source as usize >= sources.len()
        {
            bad += 1;
            continue;
        }
        by_piece[piece as usize] += 1;
        if values[0].abs() > HALF_M || values[1].abs() > HALF_M {
            outside += 1;
        }
    }
    problems.expect(
        bad == 0,
        format!("{bad} pieces are not [east, south, top, piece, yaw, source] rows"),
    );
    problems.expect(
        outside == 0,
        format!("{outside} pieces stand outside the ±{HALF_M} m square"),
    );
    problems.expect(rows.len() >= 500, format!("only {} pieces", rows.len()));
    problems.expect(
        file["summary"]["pieces"].as_u64() == Some(rows.len() as u64),
        "the furniture summary does not match its rows",
    );
    for (name, count) in MESHES.iter().zip(&by_piece) {
        let mesh = format!("Content/World/StreetFurniture/SM_Street_{name}_01.uasset");
        problems.expect(
            *count == 0 || root.join(&mesh).is_file(),
            format!("{count} pieces of {name}, but no {mesh}"),
        );
    }
    problems.expect(
        ini.contains("+DirectoriesToAlwaysStageAsUFS=(Path=\"Marburgum/Furniture\")"),
        "the furniture file is not staged into packaged builds",
    );
    problems.expect(
        ini.contains("+DirectoriesToAlwaysCook=(Path=\"/Game/World/StreetFurniture\")"),
        "the street kit is not always cooked (it is loaded by path)",
    );

    vec![format!(
        "Street furniture OK: {} pieces ({}) in {FURNITURE}, pieces and sources match {CONTRACT}, staged and cooked.",
        rows.len(),
        pieces
            .iter()
            .zip(&by_piece)
            .map(|(k, n)| format!("{n} {k}"))
            .collect::<Vec<_>>()
            .join(", ")
    )]
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_enumerators_as_snake_case() {
        let c = "enum class EPiece : uint8\n{\n    /** The post. */\n    StopPost,\n    // a wall\n    WallPanel,\n    Count,\n};";
        assert_eq!(enumerators(c, "EPiece"), vec!["stop_post", "wall_panel"]);
        assert!(enumerators(c, "ESource").is_empty());
    }
}
