#include "boost-log.hpp"
#include "boost-json.hpp"
#include "game_engine_parameters.hpp"
#include "game_piece.hpp"
#include "game_engine.hpp"




#define GET_OTHER_GP_LOCK(gp) if (acquire_another_gamepiece_lock(gp)) { m.unlock(); return false; }

//#define UNLOCK_OTHER_GP(gp) { TRACgp->m.unlock();


// only use the above macro to call this
bool GamePiece::acquire_another_gamepiece_lock(std::shared_ptr<GamePiece> gp)
{
  TRACE << id << " attempting to lock " << gp->id;
  if (gp->m.try_lock() == false)
  {
    if (gp->id < id)
    {
      TRACE << id << " failed to unlock lower: " << gp->id;
      return false;
    }
    else
    {
      assert(gp->id != id);
      TRACE << id << " waiting on lock for " << gp->id;
      gp->m.lock();
    }
  }
  TRACE << id << " successfully_lock " << gp->id;
  return true;
}


void GamePiece::unlock_another_gamepiece_lock(std::shared_ptr<GamePiece> gp)
{
  TRACE << id << " unlocking " << gp->id;
  gp->m.unlock();
}


bool GamePiece::acquire_another_gamepiece_lock_velocity(std::shared_ptr<GamePiece> gp)
{
  if (gp->m.try_lock() == false)
  {
    if (gp->nearest_collision_distance < nearest_collision_distance)
    {
      return false;
    }
    else if (gp->nearest_collision_distance == nearest_collision_distance && gp->id < id)
    {
      return false;
    }
    else
    {
      assert(gp->id != id);
      gp->m.lock();
      return true;
    }
  }
  return true;
}




size_t GamePiecePtrHash::operator()(const std::shared_ptr<GamePiece> gp) const {
  return std::hash<int>()(gp->get_id());
}

bool GamePiecePtrEqual::operator()(const std::shared_ptr<GamePiece> a, const std::shared_ptr<GamePiece> b) const {
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

GamePiece::GamePiece(const int& id_, std::vector<std::string> row) :
  current_part(nullptr),
  parts()
{
  ENTRANCE << "GamePiece(row)";
  try
  {
    std::string vals;
    for (auto i : row) vals += i + " ";
    TRACE << "row is: " << vals;

    id = id_;
    mass = 1.0;
    position.x = std::stof(row[1]);
    position.y = std::stof(row[2]);
    velocity.x = std::stof(row[3]);
    next_velocity.x = velocity.x;
    velocity.y = std::stof(row[4]);
    next_velocity.y = velocity.y;
    acceleration.x = std::stof(row[5]);
    acceleration.y = std::stof(row[6]);
  }
  catch (...)
  {
    ERROR << "Fatal: unable to create person from row";
    exit(1);
  }
  
  TRACE << "created player" << *this;
}

GamePiece::~GamePiece()
{
  TRACE << "Deconstructing GamePiece: " << id;
}

std::ostream& operator<<(std::ostream& os, const GamePiece& gp)
{
  os << gp.id;
//  os << "GP(" 
//     << "m: " << gp.mass << " "
//     << "pos:" << gp.position << " "
//     << "vel" << gp.velocity << ")";
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

bool GamePiece::operator==(const GamePiece& rhs) const
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
  //ENTRANCE << "add_new_part()";
  assert(!new_parts.empty());
  
  auto new_it = new_parts.begin();
  (*new_it)->add_game_piece(shared_from_this());
  tmp_parts.insert(std::move(*new_it));
  new_parts.erase(new_it);

}

void GamePiece::rem_old_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts)
{
  //ENTRANCE << "add_old_part()";
  assert(!old_parts.empty());

  auto old_it = old_parts.begin();
  (*old_it)->remove_game_piece(shared_from_this());
  parts.erase(old_it);
}

void GamePiece::add_from_both(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts)
{ 
  auto new_it = new_parts.begin();

  tmp_parts.insert(std::move(*new_it));
  new_parts.erase(new_parts.begin());
  parts.erase(parts.begin());
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

QueueOperationResults GamePiece::update_partitions()
{
  ENTRANCE << *this << " update_partitions()";
  std::shared_ptr<Partition> tmp = GameEngine::get_instance()->get_partition(shared_from_this());
  
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
      return QueueOperationResults::option1;
    }
    else
    {
      current_part = std::move(tmp);
      TRACE << "Changing partition, now in: " << *current_part;
    }
  }

  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> new_parts;
  GameEngine::get_instance()->get_partition_and_nearby(shared_from_this(), new_parts);
  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> tmp_parts;

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
  for (auto it = tmp_parts.begin(); it != tmp_parts.end(); ++it)
  {
    parts.insert(std::move(*it));
  }

  return QueueOperationResults::option1;
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



float GamePiece::detect_player_on_player_collision(const std::shared_ptr<GamePiece> other)
{
  ENTRANCE << *this << " detect_player_on_player_collision()";
  PhyVector collision_vector(position, other->position); 
  if (collision_vector.get_magnitude() > PLAYER_ON_PLAYER_COLLISION)
  {
    WARNING << "no collision - magnitude: " << collision_vector.get_magnitude();
    return 100000.0;
  }
  WARNING << "Players within distance: " << collision_vector.get_magnitude();
  WARNING << *this;
  WARNING << *other;
  float dot_product = velocity.dot(other->velocity);
  TRACE << "velocity: " << velocity;
  TRACE << "dot product: " << dot_product;
  TRACE << "collision_vec: " << collision_vector;

  if (dot_product > 0)
  {
    TRACE << "close encounter between " << *this << " and " << *other << " of " << collision_vector.get_magnitude() << " but dot_product is negative: " << dot_product;
    return 100000.0;
  }
  return collision_vector.get_magnitude();
         
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



QueueOperationResults GamePiece::detect_collisions()
{
  ENTRANCE << *this << " detect_collisions()";
  m.lock();
  
  for (auto part : parts)
  {
    const std::unordered_set<std::shared_ptr<GamePiece>> pieces = part->get_pieces();
    for (auto gp : pieces)
    {
      if (*gp == *this || already_compared.contains(gp))
      {
        continue;
      }

      if (!acquire_another_gamepiece_lock(gp))
      {
        m.unlock();
        return QueueOperationResults::send_back;
      }
      already_compared.insert(gp);
      gp->already_compared.insert(shared_from_this());

      float dist = detect_player_on_player_collision(gp);
      if (dist < PLAYER_ON_PLAYER_COLLISION)
      {
        TRACE << "Collision Detected between " << *this << " and " << *gp << " with a distance of " << dist;
        nearest_collisions[dist] = gp;
        gp->nearest_collisions[dist] = shared_from_this();
        
        nearest_collision_distance = std::min(nearest_collision_distance, dist);
        gp->nearest_collision_distance = std::min(gp->nearest_collision_distance, dist);
        
      }
      unlock_another_gamepiece_lock(gp);
    }
  }
  if (nearest_collisions.empty())
  {
    m.unlock();
    return QueueOperationResults::option1;
  }
  m.unlock();
  return QueueOperationResults::option2;
}


QueueOperationResults GamePiece::simple_velocity()
{
  // add acceleration here
  velocity = velocity;

  
  handle_possible_collision_with_wall();
  //velocity = next_velocity;
  return QueueOperationResults::option1;
}

QueueOperationResults GamePiece::collision_velocity()
{
  m.lock();
  if (already_calculated == true)
  {
    m.unlock();
    return QueueOperationResults::option1;
  }

  while (!nearest_collisions.empty())
  {
    if (!acquire_another_gamepiece_lock_velocity(nearest_collisions.begin()->second))
    {
      m.unlock();
      return QueueOperationResults::send_back;
    }
    float dist = nearest_collisions.begin()->first;
    std::shared_ptr<GamePiece> gp = nearest_collisions.begin()->second;
    if (gp->already_calculated)
    {
      unlock_another_gamepiece_lock(gp);
      nearest_collisions.erase(nearest_collisions.begin());
      continue;
    }
    if (dist == nearest_collision_distance)
    {
      update_next_velocities(gp);
      already_calculated = true;
      gp->already_calculated = true;
      unlock_another_gamepiece_lock(gp);
      m.unlock();
      return QueueOperationResults::option1;
    }
  }
  m.unlock();
  return QueueOperationResults::option2;
}


QueueOperationResults GamePiece::update_position()
{
  position += velocity;
  return QueueOperationResults::option1;
}



QueueOperationResults GamePiece::handle_finished()
{
  ENTRANCE << *this << " handle_finished()";
  // pull new acceleration from cache here

  already_compared.clear();
  nearest_collisions.clear();
  nearest_collision_distance = 100000.0;

  already_calculated = false;

  if (is_stationary())
  {
    assert(false && "separate route for stationary objects to avoid extra computation not yet implemented");
    return QueueOperationResults::option1;
  }
  return QueueOperationResults::option1;
}


PhyVector BuildCollisionVector(const PhyVector a, const PhyVector b)
{
  return PhyVector(a.x - b.x,
                   a.y - b.y);
}

void GamePiece::update_next_velocities(std::shared_ptr<GamePiece> b)
{
  float m1 = this->get_mass();
  float m2 = b->get_mass();

  PhyVector p1(this->get_position());
  PhyVector p2(b->get_position());

  PhyVector v1(this->get_velocity());
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

  float v2n_after (((v2n * (m2 - m1)) + (v1n * (2 * m1))) / (m2 + m1));
  float v2t_after (v2t);

  PhyVector v1n_vec(un * v1n_after);
  PhyVector v1t_vec(ut * v1t_after);

  PhyVector v2n_vec(un * v2n_after);
  PhyVector v2t_vec(ut * v1t_after);

  PhyVector res1(v1n_vec + v1t_vec);
  PhyVector res2(v2n_vec + v2t_vec);
  
  b->velocity = res2;
  velocity = res1;

}


