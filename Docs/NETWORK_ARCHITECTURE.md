# MarburgumII — MMORPG-first client/server architecture

**Status:** Accepted foundational direction; source modules and target manifests scaffolded, runtime integration and testing still required.  
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

**Dependency rule:** both side-specific modules may depend on `MarburgumIIShared`; neither depends on the other. The main game module may consume shared contracts but not either side-specific implementation. The three roles are **source/target separation**, not three independently functioning services today.

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

**Validation status:** the .NET source validator checks this manifest topology. There has **not** been a UE 5.8.3 compile/link, Editor multiplayer session, headless server run, client login, replication smoke, or load test of the new modules in this change.

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

**Persistence:** durable state requires explicit schema/version migration, atomic economic operations, idempotent replay, and reconnect/resume semantics. Database/hosting technology remains an open decision; do not call the current skeleton a working backend.

**Replication:** begin with conventional Unreal Engine replication for the first minimal server/client proof. Do **not** silently assume interoperability with original Marburgum protocol v3, or add a second transport without an accepted ADR and integration tests. Consider Replication Graph/advanced interest strategies and other replication mechanisms after profiling, not by default.

## 5. World streaming is not network interest management

World Partition decides what environment a client loads or renders. An authoritative server's **area of interest (AOI)** decides which entities/events are relevant to each client. The two systems must remain independent. The full world must not be replicated to every player, nor should a client-loaded World Partition cell become a security boundary. Use region/position, entity ownership, mission state, party affiliation and important global events as candidate relevance inputs; record selection/filtered/fan-out statistics.

A population of 10,000–20,000 **virtual** inhabitants and 250 concurrent players per logical shard were previously discussed as ambitious design/engineering goals. They are **not** demonstrated capacities and must not imply 20,000 active AI actors or globally replicated characters. Character simulation levels (persistent authored, near-player active, distant aggregate) and shard/instance policies require separate design.

## 6. Development order and gates

**Gate 0A — native structure:** compile core/shared/client/server in exact UE 5.8.3; verify UHT and module load by target, no side-to-side link, and dedicated server startup without UI dependencies.

**Gate 0B — first real network loop:** launch one dedicated server and one actual client process; join the same empty map; observe replicated `NetworkContractVersion` and `GameState`, disconnect, reconnect, and verify no unauthorized client-side change. Repeat with two clients concurrently, proving both observe the same server-owned state. Include log and build SHA evidence. It is fine for the first world to be an empty test map.

**Gate 1 — first gameplay transaction:** one client requests an interaction, server validates it, commits one change (e.g. an inventory item), replicates the result and survives a save/restart; exercise duplicated requests and two clients attempting the same limited resource. UI reports the committed result; rejected requests have no effects.

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
