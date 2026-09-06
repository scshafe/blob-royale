# Third-party notices

Blob Royale project code is not offered under an open-source license and remains all rights
reserved. The following independently licensed components are used to build or distribute the
project. Their licenses apply only to those components, not to Blob Royale project code.

## Native dependencies

| Component | Pinned release | License | Use |
|---|---:|---|---|
| Boost (Asio, Beast, JSON) | 1.83.0 | Boost Software License 1.0 | HTTP/WebSocket transport and the private JSON implementation |
| Catch2 | 3.15.3 (`8b08d4d79514f45f7e4ce2a607ac9c94e920d1bb`) | Boost Software License 1.0 | Native tests only |

The complete license text is retained at `third_party/licenses/boost-BSL-1.0.txt`. Copyright
notices remain with the respective Boost and Catch2 contributors.

## Browser runtime dependencies

| Component | Pinned release | License |
|---|---:|---|
| React | 19.2.8 | MIT |
| React DOM | 19.2.8 | MIT |
| Scheduler | 0.27.0 | MIT |
| Ajv | 8.20.0 | MIT |
| ajv-formats | 3.0.1 | MIT |
| fast-deep-equal | 3.1.3 | MIT |
| fast-uri | 3.1.7 | BSD-3-Clause |
| json-schema-traverse | 1.0.0 | MIT |
| require-from-string | 2.0.2 | MIT |

The separately deployed production web bundle contains code from these packages. Their exact
copyright and license texts are retained under `third_party/licenses`; exact dependency metadata is
recorded in `frontend-react/package-lock.json`. The external web SBOM follows npm's complete
production-only graph and enriches it from that lockfile. Build-only packages do not become Blob
Royale project code.

## Toolchain-only dependencies

Vite, TypeScript, Vitest, Testing Library, ESLint, Prettier, json-schema-to-typescript, Syft, Grype,
Clang, GCC, CMake, Ninja, Node.js, npm, Ubuntu, and their transitive packages are used by the pinned
build and verification environment under their respective licenses. Exact versions are defined in
`ci/linux/Dockerfile`, `CMakeLists.txt`, and `frontend-react/package-lock.json` and are emitted by
`scripts/report-toolchain` where applicable.

The shipped native image includes Ubuntu runtime packages under their respective licenses. The
release evidence inventories the actual saved image, including base and transitive packages, rather
than copying toolchain package records into the release layout. CycloneDX SBOMs, vulnerability
reports, and the image-content manifest remain external release evidence; the image retains this
notice and the applicable license texts without embedding a self-referential SBOM.

RapidCSV is not present in or linked by the canonical product. Its former vendored prototype copy
was deleted during the one-engine cutover; Git history remains the source archive. The BSD-3-Clause
text and original copyright notice that were missing from the prototype checkout are restored at
`third_party/licenses/rapidcsv-BSD-3-Clause.txt` as historical compliance evidence.
