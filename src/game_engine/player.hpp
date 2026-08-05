#ifndef _PLAYER_HPP_
#define _PLAYER_HPP_

#include <vector>
#include <string>

// boost-headers
#include "boost-json.hpp"

// game-state
#include "game_piece.hpp"




class Player : public GamePiece
{
public:
  Player();
  Player(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  Player(const int& id, std::vector<std::string> row);
  ~Player() override;
  
  boost::json::object getGamePieceJson() override;
  bool is_stationary() override;
};


#endif
