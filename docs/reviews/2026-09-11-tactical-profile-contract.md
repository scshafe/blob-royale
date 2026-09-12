# Step 15: tactical profile implementation contract

Status: implementation contract after the owner's four-setting scope approval; not verification.
Step 5 remains unapproved. This adds objective decisions and profile selection, not combat.

## Scope and alternatives

Use one tactical algorithm configured by values, not a class per personality. Compared with
publishing a second independent list of selectable kinds, one immutable NPC catalogue keeps
publication and admission connected. Preserve the existing plain-kind wire projection and append
only actual profiled choices. Do not expose inactive aggression, charge, or shield knobs.

New tactical go commands use unit-strength direction or explicit zero. Unlike the untouched
diagnostic bots, they do not scale acceleration with target distance or a personality weight.
Objective-seeking is a probability of choosing go at a due decision, not a thrust-power control.

## Profile values and authoring

Controllers own `TacticalProfile` and `TacticalProfileCatalogue`. Simulation owns only the
bounded `BotProfileName` identity, using `BoundedName` and the existing lower-snake-case grammar,
maximum 64 bytes, with no empty default. At most 16 configured profiles; unique names, preserved
declaration order, an empty catalogue is valid. Names, not declaration order, seed decisions.

`TacticalProfile::create(Section)` accepts `profile_name` as a string and four required settings:

| Field | Bound | Initial authored value |
|---|---|---|
| `objective_seek_probability` | finite `[0,1]` | `1` |
| `reaction_delay_ticks` | integer `[0,4000]` | `80` |
| `aim_error` | finite `[0,0.25]` | `0.05` |
| `target_persistence_ticks` | integer `[0,4000]` | `400` |

The immutable value exposes `name()` as `const BotProfileName&` and the four named accessors.
`TacticalProfileCatalogue::create(vector<TacticalProfile>)` validates size/uniqueness; its empty
default is valid, `profiles()` returns a const span, and `find(BotProfileName)` returns a pointer
or explicit absence. No factory substitutes a default profile for a missing or unknown name.

Extend the existing strict `[family.name]` parser with `[bot_profile.<name>]`; all four keys are
required, unknown/repeated/malformed values fail visibly. Keep existing configurations valid.
Roster terms are `tactical@<profile>:<count>`; legacy `kind:count` keeps its meaning. Duplicate
terms compare `(kind, profile_name)`, with the existing 16-term and total-bot bounds unchanged.
Reject a profile on an unprofiled kind, tactical without a profile, and unknown selections.

The composition root retains the validated profile catalogue in startup configuration. It creates
one immutable NPC catalogue for each room and passes that same value to runtime and session
context. Tactical selections are available in hill, race, and royale lobbies. Sandbox has no
stable seat identity: reject tactical startup rosters there and omit profiled selectable choices.
This is explicit unsupported admission, not a fabricated identity from controller allocation.

## NPC declaration and authoritative admission

Simulation owns `NpcDeclaration{SeatKindName kind, optional<BotProfileName> profile_name}`.
Move the existing `SeatKindName` alias into its own header without changing policy or storage.
Append optional `profile_name` to `NpcSeat` and `SeatNpcCommand`; their `declaration()` returns
the full identity. Preserve it through declaration, join, leave, and snapshot publication.

`NpcCatalogue::create(vector<string> unprofiled_kinds, vector<NpcDeclaration> profiles = {})`
owns ordered immutable validated choices. Retain the existing 64 plain-kind limit; add 16 profiled
choices, maximum 80 total. Reject invalid names, duplicate plain names, duplicate profile pairs,
missing profile tokens in the profiled partition, and a kind appearing in both partitions.
It exposes `empty()`, const-span `unprofiled_kinds()`/`profiles()`, and exact `contains(kind,
optional<string_view>)` / `contains(NpcDeclaration)`. It contains no tactical policy or factory.

Replace names-only arguments in `SessionWelcome::create` and `MatchSessionContext::create` with
this value. Their existing kind-name accessors may forward to it; expose the profile projection
and catalogue as const data. Decoder admission takes `const NpcCatalogue&`, defaulting to an
empty catalogue for calls that admit no NPC selection. Runtime and `CommandSink` receive the
same catalogue value and check exact `SeatNpcCommand` membership before mailbox insertion.
Default empty means no NPC selections admitted, not unchecked admission. Fixtures requiring NPC
selection must author the catalogue explicitly. Preserve the old command vocabulary and order.

Append optional `expected_npc` to `JoinCommand` for coordinator-issued indexed joins. When present
it requires a seat index and valid declaration; phase 0 fills only an exact current declaration
match. Mismatch is a normal no-op. Every reconciler-issued join supplies it. **Absence preserves
existing unguarded human and literal CSV indexed-join semantics**; it is not inferred from live
state and is not a malformed-input fallback. No wire join field or new permission is added.

Reconciliation compares full declaration for hosted ownership, failed-creation caching, and
in-flight joins. A replaced declaration retires its old pending/hosted bot without waiting for
the join timeout; its queued guarded join cannot bind to the replacement. Session closure still
uses ordinary leave. Preserve old diagnostic naming, seeds, and command behavior otherwise.

## Wire and browser

Retain `welcome.npc_controller_kinds` for unprofiled choices in existing order. Only when profiled
choices exist, append `npc_profiles: [{npc_kind, profile_name}]`. The two arrays are projections of
one catalogue, not separately authored authority. Append optional `profile_name` to `seat_npc`
and NPC seat publication; omit it for unprofiled choices. Null is never omission. Old no-profile
goldens remain unchanged. Update v3 schemas, generated types, examples, validators, and docs in
the implementation commit. Historical v2 artifacts are unchanged.

One lobby selector expands the two projections into flat selectable entries. Selecting an entry
sends its complete declaration immediately; no secondary draft/profile state. Display the profile
on pending and occupied NPC seats even when a controller display name exists. Bound menu height
with scrolling. Retain the current welcome catalogue through snapshots; replace/clear it on the
existing connection/room lifetime. Check outgoing declaration and published NPC seat membership
against that retained catalogue, including malformed/missing/unknown/mismatched profiles.

## Decisions, timing, and randomness

One closed provider table uses mode-state schema IDs. Candidates have an explicit objective-kind
key, subject identity, current public target, arrival radius, and squared distance. Hill and zone
targets use published centers/radii; race uses the exact published road binding, next checkpoint,
and canonical centreline recovery beyond the existing default racer caution fraction. No private
hill schedule, commands, future ticks, or gameplay dependency. Missing race progress is waiting;
finished progress coasts; invalid bindings/progress fail visibly. Maximum 32 candidates, with
excess rejected before partial output. Unsupported running mode fails explicitly.

Screen candidate target and straight center segment using canonical `terrain_supports_point` and
`first_support_exit`; propagate numerical failures. This is not a pathfinder or a guarantee about
momentum, aim error, future moving terrain, body-radius clearance, hazards, or combat safety.
Choose minimum squared distance, then explicit objective-kind ordinal, then subject identity.
Before persistence expiry retain the same still-eligible key, refreshing its public target. A
kept key does not renew its acquisition time. A removed/invalid key or changed gate invalidates
the lease immediately and restarts reaction timing; zero delay permits immediate reconsideration.

First eligible running dynamic-body observation, body/generation change, and stun recovery start
a reaction delay from the actual observed tick. The first timer starts even if no objective is
yet available; repeated empty observations do not continually restart it. Due decisions schedule
from the observed tick, never simulate missed decisions.
At each due valid target outside its arrival region consume one seek-choice draw, even at
probability zero/one. Seek iff `draw < objective_seek_probability`; otherwise explicitly coast.
On seek, normalize the raw target offset, consume one aim draw even for zero error, and use:
`e = ((draw * 2) - 1) * aim_error`, `rx = ux - e*uy`, `ry = uy + e*ux`, followed by written
`sqrt(rx*rx + ry*ry)` normalization and the canonical component clamp. No trig. No draws for
missing/invalid/arrived targets, bodyless state, non-running phases, or observed stun.

Use checked absolute `TickWindow` values for reaction and persistence windows; overflow is a named
failure. While waiting retain valid current intent; cancellation clears it with an explicit zero
when the public body/generation permits. During active stun use no decision/RNG work. Generation
changes also invalidate held work across a missed stun. Author through `Controller::request_thrust`.
Body-independent Controllable with no body waits; no entity uses the existing spawn-request rule.
Non-running observations clear tactical decisions and coast where a dynamic body exists.

Add a default-true protected observation-admission hook before `Controller::decide` mutates base
identity/retry state, after its controller-identity check. Tactical refuses tick <= last completed
tick, returning no commands and changing no state. Legacy implementations accept every call as
before. Mark completion only after successful processing; do not consume failed observations.

Tactical seed identity is raw configured match seed, lobby ID, authored seat index, profile name,
and newly observed public `running_started_tick`. Specify the unsigned mix sequence:
`h=mix_bits(0x746163746963616c XOR match_seed)`, then mix XOR lobby, seat, name length, each
unsigned ASCII name byte in order, and finally running-start tick. Reuse `DeterministicRandom`.
Reseed once per newly observed running identity, never reseat or reallocate controllers. No draws
before running. This is stable domain-separated variation, not universal 64-bit collision freedom.
Legacy factories still receive their existing room seed unchanged.

The registry exposes its stable `Registration` lookup and a per-row `requires_profile` flag.
Its single factory entry point takes an optional `controllers::CreationContext` containing a
borrowed `const TacticalProfile*` and optional `TacticalSeedIdentity{match_seed,lobby_id,seat_index}`.
Unknown kinds retain their existing failure. Plain rows reject either context field; profiled
rows require both. The tactical factory copies both values, never retaining the profile pointer.
The reconciler owns its profile catalogue and receives raw match seed separately from the unchanged
legacy room seed. No other layer maintains a parallel list of profiled kind names.

## Proof and ownership

First prove unused observation/steering helpers against independent frozen complete old behavior
on both controller lanes; only then switch readers. Preserve all diagnostic arithmetic, RNG,
spawn/retry, literal replay, and repeated-observation behavior. Root alone owns CMake, formatting,
schema generation, and serial gates. Controller work owns helpers/proofs/algorithm; boundary work
owns simulation/runtime/server/protocol; browser work owns frontend integration. After the proof,
a configuration worker may own the application profile parser/value integration and its tests;
root retains reconciliation/composition, schemas, documentation, and final integration. Ownership
transfers are explicit, and no worker runs a gate or edits another active slice.

Retain the original Step 15 two-lane selection and full web gate. Add both runtime/server/fixture
selections for the new admission/identity path, fixed-corpus replay for authoring/decoder changes,
and the browser gate for profile selection. Do not weaken existing fixture expectations. Native
performance/release evidence and Phase C remain outside this step.
