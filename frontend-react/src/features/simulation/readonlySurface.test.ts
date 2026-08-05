import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';

import { describe, expect, it } from 'vitest';

const webRoot = process.cwd();
const simulationSourceRoot = resolve(webRoot, 'src/features/simulation');
const productionSourceFiles = Object.freeze([
  'SimulationApi.ts',
  'SimulationFeature.tsx',
  'SimulationViewer.tsx',
  'useSimulationConnection.ts',
]);

describe('protocol v1 read-only client surface', () => {
  it('contains no legacy commands, client writes, or polling loop', async () => {
    const sourceText = (
      await Promise.all(
        productionSourceFiles.map((sourceFile) =>
          readFile(resolve(simulationSourceRoot, sourceFile), 'utf8'),
        ),
      )
    ).join('\n');

    expect(sourceText).not.toMatch(
      /(?:start-sim|pause-sim|game-config|game-state|\.send\s*\(|setInterval\s*\()/,
    );
    expect(sourceText).not.toMatch(/<(?:button|input|select|textarea)\b/);
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
