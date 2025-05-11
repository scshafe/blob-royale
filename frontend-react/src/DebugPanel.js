import React from 'react';
import axios from 'axios';

import './style/DebugPanel.css';


function DebugPanel({players, config, started}) {



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
          { config.keys().toArray().map( k => (
            <tr>
              <th>{k}</th>
              <th>{config.get(k)}</th>
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
          { players.map( p => (
            <tr>
            <th scope="row">{p.id}</th>
            <td>({p.pos.x}, {p.pos.y})</td>
            <td>({p.vel.x}, {p.vel.y})</td>
            <td>({p.main_part.row}, {p.main_part.col})</td>
            </tr>
          ))}
        </tbody>
    </table>

    </div>
  );

}

export default DebugPanel;
