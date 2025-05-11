



class Cell {
  constructor(json) {
    this.row = json[0];
    this.col = json[1];
  }

  draw_cell(ctx, config) {
    let part_width  = config.get("width")  / config.get("part_cols");
    let part_height = config.get("height") / config.get("part_rows");

    let x = part_width  * this.col;
    let y = part_height * this.row;

    console.log("cell: ", this);
    console.log(x, y, part_width, part_height);

    ctx.globalAlpha = 0.5;
    ctx.rect(x, y, part_width, part_height);
    ctx.fillStyle = 'red';
    ctx.strokeStyle = 'black';
    ctx.stroke();
  }

}

//function Cell({row, cell})

export default Cell;
