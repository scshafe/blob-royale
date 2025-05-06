import axios from 'axios';
import React from 'react';

const player_color_coding = ['red', 'green', 'orange', 'blue', 'yellow', 'purple'];

/* Props format:
 * - players := []
 *
 */
function GameCanvas(props) {
  const canvasRef = React.useRef(null);
  const players = props.players;

  React.useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, 1200, 800);

    players.map( player => {
      console.log('player: ', player.num, player.x_pos, player.y_pos);
      //ctx.fillStyle = player_color_coding[parseInt(player.num)];
      ctx.beginPath();
      ctx.arc(player.x_pos, player.y_pos, player.radius, 0, 2 * Math.PI);
      ctx.fillStyle = "red";
      ctx.fill();
      ctx.strokeStyle = "green";
      ctx.stroke();
    });

    
  }, [players]);



  return <canvas style={{border: 2+"px solid"}} ref={canvasRef} width={1200} height={800} />;
}

export default GameCanvas;
