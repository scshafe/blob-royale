#include "game_piece.hpp"

#include <boost/json.hpp>

GamePiece::GamePiece(int id_, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  id(id_),
  position(x, y),
  velocity(vel_x, vel_y),
  acceleration(accel_x, accel_y)
{}



void GamePiece::add_partition(Partition* partition)
{
  piece_partitions.push_back(partition);
}

void GamePiece::remove_partition(Partition* partition)
{
  for (int i = 0; i < piece_partitions.size(); i++)
  {
    if (partition == piece_partitions[i])
    {
      piece_partitions.erase(piece_partitions.begin() + i);
      break;
    }
  }
}

boost::json::object GamePiece::getJson()
{
  boost::json::object root;

  root["id"] = id;
  root["x_pos"] = position.x;
  root["y_pos"] = position.y;
  root["x_vel"] = velocity.x;
  root["y_vel"] = velocity.y;
  root["x_acc"] = acceleration.x;
  root["y_acc"] = acceleration.y;
  return root;
}

std::string GamePiece::JsonStringify()
{

  std::stringstream ss;
  
  ss << "{\"id\":" << id << ", ";
  ss << "\"x_pos\": " << position.x << ", ";
  ss << "\"y_pos\": " << position.y << ", ";
  ss << "\nx_vel\": " << velocity.x << ", ";
  ss << "\ny_vel\": " << velocity.x << ", ";
  ss << "\nx_acc\": " << acceleration.x << ", ";
  ss << "\ny_acc\": " << acceleration.x << "}";
  return ss.str();
}
