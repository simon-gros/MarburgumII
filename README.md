# Magna Raedarum: Marburgum II

<p align="center">
  <strong>A persistent world in a fictionalized 1990s Maribor, Slovenia.</strong><br>
  A clean-room Unreal Engine rebuild of <em>Magna Raedarum: Marburgum</em>, grounded in the original game's world, systems and stories.
</p>

<p align="center">
  <code>Unreal Engine 5.8.3</code> · <code>C++</code> · <code>.NET 10</code> · <code>Rust</code><br>
  <strong>Pre-alpha development · Linux test builds · Owner acceptance pending</strong>
</p>

<p align="center">
  <a href="#the-game">The game</a> ·
  <a href="#development-status">Development status</a> ·
  <a href="#getting-started">Getting started</a> ·
  <a href="#architecture">Architecture</a> ·
  <a href="#roadmap">Roadmap</a> ·
  <a href="#documentation">Documentation</a>
</p>

<p align="center">
  <img src="Docs/Captures/lent-buildings/overview.jpg" width="920" alt="The Lent district in an Unreal Engine development capture">
</p>
<p align="center"><sub>The work-in-progress Lent district, with rebuilt building geometry. In-engine development capture; not final art.</sub></p>

---

## The game

**Marburgum II** is an MMORPG-first urban role-playing game about living in, exploring and changing a shared city. Its design preserves the original Marburgum's identity: everyday interactions, missions, professions, skills, trade, vehicles, social relationships, investigations, wildlife and life along the Drava. Narrative and systemic progression take precedence over repetitive combat; combat remains a design possibility, subject to deliberate scope and testing.

The game is built for an **authoritative dedicated server** from its first playable milestone. Solo-friendly play and small-group cooperation (especially 2–5 players) are important experiences, while support for a large population must be demonstrated through future capacity testing. Skills replace the former generated talent system; urban stories are described as **missions**, not fantasy quests.

The default playable development world is now **Lent and its eight neighboring 512-metre windows**, combined in one 1,536-metre-square level with reconstructed survey ground, streets and buildings (ADR-045). It is still an **unstreamed, non-final prototype**, not a proven city-scale production environment. The ultimate geographic foundation is intended to be a separately licensed [Maribor 3D city model](Docs/WORLD_MAP_ADOPTION.md), once purchased, evaluated and approved. That model is **not** part of this repository or the current level.

## Development status

**As of 12 November 2026 (reviewed at `c5d41efc`, ADR-059):** The earlier Linux packaged multiplayer and durable interaction tests are established. Recent **editor-tested** work now includes fishing, 0–1990 skill XP, all eight original mission flows, a self-hosted HTTPS account service, first-character creation and a full-height rotating/zooming 3D preview, two map-quality passes, and protagonist face/head improvements. **This review did not run a new Unreal build or test.** A single current-asset packaged integration, multi-client performance matrix and explicit owner acceptance remain open. [12 November review](Docs/DEVELOPMENT_REVIEW_2026-11-12.md).

| Area | Present development implementation | Evidence / next boundary |
| --- | --- | --- |
| **Networking** | Dedicated authoritative server, replicated state, connection/reconnection, refusal of unauthorized mutations | [Stage 0 tests](Docs/STAGE0_EVIDENCE.md) · Windows qualification outstanding |
| **Interface & chat** | Rendered UMG session and chat screens, server-backed HUD and first Character, Inventory, Journal and Skills views; Settings, full focus and responsive test matrix outstanding | [UI captures](Docs/STAGE0C_UI_EVIDENCE.md) · [three-client chat](Docs/STAGE0C_CHAT_EVIDENCE.md) · [UI roadmap](Docs/UI_HUD_ROADMAP.md) |
| **Character** | Mannequin-compatible protagonist and three model choices; creator 3D stage turns/zooms; ADR-058/059 improved face UVs/mips and head geometry | [Character pipeline](Docs/CHARACTER_PROTAGONIST.md) · latest packaged art/owner approval open |
| **World and streets** | Nine-window, 1,536 × 1,536 m `/Game/World/lent` with 870 shells, collidable surveyed paving, Drava, bridges, ADR-053/054 material/ground/façade improvements and a noncolliding distant ring; **no streaming** | [Map decisions](Docs/DECISIONS.md) · latest packaged performance/owner test open |
| **Interactions & story** | Authoritative inventory/journal and **all eight original mission flows**, including durable choices, clues, money/XP and restart; Melje/Tabor interactions use temporary remote pads | [ADR-047–052](Docs/DECISIONS.md) · editor proof; packaged two-client/owner approval open |
| **Movement & condition** | Server-bounded movement; stamina/winded state; swimming, submersion, drowning and recovery, including persisted condition; newly generated swim/vehicle/fight animations have **IK-aligned drawn feet** (ADR-042/043) | [Movement](Docs/STAGE1_MOVEMENT_TIME_EVIDENCE.md) · [condition](Docs/STAGE1_CONDITION_EVIDENCE.md) · [swimming](Docs/STAGE1_SWIM_EVIDENCE.md) |
| **Cars & driving** | Five-car catalogue, four-raycast authoritative vehicle/seats, pedestrian responses; ADR-056 fixes the own-car freeze reported by owner, with editor reduced-cull evidence | [Vehicle tests](Docs/STAGE1_VEHICLE_EVIDENCE.md) · [ADR-056](Docs/DECISIONS.md) · packaged long drive and owner retest open |
| **Fighting** | Original unarmed jab, committed swing and held guard, server-chosen NPC targets, stamina/cooldowns, down/recovery, procedural clips and Kenney CC0 impacts; earlier two-client packaged smoke, later IK correction editor-tested | [Fighting](Docs/STAGE1_COMBAT_EVIDENCE.md) · [ADR-041](Docs/DECISIONS.md) |
| **Street people** | Author-derived ambient groups/looks/reactions; server population walking a surveyed-pavement graph, fleeing/watching fights, reacting to bodies, spreading panic and dodging/being hit by cars; **one-client editor smoke only** | [Street people](Docs/STAGE1_STREET_EVIDENCE.md) · [ADR-044](Docs/DECISIONS.md) |
| **Accounts & first launch** | Rust HTTPS accounts, verification codes, Argon2id credentials/reset, session, one durable First/Last/model character, single-use world tickets and 3D creator; editor account smoke | [ADR-055/057](Docs/DECISIONS.md) · [onboarding](Docs/FIRST_LAUNCH_ONTOLOGY.md) · real mail sender, packaged matrix and owner gate open |
| **Fishing, skills & missions** | Drava fishing, original 0–1990 XP curve, durable Fishing/Reputation XP, money/clues and all eight mission flows | [ADR-047–052](Docs/DECISIONS.md) · current packaged multi-client acceptance pending |
| **Rendering** | Optional client-side NVIDIA feature checks/fallback; Unreal TSR baseline | [Rendering and platform limits](Docs/RENDERING_NVIDIA.md) |

<details>
<summary><strong>What is not implemented yet?</strong></summary>

A live email sender and hosted account operations (verification codes are currently written to a development outbox); production database/backups; packaged nine-window/crowd/mission/auth performance and multiplayer qualification; the purchased city model, World Partition and shard scaling; vehicle ownership/fuel/traffic/audio, broad professions/trading, fully authored Melje/Tabor ground, ecology art, and expanded consequences of violence. The latest UI, map and model need an **asset-fresh personal owner playtest and explicit sign-off**; the owner's earlier driving defect report (ADR-056) is not approval of the fix.

</details>

### In-game development captures

| Lent and its streets | Early mission interface | Character in the world |
| :---: | :---: | :---: |
| ![Lent promenade](Docs/Captures/lent-buildings/promenade-street.jpg) | ![Mission HUD](Docs/Captures/stage1-lent/M-mission-started-1920x1080.jpg) | ![Protagonist in Lent](Docs/Captures/character-protagonist/retopo-lent-520x560.jpg) |

These screenshots show development milestones from different revisions, not a single final release build.

## Getting started

The **exact Unreal Engine patch is 5.8.3**, as specified in [`.unreal-engine-version`](.unreal-engine-version). The project's `EngineAssociation` value (`5.8`) is not a substitute for verifying `Engine/Build/Build.version`.

**Prerequisites:** Unreal Engine 5.8.3 with the supported Linux C++ toolchain, Git LFS, .NET 10 SDK and stable Rust. Unreal Engine itself is not distributed here. Linux is the validated development platform; [Windows setup](Docs/DEVELOPMENT.md#windows) is documented but has not yet passed the equivalent project build/test matrix.

```bash
git clone https://github.com/simon-gros/MarburgumII.git
cd MarburgumII
git lfs install
git lfs pull

export UE_ROOT=/absolute/path/to/UnrealEngine-5.8.3
./scripts/check-engine-version.sh "$UE_ROOT"

./scripts/build-linux.sh MarburgumIIEditor
./scripts/build-linux.sh MarburgumIIServer
./scripts/build-linux.sh MarburgumIIClient
```

Build and stage the distinct Linux server and client executables, then run the existing smoke suites:

```bash
./scripts/package-linux.sh
./scripts/net-smoke-linux.sh
./scripts/chat-smoke-linux.sh
./scripts/interaction-smoke-linux.sh
./scripts/lent-smoke-linux.sh
./scripts/condition-smoke-linux.sh
./scripts/swim-smoke-linux.sh
./scripts/vehicle-smoke-linux.sh
./scripts/combat-smoke-linux.sh
```

Recent recorded mission, onboarding and street-world checks are principally **editor-mode** tests, not results of the staged commands above:

```bash
./scripts/mission-smoke-linux.sh editor
./scripts/shop-smoke-linux.sh editor
./scripts/account-smoke-linux.sh editor
./scripts/street-smoke-linux.sh editor
```

See [development setup](Docs/DEVELOPMENT.md) for the account HTTPS service, certificates and distinct packaged verification.

To enter the **current default Lent world** manually with development credentials, start the server and client in separate terminals after packaging:

```bash
# Terminal 1 — dedicated server
Saved/StagedBuilds/LinuxServer/MarburgumIIServer.sh /Game/World/lent -port=7777 -log

# Terminal 2 — development client (not a production login)
Saved/StagedBuilds/LinuxClient/MarburgumIIClient.sh -MB2Character=localtester
```

**Controls:** WASD + mouse to move/look · **Shift** run · **E** interact, or get in and out of a car (then W/S drive, brake and reverse, A/D steer, Shift handbrake) · **F** jab · **R** committed swing · **G** hold guard · **C** character · **I** inventory · **J** journal · **Enter** chat. The angler can begin the mission and **E** casts/strikes for a fish at the Drava bank; **K** opens Skills. These later gameplay paths are editor-qualified, not current packaged owner-accepted features. See the [full playthrough instructions](Docs/DEVELOPMENT.md#playing-the-lent-slice-by-hand) for map distinctions, world-store paths, test switches and troubleshooting.

The network and mission tests use dedicated development fixtures, including the older `/Game/Maps/Lent` blockout. The actual default game, server and editor map is **`/Game/World/lent`**. Do not confuse success on an older smoke fixture with approval of the latest world content.

Independent code and data checks:

```bash
dotnet run --project tools/MarburgumII.Tools -- verify
./scripts/mb2.sh validate all
cargo test --manifest-path tools/mb2/Cargo.toml
cargo test --manifest-path native/marburgum-sim-core/Cargo.toml
```

Before any owner sign-off, rebuild and recook from the intended source and asset snapshot, then validate the resulting packaged artifacts. The [playtest freshness guard](Docs/PLAYTEST_FRESHNESS.md) exists for this purpose, but its generic local launch profile still requires actual end-to-end qualification.

## Architecture

| Layer | Technology and role |
| --- | --- |
| **Game runtime** | Unreal Engine **5.8.3** / C++; shared gameplay, networked character, replicated state and interaction entry points |
| **Client** | `MarburgumIIClient`; UMG presentation, input, session flow, chat interface and optional rendering controls |
| **Server** | `MarburgumIIServer`; authoritative interaction and mission handling, admission checks and development persistence |
| **Shared contracts** | `MarburgumIIShared`; cross-boundary network and gameplay types |
| **Editor tools** | `MarburgumIIEditorTools`; C++ content commandlets, including map and geometry production |
| **Independent tools** | **C# / .NET 10** manifest verifier; **Rust** `tools/mb2` validation, test helpers and build/asset freshness checks |
| **Experimental native core** | Rust `native/marburgum-sim-core` with a versioned C ABI; tested independently, **not linked into the Unreal runtime** |

C# is not the gameplay scripting language, and Rust is not yet part of the running game. Repository tooling no longer depends on Python (ADR-018). For module dependencies, trust boundaries and future persistence, see [Architecture](Docs/ARCHITECTURE.md) and [Network architecture](Docs/NETWORK_ARCHITECTURE.md).

**Rendering:** Unreal TSR is the cross-platform fallback. NVIDIA DLSS Super Resolution / DLAA, Frame Generation and Reflex are optional where supported; NVIDIA's official Unreal plugins are installed **locally, outside Git** and are currently Windows/D3D12-only. Linux/Vulkan does not use those plugins, though NVIDIA Smooth Motion has a separate launcher. The included unofficial DLSS 5 integration is **not operable without a separately licensed runtime**, which is deliberately not shipped. [Full compatibility and licensing notes](Docs/RENDERING_NVIDIA.md).

## Roadmap

1. **Foundation — qualified in earlier Linux tests:** UE 5.8.3 authoritative client/server, reconnect, chat and durable transactions; latest asset-fresh integration still pending.
2. **First launch — editor-qualified:** email-confirmed accounts, username/password, character First/Last/model and full-height 3D preview, ticket-gated world entry; real email delivery, packaged UI and owner approval outstanding.
3. **Nine-window playable district — expanded editor integration:** map/collision/Drava, condition, swimming, unarmed fighting, vehicles, street people, fishing, original skill curve and eight mission flows. This is **not** a finished accepted package.
4. **Immediate gates:** clean staged rebuild, two/three-client full-system regressions, car long-distance and map performance probes, coherent Settings/focus/Slovene/controller UI and explicit personal owner sign-off.
5. **Long-term city life:** replace temporary Melje/Tabor floors, develop period art, ecology, trade/professions and transport; add measured streaming and shard operations; evaluate licensed 3D city acquisition separately.

See the [full roadmap](Docs/ROADMAP.md), [UI roadmap](Docs/UI_HUD_ROADMAP.md) and [12 November development review](Docs/DEVELOPMENT_REVIEW_2026-11-12.md). **Every major gameplay feature requires a personal owner playtest of the exact current assets and explicitly recorded approval.**

## Documentation

| Topic | Read |
| --- | --- |
| **Design & continuity** | [Vision](Docs/VISION.md) · [Game ontology](Docs/ONTOLOGY.md) · [Original-game parity register](Docs/LEGACY_PARITY.md) |
| **Engineering** | [Development guide](Docs/DEVELOPMENT.md) · [Architecture](Docs/ARCHITECTURE.md) · [Network contract](Docs/NETWORK_ARCHITECTURE.md) · [Engineering decisions](Docs/DECISIONS.md) |
| **Player experience** | [First launch](Docs/FIRST_LAUNCH_ONTOLOGY.md) · [UI/HUD design](Docs/UI_HUD_DESIGN.md) · [UI ontology](Docs/UI_HUD_ONTOLOGY.md) · [UI roadmap](Docs/UI_HUD_ROADMAP.md) · [Chat](Docs/CHAT.md) |
| **World & assets** | [Map adoption](Docs/WORLD_MAP_ADOPTION.md) · [Protagonist](Docs/CHARACTER_PROTAGONIST.md) · [Conditional Hunyuan3D pipeline](Docs/ASSET_GENERATION_HUNYUAN3D.md) · [NVIDIA rendering](Docs/RENDERING_NVIDIA.md) |
| **Verification** | [12 November review](Docs/DEVELOPMENT_REVIEW_2026-11-12.md) · [Main roadmap](Docs/ROADMAP.md) · [Playtest freshness](Docs/PLAYTEST_FRESHNESS.md) · [Stage 0](Docs/STAGE0_EVIDENCE.md) · [Lent](Docs/STAGE1_LENT_EVIDENCE.md) · [Swimming](Docs/STAGE1_SWIM_EVIDENCE.md) · [Driving](Docs/STAGE1_VEHICLE_EVIDENCE.md) · [Street people](Docs/STAGE1_STREET_EVIDENCE.md) |

Machine-readable design contracts are under `Data/`; implementation, scripts and tests are under `Source/`, `scripts/`, `tools/` and `native/`. The GitHub workflow validates Rust and C# components; **CI does not replace a real Unreal Editor, client/server build or multiplayer test**.

## Licensing and asset provenance

**No project-wide source-code license has been chosen.** Public visibility on GitHub does not by itself permit reuse.

- **Commercial city model:** planned purchase only. Licensed geometry, textures and extractable derivatives must remain outside the public repository and public CI/LFS artifacts; consult [map adoption](Docs/WORLD_MAP_ADOPTION.md).
- **Epic content:** the included UE5 mannequin resources are governed by the Unreal Engine EULA and are not automatically relicensed with this project.
- **Third-party generation tools:** Hunyuan3D-2.1 is a possible offline asset-production tool, **not an engine dependency**. Its published community license excludes EU territory; production use requires rights clearance. The current protagonist's retopology and texture bake use a separate, documented pipeline.
- **NVIDIA components:** locally installed proprietary plugins and unavailable/unlicensed neural-rendering binaries are not redistributed here.

Every imported or generated game asset requires documented provenance, appropriate use rights, technical review and owner approval.
