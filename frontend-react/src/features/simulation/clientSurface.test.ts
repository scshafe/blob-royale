import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const webRoot = process.cwd();
const simulationSourceRoot = resolve(webRoot, 'src/features/simulation');
const productionSourceFiles = Object.freeze([
  'LobbyDirectoryView.tsx',
  'LobbyPanel.tsx',
  'SimulationApi.ts',
  'SimulationCanvas.tsx',
  'SimulationFeature.tsx',
  'SimulationShell.tsx',
  'SimulationViewer.tsx',
  'useLobbyDirectory.ts',
  'useRoomNavigation.ts',
  'useSimulationConnection.ts',
  'useThrustInput.ts',
]);

async function readSimulationSource(sourceFile: string): Promise<string> {
  return readFile(resolve(simulationSourceRoot, sourceFile), 'utf8');
}

describe('protocol v3 session client surface', () => {
  it('has one transport that sends, no lifecycle control, and no interval', async () => {
    const sourcesByFile = new Map(
      await Promise.all(
        productionSourceFiles.map(
          async (sourceFile): Promise<[string, string]> => [
            sourceFile,
            await readSimulationSource(sourceFile),
          ],
        ),
      ),
    );
    const sourceText = [...sourcesByFile.values()].join('\n');

    // Protocol v3 adds commands and, since 2.4, one HTTP read: there is still no lifecycle route
    // and no second place that writes to a socket. The directory is read on a timeout chain that
    // is rescheduled after each read completes, never on an interval, so a slow server is asked at
    // most once at a time and only by `useLobbyDirectory`.
    expect(sourceText).not.toMatch(
      /(?:start-sim|pause-sim|game-config|game-state|setInterval\s*\()/,
    );
    for (const [sourceFile, source] of sourcesByFile) {
      const readsDirectory = /\.fetchLobbies\s*\(/.test(source);
      expect({ sourceFile, readsDirectory }).toEqual({
        sourceFile,
        readsDirectory:
          sourceFile === 'useLobbyDirectory.ts' ||
          sourceFile === 'useSimulationConnection.ts',
      });
    }
    for (const [sourceFile, source] of sourcesByFile) {
      const writesToSocket = /\.send\s*\(/.test(source);
      expect({ sourceFile, writesToSocket }).toEqual({
        sourceFile,
        writesToSocket: sourceFile === 'SimulationApi.ts',
      });
    }
  });

  it('opens the v3 room session route and no longer opens the v1 snapshot socket', async () => {
    const constants = await readSimulationSource('simulationConstants.ts');
    const transport = await readSimulationSource('SimulationApi.ts');

    expect(constants).toContain(
      "LOBBY_DIRECTORY_ENDPOINT_PATH = '/api/v3/lobbies'",
    );
    expect(constants).toContain(
      "ROOM_SESSION_ENDPOINT_PATH_PREFIX = '/api/v3/lobbies/'",
    );
    expect(constants).toContain(
      "ROOM_SESSION_ENDPOINT_PATH_SUFFIX = '/session'",
    );
    expect(constants).toContain(
      "SESSION_WEBSOCKET_SUBPROTOCOL = 'blob-royale.session.v3'",
    );
    expect(constants).not.toContain('/api/v1/snapshots');
    expect(transport).not.toContain('blob-royale.snapshot.v1');
  });

  it('has no Axios runtime or unused user-event dependency', async () => {
    const packageDocument = JSON.parse(
      await readFile(resolve(webRoot, 'package.json'), 'utf8'),
    ) as {
      readonly dependencies?: Readonly<Record<string, string>>;
      readonly devDependencies?: Readonly<Record<string, string>>;
    };

    expect(packageDocument.dependencies).not.toHaveProperty('axios');
    expect(packageDocument.devDependencies).not.toHaveProperty(
      '@testing-library/user-event',
    );
  });

  it('keeps the development server and API proxy on explicit loopback targets', async () => {
    const viteConfiguration = await readFile(
      resolve(webRoot, 'vite.config.ts'),
      'utf8',
    );

    expect(viteConfiguration).toContain("host: '127.0.0.1'");
    expect(viteConfiguration).toContain("target: 'http://127.0.0.1:8000'");
    expect(viteConfiguration).toContain('ws: true');
    expect(viteConfiguration.match(/port: 5173/g)).toHaveLength(2);
    expect(viteConfiguration.match(/strictPort: true/g)).toHaveLength(2);
    expect(viteConfiguration).not.toContain('0.0.0.0');
  });

  it('runs test files in one fully isolated worker thread', async () => {
    const viteConfiguration = await readFile(
      resolve(webRoot, 'vite.config.ts'),
      'utf8',
    );

    expect(viteConfiguration).toContain("pool: 'threads'");
    expect(viteConfiguration).toContain('fileParallelism: false');
    expect(viteConfiguration).toContain('maxWorkers: 1');
    expect(viteConfiguration).toContain('isolate: true');
  });
});
