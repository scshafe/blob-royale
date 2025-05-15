
#include "boost-log.hpp"

#include "player.hpp"



Player::Player() :
  GamePiece()
{}

Player::Player(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  GamePiece( id, x,  y,  vel_x,  vel_y,  accel_x,  accel_y)
{}

Player::Player(const int& id, std::vector<std::string> row) :
  GamePiece(id, row)
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


bool Player::is_stationary()
{
  return false;
}
