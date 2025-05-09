import React from 'react';
import axios from 'axios';




function Config({config, onConfigReceived }) {

  const getGameConfig = () => {
    axios.get("game-config")
      .then( response => {
        return response.data;
      })
      .then( data => {
        const tmp =  new Map();
        tmp.set("width", data["width"]);
        tmp.set("height", data["height"]);
        tmp.set("radius", data["radius"]);
        tmp.set("interval", data["interval"]);

        onConfigReceived(tmp); // callback passed to child
      })
  };


  return (
    <div className="Config">
      <button onClick={getGameConfig}>get config</button>

      <h3>Config parameters</h3>
        <ul>
          { config.keys().toArray().map( k => (<li key={k}> {k}: {config.get(k)}</li>))}
        </ul>
    </div>
  );

}

export default Config;
