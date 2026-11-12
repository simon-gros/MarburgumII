# First-launch, account and character onboarding ontology — Marburgum II

**Version:** 0.1.0, 10 November 2026  
**Status:** Approved product direction. Implemented for development by ADR-055 (self-hosted account service, front-end screens, ticket admission) and ADR-057 (full-height 3D creator preview, model cards), qualified in the editor by `scripts/account-smoke-linux.sh`. No production deployment or real email sender exists; owner runtime acceptance is outstanding.  
**Normative data:** [first_launch.v0.1.json](../Data/Ontology/first_launch.v0.1.json)  
**Companions:** [Game ontology](ONTOLOGY.md), [UI/HUD ontology](UI_HUD_ONTOLOGY.md), [network architecture](NETWORK_ARCHITECTURE.md), [main roadmap](ROADMAP.md), [UI roadmap](UI_HUD_ROADMAP.md), [ADR-022](DECISIONS.md).

## 1. Product contract and first-encounter sequence

A fresh player must **never appear in Marburgum's playable world before** completing account ownership verification, logging in with a username and password, and committing an actual character consisting of a **First name**, a **Last name**, and a **character model**. The design follows the original Marburgum's email-confirmed account pattern at the product level; the original implementation or protocol is not being transplanted.

The client initially presents a composed **title/login front-end**, rather than an automatically spawned pawn. A new player takes the registration branch and returns to login after credential setup. An already registered player signs in directly. A first-time authenticated account is sent to character creation; a returning account with a previously saved character is offered **Enter World** (and need not create the character again).

**Canonical fresh-player flow:**

1. **Launch / title and login.** Show Marburgum II identity, Username, Password, Sign in, Create account, Settings and Exit. Only a client-side front-end world is loaded.
2. **Register / confirm email.** Collect email; send a time-limited, single-use verification link or code. Present a pending confirmation screen with resend and change-email affordances. Verification is resolved by the trusted account service, not by clicking a local button.
3. **Issue credentials after verification.** Let the verified user establish a unique username and a password through an expiring account-setup flow. This is the secure interpretation of credentials being generated through email confirmation: the **account is provisioned only after verification**. Never email a plaintext password or log it. Any optional system-generated credential must be replaced at first use rather than delivered in email.
4. **Sign in.** Username and password are submitted to the account service over a protected channel. Display authenticating/refused/timeout/retry states. Issue an authenticated session with an opaque server-side account ID.
5. **Character creation (first time only).** An immersive full-screen creator requires a **First name**, **Last name**, and a chosen **approved character model**. Preview the model, allow back/forward among actual available choices, and provide Create Character and Back/Sign out. The first and last names form the in-world display name; they are not the username. No in-world spawn occurs in this screen.
6. **Save and confirm.** The trusted server validates names and the model ID, checks account entitlement, and atomically persists the account-to-character binding, character identity, and initial state. The Create Character button is disabled while the request is pending; retries use idempotency. On failure, preserve the draft and explain next steps.
7. **Enter World.** After successful persistence, show a short character-ready/Enter World state and request character-specific world admission. A world/loading transition appears only after authorization. The client enters the playable map and spawns the authoritative character **only when the server confirms admission and a valid, owned character**.

**Canonical returning-player flow:** Launch → Sign in → account and owned-character lookup → character-ready/Enter World → authoritative admission → world. If session reuse is implemented later, it must satisfy the same identity and entitlement checks; remember-me is not assumed or enabled by this design.

## 2. Ontological objects and identity boundaries

| Concept | Meaning and authoritative owner | Key constraint |
| --- | --- | --- |
| **Account** | Durable out-of-world identity managed by the account service; stable opaque account ID, verified-email status, credential lifecycle | Neither the player's display name nor a world actor |
| **EmailVerificationChallenge** | Short-lived, single-use proof of email control | Verification alone grants no gameplay session |
| **CredentialRecord** | Unique username and securely password-hashed authentication verifier, bound to an account | Password is never stored in plaintext, emailed, logged or replicated |
| **AuthenticatedSession** | Revocable, expiring claim that a server has authenticated an account | Different from an Unreal transport connection and a development character key |
| **CharacterDefinition/ModelChoice** | Server-allowlisted model ID with approved visual/rig/animation compatibility and licensed assets | Preview is client-local; server independently validates choice |
| **PlayerCharacter** | Persistent in-world identity with server-assigned character ID, first name, last name, model ID, initial state and progression | Bound to exactly one account under v1 ownership policy |
| **CharacterCreationDraft** | Unsaved client-local first name, last name and selected model | Does not create gameplay data, authority or spawn eligibility |
| **CharacterCreationReceipt** | Server-issued accepted/refused result with idempotency identity | UI success requires an acknowledged durable commit |
| **WorldAdmission** | Server-side authorization for a particular account session to control one owned character on a particular world/shard | Admission is required even when URL, map name or dev CLI flags are manipulated |

The **account ID**, **username**, **character ID**, **first name**, **last name**, **full display name**, **session token** and **Unreal PlayerState/session ID** are different values. Use opaque IDs for persistence, permissions and chat routing; never authorize by visible names. Account email and credential records do not replicate to other players. Existing server-routed private chat currently uses temporary PlayerState IDs; future stable contacts should refer to authorized character/account IDs without exposing login identifiers.

**Initial cardinality:** one verified account may own **zero or one** created playable character; every created playable character has exactly one owning account. Extra character slots, deleting/renaming a character, cosmetic changes after creation, multiple avatars, account transfer and paid character services are **future decisions**, not implemented defaults. Unique username is mandatory; two characters may share a visible first/last name unless a separate reviewed naming policy explicitly changes that. An account with no character cannot enter world.

## 3. UI/screen ontology and visual interaction

**Front-end location:** The existing non-authoritative /Game/Maps/FrontEnd remains the shell. Registration, verification, credentials, login and character creation are **full-screen UI surfaces** with no gameplay HUD, inventory, world-state simulation or playable pawn. The gameplay world has its own authoritative GameMode and is never the backdrop for client-simulated progression.

**Title and login screen:** Make a quiet, legible, high-quality graphical composition using Marburgum II brand, period-appropriate urban imagery or animated environmental ambience if properly licensed, one clear primary action, and accessible keyboard/controller focus. Password is masked with an optional reveal control. Provide caps-lock guidance where available, clear disabled/loading states and an explicit privacy-safe error message. Register, Forgot password, Settings, and Exit are visible secondary paths. Credentials should not survive in local logs or crash telemetry.

**Email branch:** The initial registration asks only for necessary information; do not request First name, Last name, or model yet. Provide pending-verification state, resend throttling, expired/used-token recovery and a return-to-login route. For public responses, avoid account-enumeration leaks: use appropriately neutral wording if a registered email is submitted. Handle delayed email delivery without opening a world connection.

**Character creation screen:** Full-height 3D preview on a dedicated client-only preview scene/viewport; side panel for First name, Last name and actual selectable model thumbnails/cards; selected/focused/disabled visual states; a coherent Create Character action; Back/Sign out without accidental data loss. Preview rotates and zooms via pointer/controller if supported. No fictitious models, body customization, stat allocations, classes or perks should be advertised without implemented systems. The exact initial selection may use suitable UE5 mannequin-compatible assets, but the usable catalogue, art quality and exact appearance require owner review. Model asset references are not accepted as arbitrary client paths: only stable server-allowlisted model IDs are sent.

**Names:** They are in-world **fictional character names**, not demands for the user's legal identity. Validate server-side with a written, reviewable policy (Unicode normalization, length, permitted letters/space/hyphen/apostrophe, names with Slovene diacritics, reserved words and abuse moderation); present field-level errors without clearing other inputs. Persist first and last separately, derive the visible full name for chat/overhead/Character panel, and keep identity stable across reconnects.

**Transition design:** The first gameplay frame is shown only after the authenticated, owned character is admitted to the correct server world and has usable replicated state. While loading, show real connection/admission/loading signals; do not claim that a waiting animation proves login, storage or server success. Timeout, ban/suspension, unavailable world, incompatible build and lost connection are distinct understandable recovery states. Returning from gameplay to front end must clear in-world widgets, stale private targets and gameplay focus.

**Accessibility and visual quality:** English/Slovene copy; robust diacritics; readable long fields; masked password; explicit focus order; keyboard/mouse and controller navigation; safe zones and screen scaling from 720p through 4K/ultrawide; reduced motion and no color-only error messages. Every screen must visually belong to the existing UMG design system, with no raw debug-text form or fake live-state badges.

## 4. Lifecycle state machine

**States:** title, registration_email, email_verification_pending, credential_setup, login, authenticating, character_creation, character_persisting, character_ready, world_admitting, world_loading, in_world, recovery and blocked. See the machine-readable transition table for the authoritative event and guard of each edge. No direct title/login/creator → in_world shortcut exists.

- Email confirmation is prerequisite to issuing usable username/password credentials for newly registered accounts.
- Only an authenticated session can request owned-character lookup or create a character.
- Only a server-confirmed persisted character can be selected for world admission.
- Only a successful world-admission decision can lead to an in-world pawn.
- Authentication expiry or loss of entitlement returns to appropriate front-end recovery/login, never an unauthenticated offline pawn.
- A duplicate Create request or concurrent login must not create two characters or bypass one-session/one-active-character policy.
- Back/Cancel before a successful commit leaves no character; after a confirmed commit the account returns as a returning player rather than making another character.
- An existing character must always restore by durable character ID; the development key is not an ownership proof.

## 5. Server architecture, credentials and migration

**Trusted account service:** Owns email verification, username uniqueness, password verifier, recovery, abuse controls and session issuance/revocation. Use a current password-hashing scheme such as Argon2id with per-password salts, a secure transport for all authentication, single-use expiry-bounded verification/reset tokens stored as verifiers rather than raw tokens, server-side rate limiting and careful secret redaction. Never put raw passwords, verification codes or long-lived bearer tokens in URLs, Unreal replication fields, telemetry, screenshots or log messages.

**Authority/admission boundary:** The game client requests login/account operations through a reviewed authentication interface; the **server** verifies the session, account status and character ownership before gameplay entry. ServerOnly modules or dedicated services own durable account/character operations; MarburgumIIClient owns UI and model previews; MarburgumIIShared contains sanitized versioned contracts; client-sent model/name data are untrusted. Exact auth-provider, transport endpoint, secure-token exchange, database and email sender are not yet selected. No unverified design assumption is an API implementation.

**Existing Stage 1 tests:** ADR-014's MB2Character key and file-backed world store are **development-only substitutes**. They may remain for explicit smoke-test profiles without silently becoming production credentials; they must be disabled/guarded in public shipping paths and must not permit bypass of the new admission chain. Preserve their deterministic test coverage while introducing a separately tested real-account route.

**Persistence and migration:** Persist account and character separately using a stable one-to-one ownership binding; carry forward character state from the authoritative server store, not from UI previews. Define idempotent create receipts, durable transaction commit, uniqueness and atomicity under concurrent requests, schema migration and backup/recovery. Migration of legacy original-Marburgum accounts is a **separate, unapproved** decision; email-confirmed experience parity does not imply reuse of old password hashes, tables or user data.

## 6. Required acceptance scenarios (test IDs)

| Test | Required observation |
| --- | --- |
| FL-01 Fresh account | First launch displays login/front-end with no pawn; new email registration waits for external verification |
| FL-02 Verification | Correct single-use link/code enables credential setup; expired, reused and invalid challenges fail safely; resend is throttled |
| FL-03 Credentials | Unique username/password established only for verified account; sign-in works; bad password, rate-limit and reset/recovery paths have safe errors |
| FL-04 First character | Fresh authenticated account MUST see First name, Last name and model chooser; no way to enter world before server-confirmed creation |
| FL-05 Name and model | Accept Slovene diacritics and permitted variants; reject invalid names/model IDs or missing fields server-side; no forged asset path |
| FL-06 Durability and races | Save survives client/server restart; double-click/replayed/concurrent Create yields one owner-bound character and one outcome |
| FL-07 Returning player | Re-login restores character identity/model/state; skips creation; Enter World admits that character only |
| FL-08 Adversarial admission | Unverified, unauthenticated, keyless, forged owner/model, expired/revoked session, direct map URL and shipping dev-key bypass attempts cannot spawn |
| FL-09 Continuity | Network loss, service error, delayed email, cancel, timeout, retry and logout preserve correct data/focus and do not show an unauthorized HUD |
| FL-10 Visual/accessibility | Genuine UE 5.8.3 packaged-client captures at 720p, 1080p, 1440p, 4K/ultrawide; focus, localization, scaling, password handling and model preview verified |
| FL-11 Multiplayer privacy | Separate clients cannot inspect another account's email, credentials or character draft; chat/display identity is decoupled from username |
| FL-12 Owner sign-off | Owner personally runs first-time and returning flows on a **freshness-verified build including current UI/model assets**, then explicitly approves before public milestone acceptance |

**Current evidence status:** Documentation and machine-readable design only. None of FL-01–FL-12 is claimed to pass; Stage 0C session connection and Stage 1 development character tests prove different things.

## 7. Delivery order and unresolved choices

**Onboarding-first workstream:** FL-A service contracts and verification / credentials → FL-B polished account screens → FL-C model catalogue and character creator → FL-D atomic character persistence and authoritative world admission → FL-E reconnect/recovery, security/visual test matrix and owner approval. These should be completed **before** first publicly accepted Stage 1 enter-world showcase; they do not invalidate existing automated Stage 0B/Gate 1 tests using a clearly labeled dev profile. See both roadmaps.

**Choices requiring an explicit later decision:** account provider/self-hosting and storage; whether username is chosen or suggested/generated during verified setup; email provider, legal/retention rules and recovery procedure; exact naming policy; launch model roster and licensing; eventual additional character slots. The non-negotiable experience remains email-verified credentials, username/password login, first/last/model creation, and only then world entry.
