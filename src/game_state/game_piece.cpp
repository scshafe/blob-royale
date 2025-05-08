#include "boost-log.hpp"
#include "boost-json.hpp"
#include "game_engine_parameters.hpp"
#include "game_piece.hpp"


GamePiece::GamePiece(int id_, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  id(id_),
  position(x, y),
  velocity(vel_x, vel_y),
  acceleration(accel_x, accel_y)
{}

std::ostream& operator<<(std::ostream& os, const GamePiece& gp)
{
  os << "GamePiece: (" 
     << "--pos: " << gp.position
     << "--vel: " << gp.velocity << ")";
  return os;
}

PhyVector GamePiece::get_position()
{
  return position;
}


PhyVector GamePiece::get_velocity()
{
  return velocity;
}


PhyVector GamePiece::get_acceleration()
{
  return acceleration;
}


void GamePiece::add_partition(Partition* partition)
{
  BOOST_LOG_TRIVIAL(info) << "GamePiece::add_partition()";
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

boost::json::object GamePiece::getGamePieceJson()
{
  boost::json::object root;

  root["id"] = id;
  root["pos"] = position.getPhyVectorJson();
  root["vel"] = velocity.getPhyVectorJson();
  root["acc"] = acceleration.getPhyVectorJson();
  return root;
}

PhyVector GamePiece::phy_vector_to_other_player(const GamePiece& other)
{
  return PhyVector(position.x - other.position.x, position.y - other.position.y);
}

bool GamePiece::detect_player_on_player_collision(const GamePiece& other)
{
  PhyVector collision_vector = phy_vector_to_other_player(other);
  if (collision_vector.get_magnitude() > PLAYER_ON_PLAYER_COLLISION)
  {
    return false;
  }

  float dot_product = velocity.dot_product(collision_vector);
  BOOST_LOG_TRIVIAL(info) << "dot product: " << dot_product;
  return dot_product > 0;
         
}

void GamePiece::handle_player_on_player_collision(const GamePiece& other)
{
  BOOST_LOG_TRIVIAL(trace) << "collision between players " << id << " and " << other.id;
  PhyVector collision_vector = phy_vector_to_other_player(other);
  velocity = collision_vector.get_inverse();
}

void GamePiece::handle_possible_collision_with_wall()
{
  if ((position.x <=             PLAYER_ON_WALL_COLLISION && velocity.x < 0.0) || 
      (position.x >= MAP_WIDTH - PLAYER_ON_WALL_COLLISION && velocity.x > 0.0))
  {
    BOOST_LOG_TRIVIAL(info) << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    velocity.x = -velocity.x;
  }
  if ((position.y <=              PLAYER_ON_WALL_COLLISION && velocity.y < 0.0) ||
      (position.y >= MAP_HEIGHT - PLAYER_ON_WALL_COLLISION && velocity.y > 0.0))
  {
    BOOST_LOG_TRIVIAL(info) << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    velocity.y = -velocity.y;
  }
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
      if (detect_player_on_player_collision(*nearby))
      {
        BOOST_LOG_TRIVIAL(info) << "detected player on player collision";
        handle_player_on_player_collision(*nearby);
      }
      handle_possible_collision_with_wall();
    }
  }

  position.x += velocity.x;
  position.y += velocity.y;

//  velocity.x += acceleration.x;
//  velocity.y += acceleration.y;
}



