
#include "boost-log.hpp"

#include "player.hpp"



Player::Player() :
  GamePiece()
{}

Player::Player(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  GamePiece( id, x,  y,  vel_x,  vel_y,  accel_x,  accel_y)
{}

Player::Player(std::vector<std::string> row) :
  GamePiece(row)
{}

Player::~Player()
{
  TRACE << "deleting player " << id;
}

boost::json::object Player::getGamePieceJson()
{
  boost::json::object root;

  root["type"] = "player";
  root["gamepiece"] = GamePiece::getGamePieceJson();
  return root;
}

