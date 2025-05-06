#include "boost_config.hpp"
#include "game_piece.hpp"


GamePiece::GamePiece(int id_, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  id(id_),
  position(x, y),
  velocity(vel_x, vel_y),
  acceleration(accel_x, accel_y)
{}

std::ostream& operator<<(std::ostream& os, const GamePiece& gp)
{
  os << "GamePiece:" << std::endl 
     << "--position: " << gp.position << std::endl 
     << "--velocity: " << gp.velocity << std::endl;
}

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


PhyVector GamePiece::detect_collision(const GamePiece& other)
{
  float x_dist = position.x - other.position.x;
  float y_dist = position.y - other.position.y;
  PhyVector dist(x_dist, y_dist);
  return dist;
}

void GamePiece::run_sim()
{
  for (auto partition : piece_partitions)
  {
    std::vector<GamePiece*> nearby_pieces = partition->get_game_pieces();
    for (auto nearby : nearby_pieces)
    {
      if (nearby == this)
      {
        continue;
      }
      PhyVector possible_collision = detect_collision(*nearby);
      if (possible_collision.get_magnitude() < 10)
      {
        BOOST_LOG_TRIVIAL(info) << "possible_collision: " << possible_collision;
        BOOST_LOG_TRIVIAL(info) << *this;
        velocity.x = -velocity.x;
        velocity.y = -velocity.y;
      }

    }
  }

  position.x += velocity.x;
  position.y += velocity.y;

  velocity.x += acceleration.x;
  velocity.y += acceleration.y;
}



