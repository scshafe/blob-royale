#include "boost-log.hpp"
#include "boost-json.hpp"
#include "game_engine_parameters.hpp"
#include "game_piece.hpp"
#include "game_state.hpp"



size_t GamePiecePtrHash::operator()(const GamePiece* gp) const {
  return std::hash<int>()(gp->get_id());
}

bool GamePiecePtrEqual::operator()(const GamePiece* a, const GamePiece* b) const {
  return a->get_id() == b->get_id();
}


GamePiece::GamePiece() :
  id(0),
  position(0.0, 0.0),
  velocity(0.0, 0.0),
  acceleration(0.0, 0.0),
  current_part(nullptr),
  parts(),
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
    mass = 1.0;
    position.x = std::stof(row[2]);
    position.y = std::stof(row[3]);
    velocity.x = std::stof(row[4]);
    next_velocity.x = velocity.x;
    velocity.y = std::stof(row[5]);
    next_velocity.y = velocity.y;
    acceleration.x = std::stof(row[6]);
    acceleration.y = std::stof(row[7]);
  }
  catch (...)
  {
    ERROR << "Fatal: unable to create person from row";
    exit(1);
  }
  
  TRACE << "created player" << *this;

  update_partitions();
}

GamePiece::~GamePiece()
{
  TRACE << "Deconstructing GamePiece: " << id;
}

std::ostream& operator<<(std::ostream& os, const GamePiece& gp)
{
  os << "GP(" 
     << "m: " << gp.mass << " "
     << "pos:" << gp.position << " "
     << "vel" << gp.velocity << ")";
  return os;
}

void GamePiece::print_part_list()
{
  WARNING << "GamePiece " << *this << " partitions:";
  for (auto it = parts.begin(); it != parts.end(); ++it)
  {
    if (*it == nullptr)
    {
      ERROR << "INVALID PARTITION";
      exit(1);
    }
    TRACE << **it;
  }
}

const bool GamePiece::operator==(const GamePiece& rhs)
{
  return id == rhs.id;
}

int GamePiece::get_id() const
{
  return id;
}

float GamePiece::get_mass() const
{
  return mass;
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

void GamePiece::add_new_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                             std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts)
{
  ENTRANCE << "add_new_part()";
  assert(!new_parts.empty());
  
  auto new_it = new_parts.begin();

  (*new_it)->add_game_piece(this);

  //std::shared_ptr<Partition> tmp = std::make_shared<Partition>(**new_it);
  std::shared_ptr<Partition> tmp = *new_it;

  tmp_parts.insert(std::move(tmp));
  new_parts.erase(new_it);

  TRACE << "New parts length: " << new_parts.size();
  TRACE << "Tmp parts length: " << tmp_parts.size();
}

void GamePiece::rem_old_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts)
{
  ENTRANCE << "add_old_part()";
  assert(!old_parts.empty());

  auto old_it = old_parts.begin();

  (*old_it)->remove_game_piece(this);
  parts.erase(old_it);

  TRACE << "Old parts length: " << old_parts.size();
}

void GamePiece::add_from_both(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts)
{ 
  ENTRANCE << "add_from_both()";

  auto new_it = new_parts.begin();

  //std::shared_ptr<Partition> tmp = std::make_shared<Partition>(**new_it);
  std::shared_ptr<Partition> tmp = *new_it;

  tmp_parts.insert(std::move(tmp));
      
  new_parts.erase(new_parts.begin());
  parts.erase(parts.begin());

  TRACE << "old: " << old_parts.size() << "  new: " << new_parts.size();
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
  ENTRANCE << "update_partitions()";
  std::shared_ptr<Partition> tmp = GameState::get_instance()->get_partition(this);
  
  if (current_part == nullptr)
  {
    current_part = std::move(tmp);
    TRACE << "setting initial: " << *current_part;
  }
  else
  {
    TRACE << "before: " << *current_part;
  
    if (*current_part == *tmp) // if this doesn't change no need to update the set which is costly
    {
      TRACE << "No partition change. Still in " << (*current_part);
      return;
    }
    else
    {
      current_part = std::move(tmp);
      TRACE << "Changing partition, now in: " << *current_part;
    }
  }

  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> new_parts;
  GameState::get_instance()->get_partition_and_nearby(this, new_parts);
  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> tmp_parts;

  TRACE << "New parts length: " << new_parts.size();
  TRACE << "Old parts length: " << parts.size();

  while (!new_parts.empty() && !parts.empty())
  {
    auto new_it = new_parts.begin();
    auto old_it = parts.begin();

    if (**new_it == **old_it) // overlap
    {
      add_from_both(parts, new_parts, tmp_parts);
    }
    else if (**new_it < **old_it) // only in new, add self to partition
    {
      add_new_part(new_parts, tmp_parts);
    }
    else  // only in old, remove self from partition
    {
      rem_old_part(parts);
    }
  }

  // ----- Be Careful about these while loops
  while(!new_parts.empty())  add_new_part(new_parts, tmp_parts);
  while(!parts.empty())      rem_old_part(parts);
  


  // --- Now put tmps back into parts
  //parts = tmp_parts;
  for (auto it = tmp_parts.begin(); it != tmp_parts.end(); ++it)
  {
    parts.insert(std::move(*it));
  }
  TRACE << "parts size: " << parts.size();
  print_part_list();
}


boost::json::object GamePiece::getGamePieceJson()
{
  boost::json::object root;

  root["id"] = id;
  root["pos"] = position.getPhyVectorJson();
  root["vel"] = velocity.getPhyVectorJson();
  root["acc"] = acceleration.getPhyVectorJson();
  
  root["main_part"] = current_part->getPartJson();
  boost::json::array arr;
  for (auto it = parts.begin(); it != parts.end(); ++it)
  {
    arr.push_back((*it)->getPartJson());
  }
  root["parts"] = arr;

  return root;
}

PhyVector GamePiece::phy_vector_to_other_player(const GamePiece* other)
{
  PhyVector tmp(position.x - other->position.x, position.y - other->position.y);
  TRACE << tmp << " has magnitude " << tmp.get_magnitude();
  return tmp;
}



bool GamePiece::detect_player_on_player_collision(const GamePiece* other)
{
  ENTRANCE << "detect_player_on_player_collision()";
  PhyVector collision_vector = phy_vector_to_other_player(other);
  if (collision_vector.get_magnitude() > PLAYER_ON_PLAYER_COLLISION)
  {
    WARNING << "no collision - magnitude: " << collision_vector.get_magnitude();
    return false;
  }
  WARNING << "Players within distance: " << collision_vector.get_magnitude();
  WARNING << *this;
  WARNING << *other;
  float dot_product = velocity.dot(other->velocity);
  TRACE << "velocity: " << velocity;
  TRACE << "dot product: " << dot_product;
  TRACE << "collision_vec: " << collision_vector;
  return dot_product < 0;
         
}



void GamePiece::handle_player_on_player_collision(const GamePiece* other)
{
  ENTRANCE << "handle_player_on_player_collision()";
  TRACE << "collision between players " << id << " and " << other->id;
  next_velocity = BuildAfterCollisionVelocity(this, other);
  TRACE << "velocity will be change: " << velocity << " --> " << next_velocity;
}



void GamePiece::handle_possible_collision_with_wall()
{
  if ((position.x <=             PLAYER_ON_WALL_COLLISION && velocity.x < 0.0) || 
      (position.x >= MAP_WIDTH - PLAYER_ON_WALL_COLLISION && velocity.x > 0.0))
  {
    LOG << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    next_velocity.x = -velocity.x;
  }
  if ((position.y <=              PLAYER_ON_WALL_COLLISION && velocity.y < 0.0) ||
      (position.y >= MAP_HEIGHT - PLAYER_ON_WALL_COLLISION && velocity.y > 0.0))
  {
    LOG << "collision with wall. Velocity x: " << velocity.x << " --> " << -velocity.x;
    next_velocity.y = -velocity.y;
  }
}


void GamePiece::update_position()
{
  ENTRANCE << "update_position()" << *this;
  position.x += velocity.x;
  position.y += velocity.y;

  already_compared.clear();
  update_partitions();
}

void GamePiece::calculate_next_velocity()
{
  ENTRANCE << "update_velocity()" << *this;
  print_part_list();
  TRACE << *this << parts.size();
  for (auto part = parts.begin(); part != parts.end(); ++part)
  {
    (*part)->check_for_collisions(this);
    
    handle_possible_collision_with_wall();
  }
}

void GamePiece::update_velocity()
{
  velocity = next_velocity;
}

void GamePiece::player_on_player_collision(GamePiece* other)
{
  ENTRANCE << "player_on_player_collision()";
  if (already_compared.contains(other)) return;
  already_compared.insert(other);
  if (detect_player_on_player_collision(other))
  {
    handle_player_on_player_collision(other);
  }
}

PhyVector BuildCollisionVector(const PhyVector a, const PhyVector b)
{
  return PhyVector(a.x - b.x,
                   a.y - b.y);
}

PhyVector BuildAfterCollisionVelocity(GamePiece* a, const GamePiece* b)
{
  float m1 = a->get_mass();
  float m2 = b->get_mass();

  PhyVector p1(a->get_position());
  PhyVector p2(b->get_position());

  PhyVector v1(a->get_velocity());
  PhyVector v2(b->get_velocity());

  PhyVector n(p2 - p1);

  PhyVector un (n.normalize());
  PhyVector ut (-un.y, un.x);

  float v1n = un.dot(v1);
  float v1t = ut.dot(v1);

  float v2n = un.dot(v2);
  float v2t = ut.dot(v2);

  float v1n_after (((v1n * (m1 - m2)) + (v2n * (2 * m2))) / (m1 + m2));
  float v1t_after (v1t);

  PhyVector v1n_vec(un * v1n_after);
  PhyVector v1t_vec(ut * v1t_after);

  PhyVector res(v1n_vec + v1t_vec);
  return res;

}
//PhyVector BuildAfterCollisionVelocity(GamePiece* a, const GamePiece* b)
//{
//  WARNING << std::endl << "~~~ Player on Player collision ~~~" << std::endl;
//  PhyVector ax(a->get_position());
//  PhyVector bx(b->get_position());
//
//  PhyVector av(a->get_velocity());
//  PhyVector bv(b->get_velocity());
//
//  float am = a->get_mass();
//  float bm = b->get_mass();
//
//  PhyVector v1_v2 = av - bv;
//  PhyVector v2_v1 = bv - av;
//
//
//  float x_zmf = ( (a->get_mass() * a->get_velocity().x) + 
//                  (b->get_mass() * b->get_velocity().x) )  / 
//                (a->get_mass() + b->get_mass());
//
//  float y_zmf = ( (a->get_mass() * a->get_velocity().y) + 
//                  (b->get_mass() * b->get_velocity().y) )  / 
//                (a->get_mass() + b->get_mass());
//
//  PhyVector x1_x2 = ax - bx;
//
//  PhyVector res = av -  (x1_x2  *  ((2 * bm) / (am + bm))  * v1_v2.dot(x1_x2) / x1_x2.get_magnitude() );
//  
//  TRACE << "v1_v2: " << v1_v2                  << std::endl
//        << "x1_x2: " << x1_x2                  << std::endl
//        << "mag:   " << x1_x2.get_magnitude()  << std::endl
//        << "v1:    " << av                     << std::endl
//        << "v2:    " << bv;
//
//  
//  return res;
//
//}

//PhyVector BuildAfterCollisionVelocity(GamePiece* a, const GamePiece* b)
//{
//  WARNING << std::endl << "~~~ Player on Player collision ~~~" << std::endl;
//  TRACE << "a: " << *a;
//  TRACE << "b: " << *b;
//  PhyVector collision = BuildCollisionVector(a->get_position(), b->get_position());
//
//  TRACE << collision;
//  
//  float x_zmf = ( (a->get_mass() * a->get_velocity().x) + 
//                  (b->get_mass() * b->get_velocity().x) )  / 
//                (a->get_mass() + b->get_mass());
//
//  float y_zmf = ( (a->get_mass() * a->get_velocity().y) + 
//                  (b->get_mass() * b->get_velocity().y) )  / 
//                (a->get_mass() + b->get_mass());
//
//  PhyVector zmf(x_zmf, y_zmf);
//  
//  TRACE << "\nzmf: " zmf;
//
//  float uax_zmf = a->get_velocity().x - x_zmf;
//  float uay_zmf = a->get_velocity().y - y_zmf;
//
//  PhyVector ua_zmf(uax_zmf, uay_zmf);
//
//  TRACE << "ua_zmf " << ua_zmf;
//
//  float ubx_zmf = b->get_velocity().x - x_zmf;
//  float uby_zmf = b->get_velocity().y - y_zmf;
//
//  PhyVector ub_zmf(ubx_zmf, uby_zmf);
//
//  TRACE << "ub_zmf " <<  ub_zmf;
//
//  float am = a->get_mass() / (a->get_mass() + b->get_mass());
//  float bm = b->get_mass() / (a->get_mass() + b->get_mass());
//
//  PhyVector a_norm = collision.normalize();
//  PhyVector b_norm = collision.normalize();
//  
//  
//
//  float ax_final = (
//
//
//
//
//
//  TRACE << "post";
//  TRACE << ax_post << "  " << ay_post;
//  TRACE << bx_post << "  " << by_post;
//
//  float ax_final = ax_post + x_zmf;
//  float ay_final = ay_post + y_zmf;
//
//  float bx_final = bx_post + x_zmf;
//  float by_final = by_post + y_zmf;
//
//  TRACE << "final";
//  TRACE << ax_final << "  " << ay_final;
//  TRACE << bx_final << "  " << by_final;
//
////  std::string tmp;
////  std::cin >> tmp;
//
//  return PhyVector(ax_final, ay_final);
//}



