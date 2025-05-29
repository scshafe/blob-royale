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

        Object.keys(data).forEach( key => {
          tmp.set(key, data[key]);
        });

        tmp.set("part_height", tmp.get("height") / tmp.get("part_rows"));
        tmp.set("part_width" , tmp.get("width") / tmp.get("part_cols"));

        onConfigReceived(tmp); // callback passed to child
      })
  };

  //if (config.size === 0) getGameConfig();


  return (
    <div className="Config">
      <button onClick={getGameConfig}>get config</button>

    </div>
  );

}

export default Config;
