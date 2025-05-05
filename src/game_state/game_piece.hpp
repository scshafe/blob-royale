#ifndef _GAME_PIECE_HPP_
#define _GAME_PIECE_HPP_


#define PARTITION_SIZE 20

#include <vector>
#include <string>
#include <sstream>
#include "phy_vector.hpp"
#include "partition.hpp"

class Partition;

class GamePiece
{
private:
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
  void add_partition(Partition* partition);
  void remove_partition(Partition* partition);

  std::string jsonify_pos();
};

#endif
