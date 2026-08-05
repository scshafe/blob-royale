# Plan: Blob Royale project deep dive

**Goal:** Produce an evidence-backed repository guide and a candid assessment of the project's largest strengths and weaknesses.
**Out of scope:** Changing runtime behavior, dependencies, or product design.

## Context

The repository contains a C++ game engine/server, a React client, a dependency-aware scheduling primitive, configuration, benchmark scaffolding, and a small set of scenario fixtures. The assessment must distinguish verified behavior from inferred intent and preserve the user's existing worktree.

## Steps

- [x] **Step 1: Inventory the repository and build surfaces**
  - Verify: `rg --files -g '!frontend-react/node_modules' | sort`
  - Specialist: `cartographer`

- [x] **Step 2: Trace engine, server, and client runtime flows**
  - Verify: human review against entry points and call sites
  - Specialist: `cartographer`

- [x] **Step 3: Assess tests, operations, and security boundaries**
  - Verify: build and test commands plus targeted source inspection
  - Specialist: `doddy`

- [x] **Step 4: Write the durable project guide**
  - Verify: every substantive claim cites a repository path and the guide covers architecture, operation, strengths, weaknesses, and priorities

- [x] **Step 5: Validate the guide and final conclusions**
  - ~~Verify: `git diff --check && cmake -S . -B /tmp/blob-royale-build && cmake --build /tmp/blob-royale-build`~~
  - Verify: whitespace checks, relative-link audit, reproduced React build/test/lint results, and source-level validation of native compile blockers
  - Notes: CMake is unavailable in the assessment environment, and current declaration/definition conflicts prove the native target cannot compile as written.

## Done criteria

`docs/PROJECT_DEEP_DIVE.md` exists, accurately describes the current repository, reports evidence from available builds/tests, and prioritizes the most consequential strengths and weaknesses without changing product code.

**Amended 2026-08-04:** Replaced the unavailable native build command with explicit source-consistency checks and recorded the reproduced frontend verification failures.
