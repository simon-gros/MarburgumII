# MarburgumII — MMORPG-first client/server architecture

**Status:** Accepted foundational direction. Gate 0A builds and the Gate 0B loopback network smoke pass on Linux with UE 5.8.3 ([evidence](STAGE0_EVIDENCE.md)). Gate 1's first transaction passes with a development identity and file-backed store ([evidence](STAGE1_GATE1_EVIDENCE.md)); production persistence, login, AOI and scale remain unbuilt.  
**Engine:** Unreal Engine **5.8.3**.  
**Relationship to original:** The original project separated Unreal `MarburgumClient`, `MarburgumServer` and `MarburgumShared`, and used an authoritative server with a legacy protocol-v3 TCP/JSON contract. MarburgumII preserves the **roles and gameplay semantics**, but rebuilds their implementation. The old protocol is **not** a drop-in compatibility requirement.

## 1. Non-negotiable product model

**MarburgumII is an MMORPG from the architectural foundation.** Its intended world is a long-lived, authoritative shared simulation; solo and 2–5-person cooperative play are **player-experience modes within that architecture**, not separate offline rules engines. A local dedicated server running with one client is an initial development configuration, not an alternate permanent single-player authority.

The game ultimately retains the original Marburgum's core disciplines: 1990s fictionalized Maribor, geography and period evidence, player/character identity, skill progression (no generated talents), reputation, NPCs and social affiliations, missions, economy and trading, inventory, housing, vehicle mobility, fishing, photography, day/night/weather, narrative choices, and eventual city-scale virtual population. The gameplay specification, content identities, and data-provenance rules are the inputs to the rebuild; this source scaffold does not import or implement them.

## 2. Source module boundaries

| Module | Host | Owns | Must not own |
| --- | --- | --- | --- |
| `MarburgumII` | Runtime | Unreal game entry point, shared replicated `GameState`/`PlayerState` representations and authority-only `GameMode` | Client presentation libraries, persistence implementation |
| `MarburgumIIShared` | Runtime | Identity vocabulary, protocol/compatibility constants, definitions and serialization contracts | UMG, server persistence, client input or presentation |
| `MarburgumIIClient` | ClientOnly | Local input, camera, HUD, audiovisual projection, request creation, client prediction | Authoritative grants of currency, XP, inventory or mission rewards |
| `MarburgumIIServer` | ServerOnly | Account/session admission adapters, authoritative domain reducers, persistence and AOI/interest-management services (future) | Client UI, cameras, graphical streaming, untrusted display state |

**Dependency rule:** both side-specific modules may depend on `MarburgumIIShared` and on the core `MarburgumII` module's replicated types; neither depends on the other. The core and shared modules never depend on a side-specific module, and shared never depends on core. The .NET validator enforces these edges, and symbol inspection of the packaged binaries confirms that the Client contains no server-module code and the Server contains no client-module code ([ADR-012](DECISIONS.md)).

Unreal `AGameModeBase` exists only on server authority; `AGameStateBase` and `APlayerState` replicate relevant public state to clients. The initial `AMarburgumGameState::NetworkContractVersion` is a replicated marker for network smoke testing; it is not proof of a successful client/server handshake.

## 3. Build target matrix

| Target | Type | Modules | Purpose |
| --- | --- | --- | --- |
| `MarburgumIIClient` | Client | core + shared + client | Player-facing deployment; excludes server-only code |
| `MarburgumIIServer` | Server | core + shared + server | Headless dedicated authority; excludes client-only code |
| `MarburgumIIEditor` | Editor | all four | Editor / multiplayer PIE inspection |
| `MarburgumII` | Game | all four | Local combined/listen development; **not** a production MMORPG server deployment |

```bash
export UE_ROOT=/absolute/path/to/UnrealEngine-5.8.3
./scripts/build-linux.sh MarburgumIIEditor
./scripts/build-linux.sh MarburgumIIServer
./scripts/build-linux.sh MarburgumIIClient
```

**Validation status:** all four targets compile and link with UE 5.8.3 on Linux, and `scripts/package-linux.sh` cooks and stages the separate `LinuxServer` and `LinuxClient` builds. `scripts/net-smoke-linux.sh` passes Gate 0B over loopback using a headless dedicated server and `-nullrhi` client processes ([evidence](STAGE0_EVIDENCE.md)). Not yet tested: rendered clients, non-loopback or lossy networks, Windows builds, persistence, login and load.

### Stage 0B smoke protocol

| Piece | Module | Role |
| --- | --- | --- |
| `AMarburgumGameState` | core | Replicates `NetworkContractVersion`, a server-timer `AuthorityHeartbeat`, and `AppliedRequestCount` / `RejectedRequestCount`; keeps a server-side, process-lifetime idempotency ledger of operation GUIDs |
| `AMarburgumPlayerController` | core | `ServerSubmitOperation(FGuid)` commits once and answers `Applied` or `Duplicate`; `ServerRequestContractVersion(int32)` is always refused; results return through a client RPC |
| `FMarburgumIIServerModule` | server | Logs authority-side `ready`, `login` and `logout` evidence |
| `UMarburgumNetSmokeSubsystem` | client | Active only with `-MB2NetSmoke`; drives the request sequence, optional disconnect/reconnect (`-MB2SmokeReconnect`) and peer wait (`-MB2SmokePeers=N`), then exits 0 or 1 |

Evidence lines share the `LogMarburgumNet` category and the `MB2SMOKE` prefix. This smoke ledger is process-lifetime only; the Gate 1 interaction path below keeps its receipts in the durable world store.

### Gate 1 interaction protocol

| Piece | Module | Role |
| --- | --- | --- |
| `AMarburgumInteractable` | core | Map-placed object with a stable `InteractableId`, granted item, authored initial stock and range; replicates `RemainingStock` (-1 until the server has applied its record) |
| `AMarburgumPlayerState` | core | Server-only development character key; `Inventory`, `bInventoryKnown` and `bHasCharacter` replicate `COND_OwnerOnly` |
| `AMarburgumPlayerController` | core | `ServerRequestInteraction(FGuid, AMarburgumInteractable*)` with an 8 requests/s cap; answers with `ClientReceiveInteractionReceipt(FMarburgumInteractionReceipt)` |
| `UMarburgumAuthorityService` | core | Abstract world subsystem the RPC routes to; absent on clients, so they refuse with `authority_unavailable` |
| `UMarburgumWorldAuthority` | server | Validates operation id → store → character → receipt replay → target → range (+50 cm) → stock, commits stock, custody and receipt together, then replicates |
| `FMarburgumWorldStore` | server | One JSON document per map, `fsync` + atomic `rename` per commit (ADR-014) |
| `AMarburgumGameMode` | core | Refuses malformed or already-connected character keys at `PreLogin` and `InitNewPlayer` |

Outcomes: `Applied` (durable, then replicated), `Duplicate` (same operation id and character as a stored receipt; nothing changes, survives restarts) or `Rejected` with a reason (`out_of_range`, `out_of_stock`, `no_character`, `store_write_failed`, `operation_conflict`, ...). Evidence lines use the `MB2INTERACT` prefix; `scripts/interaction-smoke-linux.sh` drives the qualification.

## 4. Authority and lifecycle

```text
Unreal client (input / camera / presentation)
       |       requests and client-side prediction
       v
Dedicated authoritative Unreal server
  admission -> validation -> command ordering -> domain changes
                  |                |
                  |                +--> event receipts / persistence (planned)
                  v
       world, players, missions, money, items, skills
                  |
                  v
        relevance/replication projections
                  |
                  v
      clients render approved state
```

**Connection/admission:** join and identity validation must be established before consequential commands. A connected socket is not authenticated player identity; accounts, sessions and in-world characters require distinct IDs. Do not put passwords or server secrets in the client.

**Authoritative mutations:** the server alone accepts or rejects an interaction, skill XP award, inventory/money transfer, reputation change, mission stage completion, vehicle ownership change or world event. Use stable operation IDs, ordering checks, permission/eligibility checks, bounded retries, and one committed receipt per successful result. Client prediction may improve responsiveness, never grant rewards.

**General/private chat:** [CHAT.md](CHAT.md) defines a first-wave session-scoped authoritative text service. A client-owned PlayerController RPC submits plain text; the server derives sender identity from the authenticated/connected controller state, validates length/rate and resolves a recipient. General chat fans out only within the current logical server. Private chat is delivered by **unicast only to sender and recipient**, never through global GameState replication or NetMulticast. The initial C++ source path has not passed a native UE 5.8.3 multiplayer smoke test. Account identity, moderation and persistent messages are later systems; no message grants gameplay progress.

**Persistence:** durable state requires explicit schema/version migration, atomic economic operations, idempotent replay, and reconnect/resume semantics. Database/hosting technology remains an open decision; do not call the current skeleton a working backend.

**Replication:** begin with conventional Unreal Engine replication for the first minimal server/client proof. Do **not** silently assume interoperability with original Marburgum protocol v3, or add a second transport without an accepted ADR and integration tests. Consider Replication Graph/advanced interest strategies and other replication mechanisms after profiling, not by default.

## 5. World streaming is not network interest management

World Partition decides what environment a client loads or renders. An authoritative server's **area of interest (AOI)** decides which entities/events are relevant to each client. The two systems must remain independent. The full world must not be replicated to every player, nor should a client-loaded World Partition cell become a security boundary. Use region/position, entity ownership, mission state, party affiliation and important global events as candidate relevance inputs; record selection/filtered/fan-out statistics.

A population of 10,000–20,000 **virtual** inhabitants and 250 concurrent players per logical shard were previously discussed as ambitious design/engineering goals. They are **not** demonstrated capacities and must not imply 20,000 active AI actors or globally replicated characters. Character simulation levels (persistent authored, near-player active, distant aggregate) and shard/instance policies require separate design.

## 6. Development order and gates

**Gate 0A — native structure:** compile core/shared/client/server in exact UE 5.8.3; verify UHT and module load by target, no side-to-side link, and dedicated server startup without UI dependencies.

**Gate 0B — first real network loop:** launch one dedicated server and one actual client process; join the same empty map; observe replicated `NetworkContractVersion` and `GameState`, disconnect, reconnect, and verify no unauthorized client-side change. Repeat with two clients concurrently, proving both observe the same server-owned state. Include log and build SHA evidence. It is fine for the first world to be an empty test map.

**UI/visual qualification in parallel:** [UI_HUD_ONTOLOGY.md](UI_HUD_ONTOLOGY.md), [UI_HUD_ROADMAP.md](UI_HUD_ROADMAP.md) and [UI_HUD_DESIGN.md](UI_HUD_DESIGN.md) govern the early client-only graphical HUD, including authoritative signals, client-local preferences and labeled fixtures. The UI may be previewed with clearly marked demo fixtures, but public gameplay footage must display honest server-originated/replicated state. The authoritative server does not load UMG. The wanted-star/police-pursuit system is explicitly not being carried forward.

**Gate 1 — first gameplay transaction:** one client requests an interaction, server validates it, commits one change (e.g. an inventory item), replicates the result and survives a save/restart; exercise duplicated requests and two clients attempting the same limited resource. UI reports the committed result; rejected requests have no effects. *Status 2026-11-10: passes on Linux loopback with a test fixture and the ADR-014 development store ([evidence](STAGE1_GATE1_EVIDENCE.md)); owner playtest pending.*

**Gate 2 — representative Marburgum gameplay:** implement fishing + skills + mission objectives + inventory/currency as server-owned, accessible through genuine client UI, including reconnect/load without XP/item duplication. Work in a tiny approved district first.

**Gate 3 — city semantics:** incremental, provenance-reviewed Maribor map import; vehicles, NPC simulation and streaming while preserving relevance and authority boundaries.

**Gate 4 — scalability rather than first networking:** load ladder 1 → 8 → 32 → 64 → 128 → 250 clients, followed by optional 300–350 connection headroom characterization. Evaluate distributed-district and dense-hotspot patterns. Track server tick p50/p95/p99, CPU/memory, network bytes and queues, AOI fan-out, authoritative entity count, persistence latency, reconnect failures, and abuse/rate-limit failures.

**Rule:** no gameplay system is accepted as 'complete' if it functions only through local client mutation or editor shortcuts. A server/client source module skeleton passes **none** of the network-runtime gates by itself.

## 7. Links to project design

- [Foundational ontology](ONTOLOGY.md) — semantic type definitions and ownership/authority invariants.
- [Current architecture](ARCHITECTURE.md) — technology boundaries and build requirements.
- [Milestone roadmap](ROADMAP.md) — implementation order and acceptance.
- [Original authoritative architecture](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/ARCHITECTURE.md) — legacy role separation, AOI discussion and migration history.
- [Original capacity roadmap](https://github.com/simon-gros/magna-raedarum-marburgum/blob/main/docs/MULTIPLAYER_SCALABILITY_ROADMAP.md) — 250-player design target and validation ladder.
