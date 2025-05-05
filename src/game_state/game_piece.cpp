#include "game_piece.hpp"


GamePiece::GamePiece(float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
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

std::string GamePiece::jsonify_pos()
{
  std::stringstream ss;
  ss << "{" << position.x << ", " << position.y << "}";
  return ss.str();
}
