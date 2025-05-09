import axios from 'axios';
import React from 'react';

const player_color_coding = ['red', 'green', 'orange', 'blue', 'yellow', 'purple'];

/* Props format:
 * - players := []
 *
 */
function GameCanvas({players, height, width}) {
  const canvasRef = React.useRef(null);

  React.useEffect(() => {
    const canvas = canvasRef.current;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, 1200, 800);

    players.map( player => {
      ctx.beginPath();
      ctx.arc(player.pos.x, player.pos.y, player.radius, 0, 2 * Math.PI);
      ctx.fillStyle = player_color_coding[player.id];
      ctx.fill();
      ctx.strokeStyle = "green";
      ctx.stroke();
    });

    
  }, [players]);



  return <canvas style={{border: 2+"px solid"}} ref={canvasRef} width={width} height={height} />;
}

export default GameCanvas;
