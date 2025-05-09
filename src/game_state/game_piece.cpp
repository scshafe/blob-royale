#include "boost-log.hpp"
#include "boost-json.hpp"
#include "game_engine_parameters.hpp"
#include "game_piece.hpp"
#include "game_state.hpp"


GamePiece::GamePiece() :
  id(0),
  position(0.0, 0.0),
  velocity(0.0, 0.0),
  acceleration(0.0, 0.0),
  current_part(nullptr),
  parts(std::set<std::shared_ptr<Partition>>()),
  fixed(false)
{
  ERROR << "GamePiece() constructor should not be called.";
  //exit(1);
}

GamePiece::GamePiece(int id_, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y) :
  id(id_),
  position(x, y),
  velocity(vel_x, vel_y),
  acceleration(accel_x, accel_y)
{
  ENTRANCE << "GamePiece(int id_, float x, ...)";
}

GamePiece::GamePiece(std::vector<std::string> row) :
  current_part(nullptr),
  parts()
{
  ENTRANCE << "GamePiece(row)";
  try
  {
    std::string vals;
    for (auto i : row) vals += i + " ";
    TRACE << "row is: " << vals;

    id = std::stoi(row[1]);
    position.x = std::stof(row[2]);
    position.y = std::stof(row[3]);
    velocity.x = std::stof(row[4]);
    velocity.y = std::stof(row[5]);
    acceleration.x = std::stof(row[6]);
    acceleration.y = std::stof(row[7]);
  }
  catch (...)
  {
    ERROR << "Fatal: unable to create person from row";
    exit(1);
  }
  TRACE << "YEET";
  


  update_partitions();
}

GamePiece::~GamePiece()
{
  TRACE << "Deconstructing GamePiece: " << id;
}

std::ostream& operator<<(std::ostream& os, const GamePiece& gp)
{
  os << "GamePiece: (" 
     << "--pos: " << gp.position
     << "--vel: " << gp.velocity << ")";
  return os;
}

PhyVector GamePiece::get_position() const
{
  return position;
}


PhyVector GamePiece::get_velocity() const
{
  return velocity;
}


PhyVector GamePiece::get_acceleration() const
{
  return acceleration;
}


/*
 * Need to:
 * 
 * - set parts to be new_parts
 * - remove self from the parts which were in only (old) parts
 * 
 *  - to do this, make sure we use only heaps
 *
 *  - create empty heap
 *  - get new_parts heap
 *  - for old_part in old_parts:
 *      - if old_part NOT in new_parts:
 *          - remove self from 
 *  - set old_parts to new_parts
 *
 */

void GamePiece::update_partitions()
{
  ENTRANCE << "update_partitions";
  std::shared_ptr<Partition> tmp = GameState::get_instance()->get_partition(this);
  TRACE << "yup";
  if (current_part != tmp) // if this doesn't change no need to update the set which is costly
  {
    return;
  }

  std::set<std::shared_ptr<Partition>> new_parts = GameState::get_instance()->get_partition_and_nearby(this);
  std::set<std::shared_ptr<Partition>> tmp_parts;

  while (!new_parts.empty() && !parts.empty())
  {
    // We have an:
    // - Iterator
    //    - shared_ptr
    //      - Partition
    if ((*new_parts.begin()).get() == (*parts.begin()).get()) // overlap
    {
      tmp_parts.insert(*new_parts.begin());
      new_parts.erase(new_parts.begin());
      parts.erase(    parts.begin());
    }
    else if ((*new_parts.begin()).get() < (*parts.begin()).get()) // only in new, add self to partition
    {
      (*new_parts.begin())->add_game_piece(this);
      tmp_parts.insert(*new_parts.begin());
      new_parts.erase(new_parts.begin());
    }
    else  // only in old, remove self from partition
    {
      ((*parts.begin()))->remove_game_piece(this); 
      parts.erase(parts.begin());
    }
  }
  parts = tmp_parts;
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

PhyVector GamePiece::phy_vector_to_other_player(const GamePiece* other)
{
  return PhyVector(position.x - other->position.x, position.y - other->position.y);
}



bool GamePiece::detect_player_on_player_collision(const GamePiece* other)
{
  PhyVector collision_vector = phy_vector_to_other_player(other);
  if (collision_vector.get_magnitude() > PLAYER_ON_PLAYER_COLLISION)
  {
    return false;
  }

  float dot_product = velocity.dot_product(collision_vector);
  TRACE << "dot product: " << dot_product;
  return dot_product > 0;
         
}



void GamePiece::handle_player_on_player_collision(const GamePiece* other)
{
  TRACE << "collision between players " << id << " and " << other->id;
  PhyVector collision_vector = phy_vector_to_other_player(other);
  velocity = collision_vector.get_inverse();
}



void GamePiece::handle_possible_collision_with_wall()
{
  if ((position.x <=             PLAYER_ON_WALL_COLLISION && velocity.x < 0.0) || 
      (position.x >= MAP_WIDTH - PLAYER_ON_WALL_COLLISION && velocity.x > 0.0))
  {
    LOG << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    velocity.x = -velocity.x;
  }
  if ((position.y <=              PLAYER_ON_WALL_COLLISION && velocity.y < 0.0) ||
      (position.y >= MAP_HEIGHT - PLAYER_ON_WALL_COLLISION && velocity.y > 0.0))
  {
    LOG << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    velocity.y = -velocity.y;
  }
}



void GamePiece::run_sim()
{
  for (auto part = parts.begin(); part != parts.end(); ++part)
  {
    std::unordered_set<GamePiece*> nearby_pieces = (*part)->get_game_pieces();

    for (auto nearby = nearby_pieces.begin(); nearby != nearby_pieces.end(); ++nearby)
    {
      GamePiece* gp = *nearby;
      if (gp == this)
      {
        continue;
      }
      if (detect_player_on_player_collision(gp))
      {
        LOG << "detected player on player collision";
        handle_player_on_player_collision(gp);
      }
      handle_possible_collision_with_wall();
    }
  }

  position.x += velocity.x;
  position.y += velocity.y;

//  velocity.x += acceleration.x;
//  velocity.y += acceleration.y;
}



