#!/usr/bin/env node

// Generates a deterministic CycloneDX inventory of the web deployable's production npm graph.
// Inputs: a frontend package directory and an output path. Side effects: writes the output file.
// Idempotent: yes for an unchanged package manifest, lockfile, and pinned npm implementation.

import { spawnSync } from 'node:child_process';
import { readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const CYCLONEDX_SCHEMA = 'http://cyclonedx.org/schema/bom-1.6.schema.json';
const HASH_ALGORITHMS = new Map([
  ['md5', 'MD5'],
  ['sha1', 'SHA-1'],
  ['sha256', 'SHA-256'],
  ['sha384', 'SHA-384'],
  ['sha512', 'SHA-512'],
]);

class EvidenceError extends Error {
  constructor(code, message) {
    super(`${code}: ${message}`);
    this.name = 'EvidenceError';
  }
}

class LockfilePackageIndex {
  #packagesByIdentity = new Map();

  constructor(lockfile) {
    if (lockfile.lockfileVersion !== 3 || typeof lockfile.packages !== 'object') {
      throw new EvidenceError(
        'FRONTEND_SBOM.LOCKFILE_UNSUPPORTED',
        'a package-lock.json using lockfileVersion 3 is required',
      );
    }

    for (const [installPath, metadata] of Object.entries(lockfile.packages)) {
      if (installPath === '' || !metadata.version) {
        continue;
      }
      const name = LockfilePackageIndex.#packageNameFromInstallPath(installPath);
      const identity = LockfilePackageIndex.identity(name, metadata.version);
      const existing = this.#packagesByIdentity.get(identity);
      if (existing && !LockfilePackageIndex.#equivalent(existing, metadata)) {
        throw new EvidenceError(
          'FRONTEND_SBOM.AMBIGUOUS_PACKAGE',
          `${name}@${metadata.version} has conflicting lockfile metadata`,
        );
      }
      this.#packagesByIdentity.set(identity, metadata);
    }
  }

  static identity(name, version) {
    return `${name}\u0000${version}`;
  }

  metadataFor(name, version, resolved) {
    const metadata = this.#packagesByIdentity.get(
      LockfilePackageIndex.identity(name, version),
    );
    if (!metadata) {
      throw new EvidenceError(
        'FRONTEND_SBOM.PACKAGE_NOT_LOCKED',
        `${name}@${version} is in npm's production graph but not the lockfile`,
      );
    }
    if (resolved && metadata.resolved && resolved !== metadata.resolved) {
      throw new EvidenceError(
        'FRONTEND_SBOM.RESOLUTION_MISMATCH',
        `${name}@${version} resolves differently in npm's graph and the lockfile`,
      );
    }
    return metadata;
  }

  static #packageNameFromInstallPath(installPath) {
    const marker = 'node_modules/';
    const markerIndex = installPath.lastIndexOf(marker);
    if (markerIndex < 0) {
      throw new EvidenceError(
        'FRONTEND_SBOM.LOCK_PATH_INVALID',
        `cannot derive a package name from lockfile path ${installPath}`,
      );
    }
    return installPath.slice(markerIndex + marker.length);
  }

  static #equivalent(left, right) {
    return (
      left.resolved === right.resolved &&
      left.integrity === right.integrity &&
      left.license === right.license
    );
  }
}

class ProductionDependencyGraph {
  #componentsByReference = new Map();
  #dependenciesByReference = new Map();

  constructor(rootManifest, npmTree, packageIndex) {
    this.root = {
      name: rootManifest.name,
      version: rootManifest.version,
      reference: applicationReference(rootManifest.name, rootManifest.version),
    };
    if (!this.root.name || !this.root.version) {
      throw new EvidenceError(
        'FRONTEND_SBOM.MANIFEST_IDENTITY_MISSING',
        'the frontend package manifest requires a name and version',
      );
    }
    if (npmTree.name !== this.root.name || npmTree.version !== this.root.version) {
      throw new EvidenceError(
        'FRONTEND_SBOM.NPM_ROOT_MISMATCH',
        'npm returned a dependency graph for a different package',
      );
    }

    const declaredDependencies = Object.keys(rootManifest.dependencies ?? {}).sort();
    const installedDependencies = Object.keys(npmTree.dependencies ?? {}).sort();
    if (JSON.stringify(declaredDependencies) !== JSON.stringify(installedDependencies)) {
      throw new EvidenceError(
        'FRONTEND_SBOM.ROOT_GRAPH_INCOMPLETE',
        'npm production graph does not exactly match the declared runtime dependencies',
      );
    }

    const rootDependencies = this.#visitDependencies(npmTree.dependencies, packageIndex);
    this.#dependenciesByReference.set(this.root.reference, rootDependencies);
  }

  components() {
    return [...this.#componentsByReference.values()].sort((left, right) =>
      left['bom-ref'].localeCompare(right['bom-ref'], 'en'),
    );
  }

  dependencies() {
    return [...this.#dependenciesByReference.entries()]
      .sort(([left], [right]) => left.localeCompare(right, 'en'))
      .map(([reference, dependencies]) => ({
        ref: reference,
        dependsOn: [...dependencies].sort((left, right) => left.localeCompare(right, 'en')),
      }));
  }

  #visitDependencies(dependencies, packageIndex) {
    const childReferences = new Set();
    for (const name of Object.keys(dependencies ?? {}).sort()) {
      const dependency = dependencies[name];
      if (!dependency || typeof dependency.version !== 'string') {
        throw new EvidenceError(
          'FRONTEND_SBOM.NPM_GRAPH_INVALID',
          `${name} has no concrete version in npm's production graph`,
        );
      }

      const reference = packageUrl(name, dependency.version);
      const metadata = packageIndex.metadataFor(name, dependency.version, dependency.resolved);
      childReferences.add(reference);
      if (!this.#componentsByReference.has(reference)) {
        this.#componentsByReference.set(
          reference,
          CycloneDxDocument.packageComponent(name, dependency.version, metadata),
        );
        this.#dependenciesByReference.set(reference, new Set());
        const transitiveReferences = this.#visitDependencies(
          dependency.dependencies,
          packageIndex,
        );
        this.#dependenciesByReference.set(reference, transitiveReferences);
      } else if (dependency.dependencies) {
        const transitiveReferences = this.#visitDependencies(
          dependency.dependencies,
          packageIndex,
        );
        const knownReferences = this.#dependenciesByReference.get(reference);
        for (const transitiveReference of transitiveReferences) {
          knownReferences.add(transitiveReference);
        }
      }
    }
    return childReferences;
  }
}

class CycloneDxDocument {
  constructor(graph) {
    this.value = {
      $schema: CYCLONEDX_SCHEMA,
      bomFormat: 'CycloneDX',
      specVersion: '1.6',
      version: 1,
      metadata: {
        component: {
          type: 'application',
          'bom-ref': graph.root.reference,
          name: graph.root.name,
          version: graph.root.version,
        },
      },
      components: graph.components(),
      dependencies: graph.dependencies(),
    };
  }

  static packageComponent(name, version, metadata) {
    const component = {
      type: 'library',
      'bom-ref': packageUrl(name, version),
      name,
      version,
      scope: 'required',
      purl: packageUrl(name, version),
    };
    const hashes = integrityHashes(metadata.integrity);
    if (hashes.length > 0) {
      component.hashes = hashes;
    }
    if (metadata.license) {
      component.licenses = [{ expression: metadata.license }];
    }
    if (metadata.resolved) {
      component.externalReferences = [
        { type: 'distribution', url: metadata.resolved },
      ];
    }
    return component;
  }
}

function applicationReference(name, version) {
  return `urn:blob-royale:web:${encodeURIComponent(name)}@${encodeURIComponent(version)}`;
}

function packageUrl(name, version) {
  const segments = name.split('/').map((segment) => encodeURIComponent(segment));
  return `pkg:npm/${segments.join('/')}@${encodeURIComponent(version)}`;
}

function integrityHashes(integrity) {
  if (!integrity) {
    return [];
  }
  return integrity.split(/\s+/).map((value) => {
    const separatorIndex = value.indexOf('-');
    const algorithm = HASH_ALGORITHMS.get(value.slice(0, separatorIndex).toLowerCase());
    const encodedHash = value.slice(separatorIndex + 1).split('?')[0];
    if (!algorithm || !encodedHash) {
      throw new EvidenceError(
        'FRONTEND_SBOM.INTEGRITY_UNSUPPORTED',
        `unsupported package integrity value: ${value}`,
      );
    }
    return { alg: algorithm, content: Buffer.from(encodedHash, 'base64').toString('hex') };
  });
}

function readJson(path, errorCode) {
  try {
    return JSON.parse(readFileSync(path, 'utf8'));
  } catch (error) {
    throw new EvidenceError(errorCode, `${path}: ${error.message}`);
  }
}

function loadNpmProductionTree(packageDirectory) {
  const result = spawnSync(
    'npm',
    ['ls', '--omit=dev', '--all', '--json', '--package-lock-only'],
    {
      cwd: packageDirectory,
      encoding: 'utf8',
      env: { ...process.env, NO_UPDATE_NOTIFIER: '1' },
      maxBuffer: 16 * 1024 * 1024,
    },
  );
  if (result.error || result.status !== 0) {
    const detail = result.error?.message ?? result.stderr.trim() ?? `exit ${result.status}`;
    throw new EvidenceError('FRONTEND_SBOM.NPM_GRAPH_FAILED', detail);
  }
  try {
    return JSON.parse(result.stdout);
  } catch (error) {
    throw new EvidenceError('FRONTEND_SBOM.NPM_GRAPH_INVALID', error.message);
  }
}

if (process.argv.length !== 4) {
  console.error(`Usage: ${process.argv[1]} <frontend-package-directory> <output-file>`);
  process.exit(64);
}

try {
  const packageDirectory = resolve(process.argv[2]);
  const rootManifest = readJson(
    resolve(packageDirectory, 'package.json'),
    'FRONTEND_SBOM.MANIFEST_INVALID',
  );
  const lockfile = readJson(
    resolve(packageDirectory, 'package-lock.json'),
    'FRONTEND_SBOM.LOCKFILE_INVALID',
  );
  const packageIndex = new LockfilePackageIndex(lockfile);
  const npmTree = loadNpmProductionTree(packageDirectory);
  const graph = new ProductionDependencyGraph(rootManifest, npmTree, packageIndex);
  const document = new CycloneDxDocument(graph);
  writeFileSync(resolve(process.argv[3]), `${JSON.stringify(document.value, null, 2)}\n`, {
    encoding: 'utf8',
    mode: 0o644,
  });
} catch (error) {
  console.error(error instanceof Error ? error.message : String(error));
  process.exit(1);
}
