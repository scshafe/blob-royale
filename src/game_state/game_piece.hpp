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

class GamePiece
{
public:
  int id;
  PhyVector position;
  PhyVector velocity;
  PhyVector acceleration;

  Partition* current_part;
  std::set<Partition*> parts;

  //Shape shape;
  bool fixed;

  
public:
  GamePiece();
  GamePiece(int id, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  GamePiece(std::vector<std::string> row);
  ~GamePiece();

  friend std::ostream& operator<<(std::ostream& os, const GamePiece& gp);

  PhyVector get_position() const;
  PhyVector get_velocity() const;
  PhyVector get_acceleration() const;

  void update_partitions();

  boost::json::object getGamePieceJson();
  PhyVector phy_vector_to_other_player(const GamePiece* other);
  bool detect_player_on_player_collision(const GamePiece* other);
  void handle_player_on_player_collision(const GamePiece* other);
  void handle_possible_collision_with_wall();
  void run_sim();
};

#endif
