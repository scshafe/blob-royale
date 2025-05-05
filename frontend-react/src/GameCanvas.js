import axios from 'axios';
import React from 'react';


function GameCanvas() {
  const canvasRef = React.useRef(null);

  React.useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas.getContext('2d');

    // Draw a rectangle
    context.fillStyle = 'green';
    context.fillRect(10, 10, 100, 50);

    const player = canvas.getContext('2d');
    player.fillStyle = 'blue';
    player.beginPath();
    player.arc(300, 400, 20, 0, 2 * Math.PI);
    player.stroke();
  }, []);

  return <canvas ref={canvasRef} width={1200} height={800} />;
}

export default GameCanvas;
