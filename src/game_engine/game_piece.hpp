#ifndef _GAME_PIECE_HPP_
#define _GAME_PIECE_HPP_


#define PARTITION_SIZE 20

#include <string>
#include <sstream>
#include <memory>
#include <set>
#include <mutex>
#include <map>

#include "boost-json.hpp"

#include "phy_vector.hpp"
#include "partition.hpp"

class Partition;

class GamePiece;

struct GamePiecePtrHash {
  size_t operator()(const std::shared_ptr<GamePiece> gp) const;
};

struct GamePiecePtrEqual {
  bool operator()(const  std::shared_ptr<GamePiece> a, const  std::shared_ptr<GamePiece> b) const;
};


enum class QueueOperationResults
{
  send_back,
  option1,
  option2
};







class GamePiece : public std::enable_shared_from_this<GamePiece>
{
public:


  int id;
  float mass;
  PhyVector position;
  PhyVector velocity;
  PhyVector next_velocity;
  PhyVector acceleration;

  //Shape shape;
  bool fixed;


  std::mutex m;
  bool acquire_another_gamepiece_lock(std::shared_ptr<GamePiece> gp);
  bool acquire_another_gamepiece_lock_velocity(std::shared_ptr<GamePiece> gp);
  void unlock_another_gamepiece_lock(std::shared_ptr<GamePiece> gp);

  std::shared_ptr<Partition> current_part;
  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> parts;


  // detect_collisions
  QueueOperationResults detect_collisions();
  float detect_player_on_player_collision(const std::shared_ptr<GamePiece> other);
  std::unordered_set<std::shared_ptr<GamePiece>, GamePiecePtrHash, GamePiecePtrEqual> already_compared;
  std::map<float, std::shared_ptr<GamePiece>> nearest_collisions;
  float nearest_collision_distance; // for establishing locking precedence in next stage

  // collision_velcoity
  QueueOperationResults collision_velocity();
  void update_next_velocities(std::shared_ptr<GamePiece> b);
  bool already_calculated = false;

  // simple_velocity
  QueueOperationResults simple_velocity();
  void handle_possible_collision_with_wall();
  

  // uppdate_position
  QueueOperationResults update_position();
  

  // update_partitions
  QueueOperationResults update_partitions();
  void add_new_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                             std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);
  void rem_old_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts);
  void add_from_both(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);


  // handle_finish
  QueueOperationResults handle_finished();
  virtual bool is_stationary() = 0;

public:
  GamePiece();
  GamePiece(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  GamePiece(const int& id_, std::vector<std::string> row);
  ~GamePiece();

  friend std::ostream& operator<<(std::ostream& os, const GamePiece& gp);
  void print_part_list();

  bool operator==(const GamePiece& rhs) const;

  int get_id() const;
  float get_mass() const;
  PhyVector get_position() const;
  PhyVector get_velocity() const;
  PhyVector get_acceleration() const;


  virtual boost::json::object getGamePieceJson();
};

PhyVector BuildCollisionVector(const PhyVector a, const PhyVector b);



#endif
