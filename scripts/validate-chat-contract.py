#!/usr/bin/env python3
"""Source consistency checks only; never substitute for native Unreal client/server tests."""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
ERRORS = []

def check(condition, message):
    if not condition:
        ERRORS.append(message)

def file(path):
    item = ROOT / path
    check(item.is_file(), f"missing: {path}")
    return item.read_text(encoding="utf-8") if item.is_file() else ""

def main():
    source = file("Source/MarburgumII/Private/MarburgumPlayerController.cpp")
    header = file("Source/MarburgumII/Public/MarburgumPlayerController.h")
    types = file("Source/MarburgumII/Public/MarburgumChatTypes.h")
    policy = file("Source/MarburgumII/Private/MarburgumChatPolicy.cpp")
    test = file("Source/MarburgumII/Private/Tests/MarburgumChatPolicyTest.cpp")
    module = file("Source/MarburgumIIClient/Private/MarburgumIIClientModule.cpp")
    view = file("Source/MarburgumIIClient/Private/MarburgumChatWidget.cpp")
    client_rules = file("Source/MarburgumIIClient/MarburgumIIClient.Build.cs")
    game_rules = file("Source/MarburgumII/MarburgumII.Build.cs")
    network = file("Docs/NETWORK_ARCHITECTURE.md")
    design = file("Docs/CHAT.md")
    ontology_text = file("Data/Ontology/ui_hud.v0.1.json")
    contract_text = file("Data/Chat/chat.v0.1.json")

    check("UFUNCTION(Server, Reliable)" in header, "client-to-server request must be owned RPC")
    check("UFUNCTION(Client, Reliable)" in header, "server-to-client delivery must use owned RPC")
    check("PlayerState->GetPlayerId()" in source, "sender identity must derive from PlayerState")
    check("PrivateRecipient->ClientReceiveChat(Message)" in source,
          "private delivery must address only selected PlayerController")
    check("ClientReceiveChat(Message)" in source, "server must echo accepted message to sender")
    check("GetPlayerControllerIterator()" in source, "general delivery must route to connected controllers")
    check("SendSystemNotice" in source, "invalid requests must return a server refusal")
    check("SanitizeMessage(RawText, Text)" in source, "text validation must be on server")
    check("AcceptedTimestamps" in source, "chat acceptance must have sliding rate limit")
    check("LastSystemNoticeAt" in source, "invalid-request feedback must be throttled")
    check("NetMulticast" not in header and "NetMulticast" not in source,
          "private content must never use NetMulticast RPC")
    check("UPROPERTY(Replicated" not in header, "private history cannot be replicated")
    check("UPROPERTY(Replicated" not in types, "private message struct must not be replicated")
    check("Message.Text" not in source.split("UE_LOG(", 1)[-1] if "UE_LOG(" in source else True,
          "chat bodies must not appear in log statements")
    for name in ("General", "Private", "System"):
        check(name in types, f"missing chat channel: {name}")
    check("MaxMessageCharacters = 240" in file("Source/MarburgumII/Public/MarburgumChatPolicy.h"),
          "message size cap must be 240 code units")
    check("0x202E" in policy and "0x2069" in policy, "text policy must reject bidi control characters")
    check("Marburgum.Chat.TextPolicy" in test, "native Unreal message policy test missing")
    ui = file("Source/MarburgumIIClient/Private/UI/MarburgumUISubsystem.cpp")
    check("MountChat(" in module, "client module must forward local-player readiness to the chat mount")
    check("CreateWidget<UMarburgumChatWidget>" in ui, "client must actually mount its chat widget")
    check("AddToLayer(EMarburgumUILayer::Hud, Chat)" in ui, "chat widget must be attached to the HUD layer of the UI root")
    check("MarburgumIIShared" in client_rules and '"MarburgumII"' in client_rules,
          "client module must link common runtime contracts")
    check('"UMG"' in client_rules, "UMG dependency must remain client-only")
    check('"UMG"' not in game_rules, "dedicated-server common module cannot link UMG")
    check("PRIVATE" in view and "ONLINE" in view and "HandleTextCommitted" in view,
          "native UI must expose private/general composition and player discovery")
    check("personal" in design.lower() and "three" in design.lower(),
          "real 3-client test and owner acceptance must remain specified")
    check("CHAT.md" in network, "network architecture must reference chat contract")

    try:
        spec = json.loads(contract_text)
        ui = json.loads(ontology_text)
    except json.JSONDecodeError as exc:
        ERRORS.append(f"invalid JSON: {exc}")
        spec = ui = {}
    if spec and ui:
        channels = {ch["id"]: ch for ch in spec.get("channels", [])}
        check({"general", "private", "system"} == set(channels),
              "chat catalogue must list exactly general, private, system")
        check(channels.get("private", {}).get("other_clients_can_observe") is False,
              "private channel must be restricted to sender and target")
        check(spec.get("message", {}).get("max_code_units") == 240,
              "chat contract size cap mismatches C++")
        modules = {m["id"]: m for m in ui.get("module_registry", [])}
        check(modules.get("chat_panel", {}).get("stage") == "UI-03",
              "chat must appear in early UI-03 stage")
        check("private_chat_event" in modules.get("chat_panel", {}).get("required_signals", []),
              "chat panel must consume private chat signal")
        signals = {s["id"]: s for s in ui.get("semantic_signals", [])}
        check(signals.get("private_chat_event", {}).get("authority") == "authoritative_unicast",
              "private UI messages must not use globally replicated signal")
        check("privacy" in json.dumps(spec.get("evidence_gates", [])).lower() or
              "private_only_sender_recipient" in spec.get("evidence_gates", []),
              "privacy negative-test gate missing")
    if ERRORS:
        for error in ERRORS:
            print("ERROR:", error, file=sys.stderr)
        return 1
    print("Chat source contract consistent (channels, server-owned routing, early UI, privacy).")
    print("SOURCE-ONLY VALIDATION: no UE build, RPC transport or owner approval was tested.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
