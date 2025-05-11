#ifndef _GAME_PIECE_HPP_
#define _GAME_PIECE_HPP_


#define PARTITION_SIZE 20

#include <string>
#include <sstream>
#include <memory>
#include <set>

#include "boost-json.hpp"

#include "phy_vector.hpp"
#include "partition.hpp"

class Partition;

class GamePiece;

struct GamePiecePtrHash {
  size_t operator()(const GamePiece* gp) const;
};

struct GamePiecePtrEqual {
  bool operator()(const GamePiece* a, const GamePiece* b) const;
};


class GamePiece
{
public:
  int id;
  float mass;
  PhyVector position;
  PhyVector velocity;
  PhyVector next_velocity;
  PhyVector acceleration;

  std::shared_ptr<Partition> current_part;
  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> parts;

  std::unordered_set<GamePiece*, GamePiecePtrHash, GamePiecePtrEqual> already_compared;

  //Shape shape;
  bool fixed;

  
public:
  GamePiece();
  GamePiece(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  GamePiece(std::vector<std::string> row);
  ~GamePiece();

  friend std::ostream& operator<<(std::ostream& os, const GamePiece& gp);
  void print_part_list();

  const bool operator==(const GamePiece& rhs);

  int get_id() const;
  float get_mass() const;
  PhyVector get_position() const;
  PhyVector get_velocity() const;
  PhyVector get_acceleration() const;

  void update_partitions();

  boost::json::object getGamePieceJson();
  PhyVector phy_vector_to_other_player(const GamePiece* other);
  bool detect_player_on_player_collision(const GamePiece* other);
  void handle_player_on_player_collision(const GamePiece* other);
  void handle_possible_collision_with_wall();

  void player_on_player_collision(GamePiece* other);

  void update_velocity();
  void update_position();
  void calculate_next_velocity();

  void add_new_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                             std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);
  void rem_old_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts);
  void add_from_both(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);

};

PhyVector BuildCollisionVector(const PhyVector a, const PhyVector b);
PhyVector BuildAfterCollisionVelocity(GamePiece* a, const GamePiece* b);






#endif
