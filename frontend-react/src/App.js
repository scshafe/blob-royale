import  axios  from 'axios';
import  React from 'react';
import logo from './logo.svg';
import './App.css';
import GameCanvas from './GameCanvas';
import Player from './Player.js';
import Config from './Config.js';
import DebugPanel from './DebugPanel.js';

function App() {
  const [players, setPlayers] = React.useState([]);
  const [fetchInterval, setFetchInterval] = React.useState(null);
  const [config, setConfig] = React.useState(new Map());
  const [started, setStarted] = React.useState(false);
  const [running, setRunning] = React.useState(false);

  axios.defaults.baseURL = "http://192.168.86.12:8000";

  
  const pauseSim = () => {
    clearInterval(fetchInterval);
    setFetchInterval(null);
    axios.get("pause-sim")
    .then((response) => {
        console.log("pause-sim response: ", response);
        setRunning(false);
      });
  }


  const startSim = () => {
    axios.get("start-sim")
      .then((response) => {
        console.log("start-sim response: ", response);
        setStarted(true);
      })
      .then(() => {
        let tmp = setInterval(getGameState, config.get("interval"));
        setFetchInterval(tmp);
        setRunning(true);
      });
      

  };


  const build_player = (gp) => {
    let new_player = new Player(gp);
    console.log("player: ", new_player);
    return new_player;
  };

  const receiveConfig = (new_config) => {
    const tmp = new Map(new_config);
    console.log("Updating config: ", new_config);
    setConfig(new_config);
  };




  const getGameState = () => {
    axios.get("game-state")
      .then((response) => {
        return response.data;
      })
      .then(data => {
        let tmp_players = [];

        data.map( p => {
          if (p["type"] === "player")
          {
            let new_player = new Player(p["gamepiece"], config.get("radius"));
            tmp_players.push(new_player);
          }
        });
        setPlayers(tmp_players);
      })
      .catch((err) => {
        console.log("Failed to fetch game-state");
      });
  };

  return (
    <div className="App">
      <Config players={players} config={config} onConfigReceived={receiveConfig} />

      <DebugPanel players={players} config={config} started={started} />
      
      { !started  ?
        (<button onClick={startSim}>start</button>) :
        (
          fetchInterval === null ? 
          (<button onClick={startSim}>resume</button>) :
          (<button onClick={pauseSim}>pause</button>)
        )
      }
      <ul>
        {players.map(player => (
          <li key={player.num}>num: {player.num}, coordinates: ({player.x_pos}, {player.y_pos})</li>
        ))}
      </ul>

      <GameCanvas players={players} config={config} />
    </div>
  );
}

export default App;
