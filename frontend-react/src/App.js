import  axios  from 'axios';
import  React from 'react';
import logo from './logo.svg';
import './App.css';
import GameCanvas from './GameCanvas';
import Player from './Player.js';
import Config from './Config.js'


function App() {

  const [count, setCount] = React.useState(null);
  const [players, setPlayers] = React.useState([]);
  const [fetchInterval, setFetchInterval] = React.useState(null);
  const [config, setConfig] = React.useState(new Map());
  const [started, setStarted] = React.useState(false);

  axios.defaults.baseURL = "http://192.168.86.12:8000";

  const getNewCount = () => {
    axios.get("react-count")
      .then((response) => {
        setCount(response.data);
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
  }




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
            let new_player = new Player(p["gamepiece"]);
            tmp_players.push(new_player);
          }
        });
        setPlayers(tmp_players);
      })
      .catch((err) => {
        console.log("Failed to fetch game-state");
      });
  };

  const pauseFetching = () => {
    clearInterval(fetchInterval);
    setFetchInterval(null);
  }

  const beginFetching = () => {
    let tmp = setInterval(getGameState, 500);
    setFetchInterval(tmp);
  }

  

  return (
    <div className="App">
      <Config config={config} onConfigReceived={receiveConfig} />

      <div>
        Config
        <ul>
          { config.forEach( (value, key) => (
            <li key={value}>{value}: {key}</li>
          ))}
        </ul>
      </div>
      { fetchInterval === null ? 
        (<button onClick={beginFetching}>fetch</button>) :
        (<button onClick={pauseFetching}>pause</button>)
      }
      <ul>
        {players.map(player => (
          <li key={player.num}>num: {player.num}, coordinates: ({player.x_pos}, {player.y_pos})</li>
        ))}
      </ul>

      <GameCanvas players={players} height={config.get("height")} width={config.get("width")} />
    </div>
  );
}

export default App;
