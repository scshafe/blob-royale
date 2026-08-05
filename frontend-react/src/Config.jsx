import axios from 'axios';

function Config({ onConfigReceived }) {
  const getGameConfig = async () => {
    const response = await axios.get('game-config');
    const gameConfig = new Map(Object.entries(response.data));

    gameConfig.set(
      'part_height',
      gameConfig.get('height') / gameConfig.get('part_rows'),
    );
    gameConfig.set(
      'part_width',
      gameConfig.get('width') / gameConfig.get('part_cols'),
    );

    onConfigReceived(gameConfig);
  };

  return (
    <div className="Config">
      <button type="button" onClick={getGameConfig}>
        get config
      </button>
    </div>
  );
}

export default Config;
