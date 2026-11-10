//! Vehicles (ADR-039): the vehicle catalogue and the shared C++ contract carry the same definitions field
//! for field (the original's vehicles.json); every car has exactly one driver's seat and seat ids unique to
//! it; every authored instance names a known definition; the vehicle actor lives in the game module and
//! takes seats and pedals only through the player controller's server RPCs.

use std::collections::BTreeMap;
use std::path::Path;

use regex::Regex;
use serde_json::Value;

use crate::repo::{read, read_json, Problems};

const CATALOGUE: &str = "Data/Gameplay/vehicles.v0.1.json";
const CONTRACT: &str = "Source/MarburgumIIShared/Public/MarburgumVehicleContract.h";
const CONTROLLER: &str = "Source/MarburgumII/Public/MarburgumPlayerController.h";

/// Catalogue key → contract field, for the plain numbers.
const NUMBERS: [(&str, &str); 24] = [
    ("mass", "Mass"),
    ("body_length", "BodyLength"),
    ("body_width", "BodyWidth"),
    ("body_height", "BodyHeight"),
    ("wheelbase", "Wheelbase"),
    ("track_width", "TrackWidth"),
    ("wheel_radius", "WheelRadius"),
    ("engine_force", "EngineForce"),
    ("max_speed", "MaxSpeed"),
    ("reverse_speed", "ReverseSpeed"),
    ("brake_force", "BrakeForce"),
    ("handbrake_force", "HandbrakeForce"),
    ("steering_angle", "SteeringAngle"),
    ("steering_speed", "SteeringSpeed"),
    ("grip", "Grip"),
    ("wet_grip", "WetGrip"),
    ("handbrake_grip", "HandbrakeGrip"),
    ("suspension_rest_length", "SuspensionRestLength"),
    ("suspension_strength", "SuspensionStrength"),
    ("suspension_damping", "SuspensionDamping"),
    ("suspension_travel", "SuspensionTravel"),
    ("drag", "Drag"),
    ("rolling_resistance", "RollingResistance"),
    ("enter_range", "EnterRange"),
];

/// One `{ .Id = ... }` definition from the contract, as field name → raw initializer text.
type Fields = BTreeMap<String, String>;

fn definitions(contract: &str, problems: &mut Problems) -> Vec<Fields> {
    let Some(start) = contract.find("Definitions[] = {") else {
        problems.0.push("contract has no Definitions[] registry".into());
        return Vec::new();
    };
    let body = &contract[start..];
    let end = body.find("\n    };").unwrap_or(body.len());
    let field = Regex::new(r"(?s)\.([A-Za-z]+) = (\{.*?\}\s*,\s*\n|[^,\n]+,)").unwrap();
    let mut out = Vec::new();
    for chunk in body[..end].split("\n        {\n").skip(1) {
        // Seats nest braces; take them separately.
        let (head, seats) = chunk.split_once(".Seats = {").unwrap_or((chunk, ""));
        let mut fields: Fields = field
            .captures_iter(head)
            .map(|c| (c[1].to_string(), c[2].trim().trim_end_matches(',').trim().to_string()))
            .collect();
        fields.insert("Seats".into(), seats.to_string());
        out.push(fields);
    }
    out
}

fn number(text: &str) -> Option<f64> {
    text.trim().parse().ok()
}

fn numbers(text: &str) -> Vec<f64> {
    text.trim_matches(|c| c == '{' || c == '}' || char::is_whitespace(c))
        .split(',')
        .filter_map(number)
        .collect()
}

fn json_numbers(value: &Value) -> Vec<f64> {
    value.as_array().map(|a| a.iter().filter_map(Value::as_f64).collect()).unwrap_or_default()
}

fn close(a: &[f64], b: &[f64]) -> bool {
    a.len() == b.len() && a.iter().zip(b).all(|(x, y)| (x - y).abs() < 1e-9)
}

/// `{ TEXT("id"), TEXT("role"), { offset }, { door } }` rows.
fn seats(text: &str) -> Vec<(String, String, Vec<f64>, Vec<f64>)> {
    let row = Regex::new(r#"\{\s*TEXT\("([a-z_]+)"\),\s*TEXT\("([a-z]+)"\),\s*(\{[^}]*\}),\s*(\{[^}]*\})\s*\}"#).unwrap();
    row.captures_iter(text).map(|c| (c[1].to_string(), c[2].to_string(), numbers(&c[3]), numbers(&c[4]))).collect()
}

fn quoted(text: &str) -> String {
    Regex::new(r#"TEXT\("([^"]*)"\)"#).unwrap().captures(text).map(|c| c[1].to_string()).unwrap_or_default()
}

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let catalogue = read_json(root, CATALOGUE, problems);
    let contract = read(root, CONTRACT, problems);
    let controller = read(root, CONTROLLER, problems);
    if catalogue.is_null() {
        return Vec::new();
    }
    let json: Vec<&Value> = catalogue["vehicles"].as_array().map(|a| a.iter().collect()).unwrap_or_default();
    let cpp = definitions(&contract, problems);
    let json_ids: Vec<&str> = json.iter().map(|v| v["id"].as_str().unwrap_or("")).collect();
    let cpp_ids: Vec<String> = cpp.iter().map(|f| quoted(f.get("Id").map(String::as_str).unwrap_or(""))).collect();
    problems.expect(json_ids == cpp_ids, format!("definition order mismatch: catalogue {json_ids:?} vs contract {cpp_ids:?}"));

    let mut seat_total = 0;
    for (vehicle, fields) in json.iter().zip(&cpp) {
        let id = vehicle["id"].as_str().unwrap_or("?");
        let get = |name: &str| fields.get(name).map(String::as_str).unwrap_or("");
        problems.expect(vehicle["name"].as_str() == Some(quoted(get("Name")).as_str()), format!("{id}: name differs"));
        problems.expect(vehicle["class"].as_str() == Some(quoted(get("Class")).as_str()), format!("{id}: class differs"));
        for (key, field) in NUMBERS {
            let want = vehicle[key].as_f64();
            let have = number(get(field));
            problems.expect(want.is_some() && want == have, format!("{id}: {key} catalogue {want:?} vs contract {field} {have:?}"));
        }
        problems.expect(close(&json_numbers(&vehicle["centre_of_mass"]), &numbers(get("CentreOfMass"))), format!("{id}: centre_of_mass differs"));
        let front = vehicle["driven_wheels"].as_str() == Some("front");
        problems.expect(get("bFrontWheelDrive") == if front { "true" } else { "false" }, format!("{id}: driven_wheels differs"));

        let json_seats = vehicle["seats"].as_array().cloned().unwrap_or_default();
        let cpp_seats = seats(get("Seats"));
        problems.expect(json_seats.len() == cpp_seats.len(), format!("{id}: {} seats in the catalogue, {} in the contract", json_seats.len(), cpp_seats.len()));
        problems.expect(number(get("SeatCount")) == Some(cpp_seats.len() as f64), format!("{id}: SeatCount is not its seat rows"));
        problems.expect(cpp_seats.len() <= 4, format!("{id}: more than MaxSeats (4) seats"));
        for (seat, (cpp_id, role, offset, door)) in json_seats.iter().zip(&cpp_seats) {
            problems.expect(seat["id"].as_str() == Some(cpp_id) && seat["role"].as_str() == Some(role)
                && close(&json_numbers(&seat["offset"]), offset) && close(&json_numbers(&seat["door"]), door),
                format!("{id}: seat {cpp_id} differs"));
        }
        let drivers = cpp_seats.iter().filter(|s| s.1 == "driver").count();
        problems.expect(drivers == 1, format!("{id}: {drivers} driver's seats (want exactly 1)"));
        let mut unique: Vec<&String> = cpp_seats.iter().map(|s| &s.0).collect();
        unique.sort();
        unique.dedup();
        problems.expect(unique.len() == cpp_seats.len(), format!("{id}: duplicate seat ids"));
        problems.expect(cpp_seats.iter().all(|s| s.1 == "driver" || s.1 == "passenger"), format!("{id}: unknown seat role"));
        problems.expect(vehicle["model"].is_object(), format!("{id}: no model block for the bodywork commandlet"));
        seat_total += cpp_seats.len();
    }

    let instances = catalogue["instances"].as_array().cloned().unwrap_or_default();
    for instance in &instances {
        let id = instance["id"].as_str().unwrap_or("?");
        let definition = instance["definition"].as_str().unwrap_or("");
        problems.expect(json_ids.contains(&definition), format!("instance {id}: unknown definition {definition}"));
        problems.expect(json_numbers(&instance["position_m"]).len() == 3, format!("instance {id}: position_m is not 3 numbers"));
    }
    problems.expect(instances.iter().any(|i| i["id"] == "lent_hatchback"), "no lent_hatchback instance");
    problems.expect(contract.contains("PrototypeDefinitionId = TEXT(\"city_hatchback_a\")"), "prototype is not city_hatchback_a");

    // Seats and pedals only through the controller's server RPCs.
    for rpc in ["void ServerEnterVehicle(", "void ServerExitVehicle(", "void ServerSetVehicleInput("] {
        problems.expect(controller.contains(rpc), format!("controller has no {rpc}...)"));
    }
    problems.expect(controller.contains("UFUNCTION(Server, Unreliable)\n    void ServerSetVehicleInput("), "pedal intent is not an unreliable server RPC");

    vec![format!(
        "Vehicle contract OK: {} definitions ({} seats) match {CATALOGUE}; {} authored instance(s).",
        cpp.len(),
        seat_total,
        instances.len()
    )]
}
