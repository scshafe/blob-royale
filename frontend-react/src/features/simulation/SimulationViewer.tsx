import type { SimulationConnectionState } from './useSimulationConnection';
import { SimulationCanvas } from './SimulationCanvas';
import { SimulationDebugPanel } from './SimulationDebugPanel';

export interface SimulationViewerProps {
  readonly connection: SimulationConnectionState;
}

const connectionStatusLabels = Object.freeze({
  connected: 'Connected to the read-only snapshot stream.',
  connecting: 'Connecting to the snapshot stream…',
  failed: 'The simulation viewer could not connect.',
  loading_configuration: 'Loading public simulation configuration…',
  retrying: 'The snapshot stream disconnected. Retrying with bounded backoff…',
});

/** Renders connection state without exposing commands or lifecycle controls. */
export function SimulationViewer({ connection }: SimulationViewerProps) {
  const statusLabel = connectionStatusLabels[connection.status];

  return (
    <section aria-labelledby="simulation-viewer-heading">
      <h1 id="simulation-viewer-heading">Blob Royale</h1>
      <p
        aria-live={connection.status === 'failed' ? 'assertive' : 'polite'}
        role={connection.status === 'failed' ? 'alert' : 'status'}
      >
        {statusLabel}
      </p>
      {connection.error === null ? null : (
        <p className="ConnectionError">
          {connection.error.code}: {connection.error.message}
        </p>
      )}
      {connection.configuration === null ? null : (
        <div className="SimulationLayout">
          <SimulationCanvas
            configuration={connection.configuration}
            snapshot={connection.snapshot?.data ?? null}
          />
          <SimulationDebugPanel
            configuration={connection.configuration}
            snapshot={connection.snapshot}
          />
        </div>
      )}
    </section>
  );
}
