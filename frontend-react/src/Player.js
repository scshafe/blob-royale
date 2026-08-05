import Cell from './Cell.js';
import PhyVector from './PhyVector.js';

const PLAYER_COLORS = ['red', 'green', 'orange', 'blue', 'yellow', 'purple'];
const PLAYER_RADIUS = 10;

class Player {
  constructor(json) {
    this.id = json.id;
    this.pos = new PhyVector(json.pos);
    this.vel = new PhyVector(json.vel);
    this.acc = new PhyVector(json.acc);
    this.radius = PLAYER_RADIUS;
    this.mainPart = new Cell(json.main_part);
    this.parts = json.parts.map((part) => new Cell(part));
  }

  draw(context, config) {
    context.beginPath();
    context.arc(
      Math.floor(this.pos.x),
      Math.floor(this.pos.y),
      this.radius,
      0,
      2 * Math.PI,
    );
    context.fillStyle = PLAYER_COLORS[this.id];
    context.fill();
    context.strokeStyle = 'green';
    context.stroke();

    this.parts.forEach((part) => part.draw(context, config));
  }
}

export default Player;
