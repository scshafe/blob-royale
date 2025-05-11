import axios from 'axios';
import React from 'react';


/* Props format:
 * - players := []
 *
 */
function GameCanvas({players, config}) {
  const canvasRef = React.useRef(null);

  React.useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, config.get("width"), config.get("height"));

    players.map( player => {
      player.draw_player(ctx, config);
    });
  }, [players]);



  return (
  <div className="GameCanvas">
    { config === null ?
      (<h3>Config is not set yet</h3>) :
      (<canvas style={{border: 2+"px solid"}} ref={canvasRef} width={config.get("width")} height={config.get("height")} />)
    }
  </div>);

}

export default GameCanvas;
