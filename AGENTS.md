# AGENTS.md

Blob Royale is a deterministic multiplayer-simulation game: an authoritative C++20 server
(Boost.Beast HTTP/WebSocket, Boost.JSON protocol) and a strict TypeScript React canvas client in
`frontend-react/`. It is a scshafe-dev `native` project (`dev.toml`). No license is granted: all
rights reserved.

## Verify

- `./scripts/verify-linux pr` is the one gate (`dev.toml [verify]`, run by `.github/workflows/ci.yml`
  as `quality/linux-authoritative`). Docker is the only host prerequisite: it builds the
  digest-pinned Ubuntu 24.04 `linux/amd64` image (`ci/linux/Dockerfile`) and runs clean GCC and
  Clang builds with `clang-format --Werror`, every CTest preset (gcc-debug, clang ASan/UBSan,
  clang TSan; zero or skipped tests fail), a real-process smoke test, fuzz regressions, the web
  pipeline, Playwright browser tests, and the runtime-image and dependency scans. It takes about an
  hour.
- Only a native Linux/x86_64 Docker client and daemon is authoritative; anything else is advisory.
- `nightly` and `release` profiles are no weaker; `./scripts/build-release-linux` delegates to
  `release`.
- Faster loops (never a substitute for the gate): `./scripts/verify-focused <ctest-regex> [preset]`,
  `./scripts/run-linux-toolchain -- ./scripts/verify-native-build`, `./scripts/verify-web`.

## Run

- Server: `out/build/linux-gcc-debug/blob-royale --config config/blob-royale.cfg [--scenario <csv>]`
  from the repository root (listens on `127.0.0.1:8000`; SIGINT/SIGTERM shut down cleanly).
- Client: `cd frontend-react && npm ci && npm run dev` (Vite proxies `/api` to the server).

## Layout

- `src/` — `simulation` (the only world mutator), `gameplay`, `controllers`, `runtime`, `protocol`
  (the only Boost.JSON boundary), `server`, `observability`, `application`; `main.cpp` only parses
  input and builds the application. Dependencies point one way (see README.MD).
- `maps/` arenas as data; `config/` the development config; `tests/` unit, integration, fixtures,
  fuzz; `benchmarks/`; `scripts/` every build, verify, release and deploy entry point.
- `docs/architecture` (numbered decisions), `docs/protocol` (wire contracts), `docs/operations`.

## Conventions

- C++20; `.clang-format` (LLVM, 100 columns) is enforced by the gate. `.clang-tidy` (warnings as
  errors) is the configured check set; the gate does not run it.
- Configuration and scenario input is strict: unknown, duplicate or malformed values fail at
  startup. Network code holds only `CommandSink&` and `const SnapshotPublication&`; a new command
  protocol gets a new versioned route instead of widening either.
- Each script starts with a header comment stating its inputs, side effects and idempotence.
- Linux behavior is the contract; macOS results are diagnostics only.
