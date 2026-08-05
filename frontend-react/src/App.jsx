import axios from 'axios';
import { useRef, useState } from 'react';

import './App.css';
import Config from './Config.jsx';
import DebugPanel from './DebugPanel.jsx';
import GameCanvas from './GameCanvas.jsx';
import Player from './Player.js';

const SERVER_HTTP_BASE_URL = 'http://192.168.86.12:8000';
const SERVER_WEBSOCKET_URL = 'ws://192.168.86.12:8000/start-sim';

axios.defaults.baseURL = SERVER_HTTP_BASE_URL;

function App() {
  const [players, setPlayers] = useState([]);
  const [gameTickInterval, setGameTickInterval] = useState(null);
  const [config, setConfig] = useState(new Map());
  const [started, setStarted] = useState(false);
  const [running, setRunning] = useState(false);
  const [connected, setConnected] = useState(false);
  const webSocket = useRef(null);

  const sendGameInputs = () => {
    const message = { acc: [0.7, 1.1] };
    webSocket.current.send(JSON.stringify(message));
  };

  const receiveGameInputs = (event) => {
    const gameObjects = JSON.parse(event.data);
    const nextPlayers = gameObjects
      .filter((gameObject) => gameObject.type === 'player')
      .map((gameObject) => new Player(gameObject.gamepiece));

    setPlayers(nextPlayers);
  };

  const connectToServer = () => {
    webSocket.current = new WebSocket(SERVER_WEBSOCKET_URL);
    webSocket.current.onopen = () => setConnected(true);
    webSocket.current.onmessage = receiveGameInputs;
  };

  const startSimulation = () => {
    axios.get('start-sim').then(() => {
      setStarted(true);
      setGameTickInterval(setInterval(sendGameInputs, config.get('interval')));
      setRunning(true);
    });
  };

  const pauseSimulation = () => {
    clearInterval(gameTickInterval);
    setGameTickInterval(null);
    setRunning(false);
    webSocket.current.close();

    axios.get('pause-sim');
  };

  if (config.size === 0) {
    return (
      <main className="App">
        <Config onConfigReceived={setConfig} />
      </main>
    );
  }

  if (!connected) {
    return (
      <main className="App">
        <Config onConfigReceived={setConfig} />
        <DebugPanel players={players} config={config} />
        <button type="button" onClick={connectToServer}>
          connect to server
        </button>
      </main>
    );
  }

  const simulationButton =
    !started && !running ? (
      <button type="button" onClick={startSimulation}>
        start
      </button>
    ) : !running ? (
      <button type="button" onClick={startSimulation}>
        resume
      </button>
    ) : (
      <button type="button" onClick={pauseSimulation}>
        pause
      </button>
    );

  return (
    <main className="App">
      <Config onConfigReceived={setConfig} />
      <DebugPanel players={players} config={config} />
      {simulationButton}
      <ul>
        {players.map((player) => (
          <li key={player.id}>
            num: {player.id}, coordinates: ({player.pos.x}, {player.pos.y})
          </li>
        ))}
      </ul>
      <GameCanvas players={players} config={config} />
    </main>
  );
}

export default App;
