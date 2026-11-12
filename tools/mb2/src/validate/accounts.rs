//! Accounts (ADR-055): the character-model catalogue the account service loads and the shared C++ roster the
//! creator, the server and the character read name the same models, in the same order, with the same meshes;
//! every mesh is in Content; the policy limits the client shows match the service's.

use std::path::Path;

use regex::Regex;

use crate::repo::{read, read_json, Problems};

const CATALOGUE: &str = "Data/Accounts/character_models.v0.1.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumAccountContract.h";
const POLICY: &str = "services/accounts/src/policy.rs";

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let catalogue = read_json(root, CATALOGUE, problems);
    let contract = read(root, CONTRACT, problems);
    let policy = read(root, POLICY, problems);
    let entry = Regex::new(r#"\{ TEXT\("([a-z_]+)"\), TEXT\("[^"]*"\), TEXT\("[^"]*"\), TEXT\("([^"]+)"\) \}"#).unwrap();
    let in_contract: Vec<(String, String)> = entry.captures_iter(&contract).map(|c| (c[1].to_string(), c[2].to_string())).collect();
    let in_catalogue: Vec<(String, String)> = catalogue["models"].as_array().into_iter().flatten()
        .map(|m| (m["id"].as_str().unwrap_or("").to_string(), m["mesh"].as_str().unwrap_or("").to_string()))
        .collect();
    problems.expect(!in_catalogue.is_empty(), format!("{CATALOGUE}: no models"));
    problems.expect(in_contract == in_catalogue, format!("{CONTRACT} roster {in_contract:?} differs from {CATALOGUE} {in_catalogue:?}"));
    for (id, mesh) in &in_catalogue {
        // /Game/X/Y.Y → Content/X/Y.uasset
        let package = mesh.split('.').next().unwrap_or("").trim_start_matches("/Game/");
        problems.expect(root.join("Content").join(format!("{package}.uasset")).is_file(), format!("model {id}: no asset for {mesh}"));
    }
    for (constant, rust) in [("NameMin", "NAME_MIN"), ("NameMax", "NAME_MAX"), ("UsernameMin", "USERNAME_MIN"),
        ("UsernameMax", "USERNAME_MAX"), ("PasswordMin", "PASSWORD_MIN"), ("PasswordMax", "PASSWORD_MAX")]
    {
        let cpp = Regex::new(&format!(r"int32 {constant} = (\d+);")).unwrap().captures(&contract).map(|c| c[1].to_string());
        let service = Regex::new(&format!(r"const {rust}: usize = (\d+);")).unwrap().captures(&policy).map(|c| c[1].to_string());
        problems.expect(cpp.is_some() && cpp == service, format!("{constant}: contract {cpp:?}, service {service:?}"));
    }
    vec![format!("Account contract check passed: {} character model(s), roster and limits match the account service.", in_catalogue.len())]
}
