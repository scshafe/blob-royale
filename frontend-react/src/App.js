import  axios  from 'axios';
import  React from 'react';
import logo from './logo.svg';
import './App.css';
import GameCanvas from './GameCanvas';

class Player {
  constructor(num, x_pos, y_pos) {
    this.num = num;
    this.x_pos = x_pos;
    this.y_pos = y_pos;
    this.radius = 20;
  }

}


function App() {

  const [count, setCount] = React.useState(null);
  const [players, setPlayers] = React.useState([]);
  const [fetchInterval, setFetchInterval] = React.useState(null);

  axios.defaults.baseURL = "http://192.168.86.12:8000";

//  React.useEffect(() => {
//    axios.get("react-count")
//         .then((response) => {
//          setCount(response.data);
//         });
//  }, []);

  const getNewCount = () => {
    console.log("players: ", players);
    axios.get("react-count")
      .then((response) => {
        setCount(response.data);
      });
  };



  const getGameState = () => {
    axios.get("game-state")
      .then((response) => {
        return response.data;
      })
      .then(data => {
        console.log(data);
        let tmp_players = [];

        data.map( p => {
          let new_player = new Player(p['id'], 
                                      p['x_pos'],
                                      p['y_pos'], 
                                      p['x_vel'],
                                      p['x_vel'],
                                      p['x_acc'],
                                      p['y_acc'],
                                      );

          console.log("tmp: ", new_player);
          tmp_players.push(new_player);
        });
        console.log("tmp_players: ", tmp_players);
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

//  React.useEffect(() => {
//    beginFetching();
//  }, []);

//  if (players === null) {
//    console.log("Error! players === null");
//    return null;
//  }

  return (
    <div className="App">


      <h1>{count}</h1>
      <button onClick={getNewCount}>get updated count</button>
      
      { fetchInterval === null ? 
        (<button onClick={beginFetching}>fetch</button>) :
        (<button onClick={pauseFetching}>pause</button>)
      }
      <ul>
        {players.map(player => (
          <li key={player.num}>num: {player.num}, coordinates: ({player.x_pos}, {player.y_pos})</li>
        ))}
      </ul>

      <GameCanvas players={players} />
    </div>
  );
}

export default App;
