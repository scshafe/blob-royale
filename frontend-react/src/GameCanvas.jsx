import { useEffect, useRef } from 'react';

function GameCanvas({ players, config }) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    const context = canvas.getContext('2d');
    context.clearRect(0, 0, config.get('width'), config.get('height'));

    players.forEach((player) => player.draw(context, config));
  }, [config, players]);

  if (config === null) {
    return <h3>Config is not set yet</h3>;
  }

  return (
    <div className="GameCanvas">
      <canvas
        aria-label="Game field"
        ref={canvasRef}
        width={config.get('width')}
        height={config.get('height')}
      />
    </div>
  );
}

export default GameCanvas;
