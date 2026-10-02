# Prompt: execute the dynamic arenas plan from Step 2

Paste this whole file as your opening message. It is self-contained: it names the files, the
current state, the next step, and the rules that bind, so you start from evidence rather than a
tour.

---

## Who you are and what I want

You are working in `/Users/coleshaffer/Projects/blob-royale`, a C++20 authoritative game server
with a React client, on branch `main`. Before writing anything, read the ADRs in
`docs/architecture/` (0001 through 0008), `docs/protocol/v2.md`, `src/gameplay/README.md`,
`src/controllers/README.md`, and `src/simulation/README.md`.

I want you to execute `.claude/plans/2026-09-10-dynamic-arenas-and-combat.md` with the
`executing-plans` skill (`/execute`), starting at Step 2. The plan file is the source of truth and
your job is to keep it credible: verify before you tick a box, amend the plan when reality
diverges, and surface blockers rather than free-styling around them.

## Where things stand

- HEAD is `91b7595`. `890bb9e` added the plan and the proposed ADR 0008 as they stood; `91b7595`
  executed Step 1. ADR 0008 is accepted, with a dated section "Design review and acceptance,
  2026-09-10 (plan Step 1)" recording the owner's decisions. ADRs 0003, 0004, 0005, and 0007 carry
  dated pointers at their tails naming the sections later steps amend and which step lands each.
- No source, map, configuration, schema, or protocol file has changed. Every accepted fixture,
  oracle, and replay expectation stands.
- The plan has three phases. Phase A (Steps 2 to 5) is pure foundations and the prototype gate.
  Phase B (Steps 6 to 15) does not depend on the solver. Phase C (Steps 16 to 24) is provisional
  until the Step 5 gate passes.
- The four owner decisions are in ADR 0008 and bind you: contact responses read the committed
  world and carry a per-body motion disposition; terrain travels once in `welcome` with a shared
  immutable reference on the snapshot value; one shared `[movement]` section replaces the per-mode
  thrust keys; the terrain cutover is expand and contract with a one-step derived mirror.

## Read first, in this order

1. The plan: its "Facts verified in the tree" block, its "Execution constraints" section, then
   Steps 2 through 5 in full.
2. ADR 0008 in full, then the paragraphs headed "Amended 2026-09-10 (ADR 0008, plan Step 1)" at
   the tails of ADRs 0003, 0004, 0005, and 0007.
3. The files Step 2 sits beside: `src/simulation/physics.hpp`, `src/simulation/contact_rule.hpp`,
   `src/simulation/simulation_tolerance.hpp`, `src/simulation/simulation_limits.hpp`, and
   `tests/unit/simulation/physics_tests.cpp`.

## What to do

Execute Steps 2, 3, and 4 in order. One commit per step, the step name as the subject, and an
execution note in the plan for every departure. Step 4 carries `Specialist: rigorous-architect`;
dispatch it and record its prototype review at
`docs/reviews/2026-09-10-continuous-motion-prototype-review.md`.

Then stop at Step 5 and surface it. Step 5 is a human gate and I decide it. While it is pending
you may continue with Phase B (Steps 6 through 15) under the plan's gate rule. You may not touch
Phase C before I pass the gate.

If a step turns out to be wrong or missing, amend the plan: strikethrough with a reason, insert
the corrected step, and add a dated amendment at the foot. If a step needs a kernel change it does
not name, stop and amend; the plan's kernel-seam rule says which steps may change which seams
(2, 9, 13, and 16). Never add a seam change inside a feature step.

## Constraints that bind

These are the plan's "Execution constraints"; violating one is a defect, not a trade-off.

- **The accepted baseline does not move before Step 16.** A Phase A or Phase B step that changes
  one committed value stops and surfaces.
- **Both lanes, every C++ step.** Each `verify-focused` line runs on `linux-gcc-debug` and on
  `linux-clang-asan-ubsan`, one build at a time, exactly as the plan writes them out. The client
  runs through `./scripts/run-linux-toolchain -- ./scripts/verify-web` and the browser gate through
  `./scripts/run-linux-toolchain -- ./scripts/verify-browser-e2e`. `verify-focused` re-enters the
  pinned toolchain by itself when run from the host.
- **Promotions are pure moves.** Arithmetic keeps its written operation order byte for byte, and a
  test proves the old and new answers identical before any reader switches.
- **One root module, one event order.** Every event time, contact or trigger, comes from the Step 2
  module. Every ability window is the Step 14 value. Every body-bound component declares the
  Step 13 trait.
- **Session v3 is one major.** The version const moves once, in Step 7. Each kind, block, member,
  and command lands under v3 in the commit whose authoritative behavior it carries, with schema,
  generated types, client validation, examples, and the `v3.md` row. Nothing is reserved ahead of
  behavior.
- **ADR lockstep.** A step that changes a stated contract amends the named ADR section in the same
  commit with a dated entry.
- **Determinism is the product.** `-ffp-contract=off` is set in `CMakeLists.txt`; write out
  `sqrt(dx*dx + dy*dy)` rather than `std::hypot`; no standard trigonometry or distributions on the
  deterministic path; ascending `EntityId` iteration; every in-tick draw from the world's
  generator.
- **This Mac is advisory.** It runs the Linux toolchain through Docker, so its results are advisory.
  Native Linux evidence is required for any performance or release claim, and Step 5 will not be
  certified from emulation. Say which kind of evidence you have every time you report a number.
- **No push, no deployment.** Commit locally only.

## What I want back

After each step: the verify commands you ran on both lanes with their pass counts, the commit
hash, and any execution note you added to the plan. At Step 5: the prototype review, the
benchmark baselines, and a plain statement of what is proven, what is advisory, and what you could
not settle. Do not tick a box whose verify did not pass, do not describe a Mac measurement as
native evidence, and do not report a step as done while any part of it is left out.
