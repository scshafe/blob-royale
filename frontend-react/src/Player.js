import PhyVector from './PhyVector.js';
import Cell from './Cell.js';



const player_color_coding = ['red', 'green', 'orange', 'blue', 'yellow', 'purple'];

class Player {
  constructor(json, radius) {
    console.log("Player Constructor", json);
    this.id = json["id"];
    this.pos = new PhyVector(json["pos"]);
    this.vel = new PhyVector(json["vel"]);
    this.acc = new PhyVector(json["acc"]);
    this.radius = 10;
    //this.radius = Math.floor(radius);
    this.main_part = new Cell(json["main_part"]);
    this.parts = new Array();
    json["parts"].map( p => {
      this.parts.push(new Cell(p));
    });
    console.log("Player: ", this);

  }

  draw_player(ctx, config) {
    ctx.beginPath();
    ctx.arc(Math.floor(this.pos.x), Math.floor(this.pos.y), this.radius, 0, 2 * Math.PI);
    ctx.fillStyle = player_color_coding[this.id];
    ctx.fill();
    ctx.strokeStyle = "green";
    ctx.stroke();

    this.parts.map( p => {
      p.draw_cell(ctx, config);
    });
  }

}

export default Player;
