import './style/DebugPanel.css';

function DebugPanel({ players, config }) {
  return (
    <div className="DebugPanel">
      <table className="ConfigTable">
        <thead>
          <tr>
            <th scope="col">Config</th>
            <th scope="col">Value</th>
          </tr>
        </thead>
        <tbody>
          {Array.from(config.entries()).map(([key, value]) => (
            <tr key={key}>
              <th scope="row">{key}</th>
              <td>{value}</td>
            </tr>
          ))}
        </tbody>
      </table>

      <table className="PlayerTable">
        <thead>
          <tr>
            <th scope="col">Player</th>
            <th scope="col">Position</th>
            <th scope="col">Velocity</th>
            <th scope="col">Part</th>
          </tr>
        </thead>
        <tbody>
          {players.map((player) => (
            <tr key={player.id}>
              <th scope="row">{player.id}</th>
              <td>
                ({player.pos.x}, {player.pos.y})
              </td>
              <td>
                ({player.vel.x}, {player.vel.y})
              </td>
              <td>
                ({player.mainPart.row}, {player.mainPart.col})
              </td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}

export default DebugPanel;
