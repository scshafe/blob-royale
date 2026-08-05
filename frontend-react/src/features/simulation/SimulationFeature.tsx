import { SimulationViewer } from './SimulationViewer';
import { useSimulationConnection } from './useSimulationConnection';

/** Connects the canonical hook to otherwise presentational viewer components. */
export function SimulationFeature() {
  const connection = useSimulationConnection();
  return <SimulationViewer connection={connection} />;
}
