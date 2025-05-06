#ifndef _GAME_PIECE_HPP_
#define _GAME_PIECE_HPP_


#define PARTITION_SIZE 20

#include <vector>
#include <string>
#include <sstream>

#include <boost/json.hpp>

#include "phy_vector.hpp"
#include "partition.hpp"

class Partition;

class GamePiece
{
protected:
  int id;
  PhyVector position;
  PhyVector velocity;
  PhyVector acceleration;

  std::vector<Partition*> piece_partitions;
  //Shape shape;
  bool fixed;

public:

  GamePiece(int id_, float x, float y, float vel_x, float vel_y, float accel_x, float accel_y);
  GamePiece();
  friend std::ostream& operator<<(std::ostream& os, const GamePiece& gp);

  PhyVector get_position();
  PhyVector get_velocity();
  PhyVector get_acceleration();

  void add_partition(Partition* partition);
  void remove_partition(Partition* partition);

  boost::json::object getGamePieceJson();
  PhyVector phy_vector_to_other_player(const GamePiece& other);
  bool detect_player_on_player_collision(const GamePiece& other);
  void handle_player_on_player_collision(const GamePiece& other);
  void handle_possible_collision_with_wall();
  void run_sim();
};

#endif
