//! General/private chat: server-owned routing, privacy and early UI, checked in source only.

use std::collections::HashMap;
use std::path::Path;

use regex::Regex;
use serde_json::Value;

use crate::repo::{read, Problems};

pub fn check(root: &Path, problems: &mut Problems) -> Vec<String> {
    let mut file = |path: &str| read(root, path, problems);
    let source = file("Source/MarburgumII/Private/MarburgumPlayerController.cpp");
    let header = file("Source/MarburgumII/Public/MarburgumPlayerController.h");
    let types = file("Source/MarburgumII/Public/MarburgumChatTypes.h");
    let policy = file("Source/MarburgumII/Private/MarburgumChatPolicy.cpp");
    let policy_header = file("Source/MarburgumII/Public/MarburgumChatPolicy.h");
    let test = file("Source/MarburgumII/Private/Tests/MarburgumChatPolicyTest.cpp");
    let module = file("Source/MarburgumIIClient/Private/MarburgumIIClientModule.cpp");
    let view = file("Source/MarburgumIIClient/Private/MarburgumChatWidget.cpp");
    let ui = file("Source/MarburgumIIClient/Private/UI/MarburgumUISubsystem.cpp");
    let client_rules = file("Source/MarburgumIIClient/MarburgumIIClient.Build.cs");
    let game_rules = file("Source/MarburgumII/MarburgumII.Build.cs");
    let network = file("Docs/NETWORK_ARCHITECTURE.md");
    let design = file("Docs/CHAT.md");
    let ontology_text = file("Data/Ontology/ui_hud.v0.1.json");
    let contract_text = file("Data/Chat/chat.v0.1.json");

    let mut expect = |condition: bool, message: &str| problems.expect(condition, message);
    expect(header.contains("UFUNCTION(Server, Reliable)"), "client-to-server request must be owned RPC");
    expect(header.contains("UFUNCTION(Client, Reliable)"), "server-to-client delivery must use owned RPC");
    expect(source.contains("PlayerState->GetPlayerId()"), "sender identity must derive from PlayerState");
    expect(source.contains("PrivateRecipient->ClientReceiveChat(Message)"), "private delivery must address only selected PlayerController");
    expect(source.contains("ClientReceiveChat(Message)"), "server must echo accepted message to sender");
    expect(source.contains("GetPlayerControllerIterator()"), "general delivery must route to connected controllers");
    expect(source.contains("SendSystemNotice"), "invalid requests must return a server refusal");
    expect(source.contains("SanitizeMessage(RawText, Text)"), "text validation must be on server");
    expect(source.contains("AcceptedTimestamps"), "chat acceptance must have sliding rate limit");
    expect(source.contains("LastSystemNoticeAt"), "invalid-request feedback must be throttled");
    expect(!header.contains("NetMulticast") && !source.contains("NetMulticast"), "private content must never use NetMulticast RPC");
    expect(!header.contains("UPROPERTY(Replicated"), "private history cannot be replicated");
    expect(!types.contains("UPROPERTY(Replicated"), "private message struct must not be replicated");
    let logs = Regex::new(r"(?s)UE_LOG\((.*?)\);").unwrap();
    expect(logs.captures_iter(&source).all(|c| !c[1].contains("Message.Text") && !c[1].contains("RawText")),
        "chat bodies must not appear in log statements");
    for name in ["General", "Private", "System"] {
        expect(types.contains(name), &format!("missing chat channel: {name}"));
    }
    expect(policy_header.contains("MaxMessageCharacters = 240"), "message size cap must be 240 code units");
    expect(policy.contains("0x202E") && policy.contains("0x2069"), "text policy must reject bidi control characters");
    expect(test.contains("Marburgum.Chat.TextPolicy"), "native Unreal message policy test missing");
    expect(module.contains("MountChat("), "client module must forward local-player readiness to the chat mount");
    expect(ui.contains("CreateWidget<UMarburgumChatWidget>"), "client must actually mount its chat widget");
    expect(ui.contains("AddToLayer(EMarburgumUILayer::Hud, Chat)"), "chat widget must be attached to the HUD layer of the UI root");
    expect(client_rules.contains("MarburgumIIShared") && client_rules.contains("\"MarburgumII\""), "client module must link common runtime contracts");
    expect(client_rules.contains("\"UMG\""), "UMG dependency must remain client-only");
    expect(!game_rules.contains("\"UMG\""), "dedicated-server common module cannot link UMG");
    expect(view.contains("PRIVATE") && view.contains("ONLINE") && view.contains("HandleTextCommitted"),
        "native UI must expose private/general composition and player discovery");
    expect(design.to_lowercase().contains("personal") && design.to_lowercase().contains("three"),
        "real 3-client test and owner acceptance must remain specified");
    expect(network.contains("CHAT.md"), "network architecture must reference chat contract");

    match (serde_json::from_str::<Value>(&contract_text), serde_json::from_str::<Value>(&ontology_text)) {
        (Ok(spec), Ok(ui)) => {
            let channels: HashMap<&str, &Value> = spec["channels"].as_array().into_iter().flatten()
                .map(|ch| (ch["id"].as_str().unwrap_or(""), ch)).collect();
            let mut ids: Vec<_> = channels.keys().copied().collect();
            ids.sort_unstable();
            expect(ids == ["general", "private", "system"], "chat catalogue must list exactly general, private, system");
            expect(channels.get("private").map(|ch| ch["other_clients_can_observe"] == false).unwrap_or(false),
                "private channel must be restricted to sender and target");
            expect(spec["message"]["max_code_units"] == 240, "chat contract size cap mismatches C++");
            let chat_panel = ui["module_registry"].as_array().into_iter().flatten().find(|m| m["id"] == "chat_panel");
            expect(chat_panel.map(|m| m["stage"] == "UI-03").unwrap_or(false), "chat must appear in early UI-03 stage");
            expect(chat_panel.and_then(|m| m["required_signals"].as_array())
                .map(|s| s.iter().any(|x| x == "private_chat_event")).unwrap_or(false),
                "chat panel must consume private chat signal");
            let private_signal = ui["semantic_signals"].as_array().into_iter().flatten().find(|s| s["id"] == "private_chat_event");
            expect(private_signal.map(|s| s["authority"] == "authoritative_unicast").unwrap_or(false),
                "private UI messages must not use globally replicated signal");
            let gates = &spec["evidence_gates"];
            expect(gates.to_string().to_lowercase().contains("privacy")
                || gates.as_array().map(|g| g.iter().any(|x| x == "private_only_sender_recipient")).unwrap_or(false),
                "privacy negative-test gate missing");
        }
        (Err(error), _) | (_, Err(error)) => problems.0.push(format!("invalid JSON: {error}")),
    }

    vec![
        "Chat source contract consistent (channels, server-owned routing, early UI, privacy).".to_string(),
        "SOURCE-ONLY VALIDATION: no UE build, RPC transport or owner approval was tested.".to_string(),
    ]
}
