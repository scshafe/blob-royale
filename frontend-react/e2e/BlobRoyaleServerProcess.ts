import { constants as fileSystemConstants } from 'node:fs';
import { access } from 'node:fs/promises';
import { spawn, type ChildProcess } from 'node:child_process';
import { fileURLToPath } from 'node:url';

import { BrowserE2EError } from './BrowserE2EError';

const SERVER_SHUTDOWN_TIMEOUT_MILLISECONDS = 10_000;
const SERVER_FORCE_CLEANUP_TIMEOUT_MILLISECONDS = 5_000;
const SERVER_OUTPUT_MAXIMUM_BYTES = 65_536;

export interface BlobRoyaleServerExitResult {
  readonly code: number | null;
  readonly signal: NodeJS.Signals | null;
}

function waitForProcessExit(
  exitPromise: Promise<BlobRoyaleServerExitResult>,
  timeoutMilliseconds: number,
  timeoutCode: string,
): Promise<BlobRoyaleServerExitResult> {
  return new Promise((resolve, reject) => {
    const timeout = setTimeout(() => {
      reject(
        new BrowserE2EError(
          timeoutCode,
          'Server process did not exit before the deadline.',
          {
            timeout_milliseconds: timeoutMilliseconds,
          },
        ),
      );
    }, timeoutMilliseconds);

    void exitPromise.then(
      (result) => {
        clearTimeout(timeout);
        resolve(result);
      },
      (error: unknown) => {
        clearTimeout(timeout);
        reject(error);
      },
    );
  });
}

/** @canonical browser_e2e_server_process -- owns the exact E2E backend lifecycle. */
export class BlobRoyaleServerProcess {
  private activeChild: ChildProcess | null = null;
  private exitPromise: Promise<BlobRoyaleServerExitResult> | null = null;
  private processError: Error | null = null;
  private serverOutput = '';
  private serverOutputTruncated = false;

  private constructor(
    private readonly executablePath: string,
    private readonly configurationPath: string,
    private readonly scenarioPath: string,
  ) {}

  static async createFromEnvironment(): Promise<BlobRoyaleServerProcess> {
    const executablePath =
      process.env.BLOB_ROYALE_BROWSER_E2E_SERVER_EXECUTABLE;
    if (executablePath === undefined || executablePath === '') {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_EXECUTABLE_UNSET',
        'BLOB_ROYALE_BROWSER_E2E_SERVER_EXECUTABLE must name the exact server executable.',
      );
    }

    try {
      await access(executablePath, fileSystemConstants.X_OK);
    } catch (cause) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_EXECUTABLE_UNAVAILABLE',
        'The exact server executable is not executable.',
        { executable_path: executablePath },
        cause,
      );
    }

    const fixtureDirectory = fileURLToPath(
      new URL('./fixtures/', import.meta.url),
    );
    return new BlobRoyaleServerProcess(
      executablePath,
      `${fixtureDirectory}blob-royale-browser-e2e.cfg`,
      `${fixtureDirectory}blob-royale-browser-e2e.csv`,
    );
  }

  async start(): Promise<void> {
    if (this.activeChild !== null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_ALREADY_ACTIVE',
        'The E2E server process must be stopped before it can be restarted.',
        this.diagnosticContext(),
      );
    }

    this.processError = null;
    this.serverOutput = '';
    this.serverOutputTruncated = false;
    const child = spawn(
      this.executablePath,
      ['--config', this.configurationPath, '--scenario', this.scenarioPath],
      {
        detached: true,
        stdio: ['ignore', 'pipe', 'pipe'],
      },
    );
    this.activeChild = child;
    child.stdout?.on('data', (chunk: Buffer) => {
      this.appendServerOutput('stdout', chunk);
    });
    child.stderr?.on('data', (chunk: Buffer) => {
      this.appendServerOutput('stderr', chunk);
    });
    child.on('error', (error) => {
      this.processError = error;
    });
    this.exitPromise = new Promise((resolve) => {
      child.once('close', (code, signal) => {
        resolve(Object.freeze({ code, signal }));
      });
    });

    try {
      await new Promise<void>((resolve, reject) => {
        const spawned = () => {
          child.off('error', failed);
          resolve();
        };
        const failed = (error: Error) => {
          child.off('spawn', spawned);
          reject(error);
        };
        child.once('spawn', spawned);
        child.once('error', failed);
      });
    } catch (cause) {
      this.activeChild = null;
      this.exitPromise = null;
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_START_FAILED',
        'Failed to spawn the exact server process.',
        this.diagnosticContext(),
        cause,
      );
    }
  }

  async assertRunning(): Promise<void> {
    const child = this.requireActiveChild();
    if (
      this.processError === null &&
      child.exitCode === null &&
      child.signalCode === null
    ) {
      return;
    }

    const exitResult =
      this.exitPromise === null
        ? Object.freeze({ code: child.exitCode, signal: child.signalCode })
        : await this.exitPromise;
    throw new BrowserE2EError(
      'BROWSER_E2E.SERVER_EXITED_UNEXPECTEDLY',
      'The server exited before the browser test requested shutdown.',
      {
        ...this.diagnosticContext(),
        exit_code: exitResult.code,
        exit_signal: exitResult.signal,
      },
      this.processError ?? undefined,
    );
  }

  async terminateWithSigterm(): Promise<BlobRoyaleServerExitResult> {
    const child = this.requireActiveChild();
    const processId = child.pid;
    const exitPromise = this.exitPromise;
    if (processId === undefined || exitPromise === null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_PROCESS_STATE_INVALID',
        'The active server has no process ID or exit observer.',
        this.diagnosticContext(),
      );
    }
    if (child.exitCode !== null || child.signalCode !== null) {
      await this.assertRunning();
    }

    try {
      process.kill(-processId, 'SIGTERM');
    } catch (cause) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_SIGTERM_FAILED',
        'Failed to send SIGTERM to the server process group.',
        this.diagnosticContext(),
        cause,
      );
    }

    let exitResult: BlobRoyaleServerExitResult;
    try {
      exitResult = await waitForProcessExit(
        exitPromise,
        SERVER_SHUTDOWN_TIMEOUT_MILLISECONDS,
        'BROWSER_E2E.SERVER_SIGTERM_TIMEOUT',
      );
    } catch (cause) {
      await this.forceCleanupActiveProcess();
      throw cause;
    }
    this.activeChild = null;
    this.exitPromise = null;

    if (exitResult.code !== 0 || exitResult.signal !== null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_SIGTERM_EXIT_INVALID',
        'SIGTERM must produce an ordinary server exit with code zero.',
        {
          ...this.diagnosticContext(),
          exit_code: exitResult.code,
          exit_signal: exitResult.signal,
        },
      );
    }
    return exitResult;
  }

  async cleanup(): Promise<void> {
    if (this.activeChild === null) {
      return;
    }
    await this.forceCleanupActiveProcess();
  }

  private appendServerOutput(stream: string, chunk: Buffer): void {
    if (this.serverOutputTruncated) {
      return;
    }
    const remainingBytes =
      SERVER_OUTPUT_MAXIMUM_BYTES - Buffer.byteLength(this.serverOutput);
    if (remainingBytes <= 0) {
      this.serverOutputTruncated = true;
      return;
    }
    const boundedChunk = chunk.subarray(0, remainingBytes).toString('utf8');
    this.serverOutput += `[${stream}] ${boundedChunk}`;
    if (chunk.byteLength > remainingBytes) {
      this.serverOutputTruncated = true;
    }
  }

  private diagnosticContext(): Readonly<
    Record<string, number | string | null>
  > {
    return Object.freeze({
      configuration_path: this.configurationPath,
      executable_path: this.executablePath,
      process_id: this.activeChild?.pid ?? null,
      scenario_path: this.scenarioPath,
      server_output: this.serverOutput,
      server_output_truncated: this.serverOutputTruncated ? 1 : 0,
    });
  }

  private async forceCleanupActiveProcess(): Promise<void> {
    const child = this.requireActiveChild();
    const processId = child.pid;
    const exitPromise = this.exitPromise;
    if (processId === undefined || exitPromise === null) {
      this.activeChild = null;
      this.exitPromise = null;
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_CLEANUP_STATE_INVALID',
        'The active server cannot be cleaned up without a process ID and exit observer.',
        this.diagnosticContext(),
      );
    }

    if (child.exitCode === null && child.signalCode === null) {
      try {
        process.kill(-processId, 'SIGKILL');
      } catch (cause) {
        if ((cause as NodeJS.ErrnoException).code !== 'ESRCH') {
          throw new BrowserE2EError(
            'BROWSER_E2E.SERVER_FORCE_CLEANUP_FAILED',
            'Failed to send SIGKILL to the server process group.',
            this.diagnosticContext(),
            cause,
          );
        }
      }
    }

    try {
      await waitForProcessExit(
        exitPromise,
        SERVER_FORCE_CLEANUP_TIMEOUT_MILLISECONDS,
        'BROWSER_E2E.SERVER_FORCE_CLEANUP_TIMEOUT',
      );
    } finally {
      this.activeChild = null;
      this.exitPromise = null;
    }
  }

  private requireActiveChild(): ChildProcess {
    if (this.activeChild === null) {
      throw new BrowserE2EError(
        'BROWSER_E2E.SERVER_NOT_ACTIVE',
        'The E2E server process is not active.',
        this.diagnosticContext(),
      );
    }
    return this.activeChild;
  }
}
