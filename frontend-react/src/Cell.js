class Cell {
  constructor(json) {
    this.row = json[0];
    this.col = json[1];
  }

  draw(context, config) {
    const partitionWidth = config.get('width') / config.get('part_cols');
    const partitionHeight = config.get('height') / config.get('part_rows');
    const x = partitionWidth * this.col;
    const y = partitionHeight * this.row;

    context.globalAlpha = 0.5;
    context.rect(x, y, partitionWidth, partitionHeight);
    context.fillStyle = 'red';
    context.strokeStyle = 'black';
    context.stroke();
  }
}

export default Cell;
