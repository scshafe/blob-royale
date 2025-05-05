#ifndef _PLAYER_HPP_
#define _PLAYER_HPP_

#include "game_piece.hpp"


class MapObject : public GamePiece
{
public:
  MapObject();

private:
};

class Player : public GamePiece
{
public:
  Player();
  Player(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  

};


#endif
