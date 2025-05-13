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
  size_t operator()(const std::shared_ptr<GamePiece> gp) const;
};

struct GamePiecePtrEqual {
  bool operator()(const  std::shared_ptr<GamePiece> a, const  std::shared_ptr<GamePiece> b) const;
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

  std::shared_ptr<Partition> current_part;
  std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>> parts;

  std::unordered_set<std::shared_ptr<GamePiece>, GamePiecePtrHash, GamePiecePtrEqual> already_compared;

  //Shape shape;
  bool fixed;

  
public:
  GamePiece();
  GamePiece(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  GamePiece(std::vector<std::string> row);
  ~GamePiece();

  friend std::ostream& operator<<(std::ostream& os, const GamePiece& gp);
  void print_part_list();

  bool operator==(const GamePiece& rhs) const;

  int get_id() const;
  float get_mass() const;
  PhyVector get_position() const;
  PhyVector get_velocity() const;
  PhyVector get_acceleration() const;

  void update_partitions();

  virtual boost::json::object getGamePieceJson();
//  PhyVector phy_vector_to_other_player(const GamePiece* other);
  bool detect_player_on_player_collision(const std::shared_ptr<GamePiece> other);
  void handle_player_on_player_collision(std::shared_ptr<GamePiece> other);
  void handle_possible_collision_with_wall();

  void player_on_player_collision(std::shared_ptr<GamePiece> other);

  void update_velocity();
  void update_position();
  void calculate_next_velocity();
  void update_next_velocities(std::shared_ptr<GamePiece> b);

  void add_new_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                             std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);
  void rem_old_part(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts);
  void add_from_both(std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& old_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& new_parts,
                              std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);

};

PhyVector BuildCollisionVector(const PhyVector a, const PhyVector b);






#endif
