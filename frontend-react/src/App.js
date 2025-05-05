import  axios  from 'axios';
import  React from 'react';
import logo from './logo.svg';
import './App.css';
import GameCanvas from './GameCanvas';


function App() {

  const [count, setCount] = React.useState(null);
  const [playerLocs, setPlayerLocs] = React.useState([]);

  axios.defaults.baseURL = "http://192.168.86.12:8000";

  React.useEffect(() => {
    axios.get("react-count")
         .then((response) => {
          setCount(response.data);
         });
  }, []);

  const getNewCount = () => {
    axios.get("react-count")
      .then((response) => {
        setCount(response.data);
      });
  };

  React.useEffect(() => {
    axios.get("game-state")
      .then((response) => {
        return response.data;
      })
      .then(data => {
        setPlayerLocs(data);
      });
  }, []);

  if (!count) return null;

  return (
    <div className="App">


      <h1>{count}</h1>
      <button onClick={getNewCount}>get updated count</button>
      <ul>
        {playerLocs.map(playerLoc => (
          <li key={playerLoc.playerNum}>{playerLoc.x}, {playerLoc.y}</li>
        ))}
      </ul>

      <GameCanvas />
    </div>
  );
}

export default App;
