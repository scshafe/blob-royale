import PhyVector from './PhyVector.js';


class Player {
  constructor(json) {
    console.log("Player Constructor");
    this.id = json["id"];
    this.pos = new PhyVector(json["pos"]);
    this.vel = new PhyVector(json["vel"]);
    this.acc = new PhyVector(json["acc"]);
    this.radius = 10;

  }



}

export default Player;
