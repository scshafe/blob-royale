import axios  from 'axios';
import axiosRetry from 'axios-retry';
import React from 'react';

// local
import './App.css';
import GameCanvas from './GameCanvas';
import Player from './Player.js';
import Config from './Config.js';
import DebugPanel from './DebugPanel.js';



function App() {
  const [players, setPlayers] = React.useState([]);
  //const [fetchInterval, setFetchInterval] = React.useState(null);
  const [gameTickInterval,  setGameTickInterval]  = React.useState(null);
  const [config, setConfig] = React.useState(new Map());
  const [started, setStarted] = React.useState(false);
  const [running, setRunning] = React.useState(false);
  const [connected, setConnected] = React.useState(false);
  const ws = React.useRef(null);

  axios.defaults.baseURL = "http://192.168.86.12:8000";

  
  const pauseSim = () => {
    clearInterval(gameTickInterval);
    setGameTickInterval(null);
    setRunning(false);
    ws.current.close();

    axios.get("pause-sim")
    .then((response) => {
        console.log("pause-sim response: ", response);
      });
  }


  const send_game_inputs = () => {
    const msg = {
      acc: [0.7, 1.1]
    };
    ws.current.send(JSON.stringify(msg));
  };

  const receive_game_inputs = (event) => {

    let data = JSON.parse(event.data);
    console.log("receiving: ", data);
    let tmp_players = [];
    data.map( p => {
      if (p["type"] === "player")
      {
        let new_player = new Player(p["gamepiece"], config.get("radius"));
        tmp_players.push(new_player);
      }
    });
    setPlayers(tmp_players);
  }


  // try again without the start-sim endpoint, see what happens?
  //React.useEffect(() => {
  const connectToServer = () => {
    ws.current = new WebSocket(
      "ws://192.168.86.12:8000/start-sim"
    );
    ws.current.onopen = () => {
      console.log("websocket connected to server");
      setConnected(true);
    }
    ws.current.onmessage = receive_game_inputs;

  };
      



  const startSim = () => {
    axios.get("start-sim")
      .then((response) => {
        console.log("start-sim response: ", response);
        setStarted(true);
      })
      .then(() => {
        let tmp = setInterval(send_game_inputs, config.get("interval"));
        setGameTickInterval(tmp);
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

  if (config.size === 0)
  return (
    <div className="App">
      <Config players={players} config={config} onConfigReceived={receiveConfig} />
    </div>
  );

  if (!connected)
  return (
    <div className="App">
      <Config players={players} config={config} onConfigReceived={receiveConfig} />
      <DebugPanel players={players} config={config} started={started} />
      <button onClick={connectToServer}>connect to server</button>
    </div>
  );


  return (
    <div className="App">
      <Config players={players} config={config} onConfigReceived={receiveConfig} />

      <DebugPanel players={players} config={config} started={started} />
   
      { !started && !running ?
        (<button onClick={startSim}>start</button>) :
        (
          !running ? 
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





