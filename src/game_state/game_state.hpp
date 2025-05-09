#ifndef _GAME_STATE_HPP_
#define _GAME_STATE_HPP_

#include <vector>
#include <string>
#include <memory>
#include <set>

#include "player.hpp"
//#include "map_object.hpp"
#include "game_piece.hpp"
#include "partition.hpp"






class GameState {
public:
  void initialize(std::string testfile);
  GameState();
  ~GameState();

  static GameState* get_instance();

  void operator()();
  void run_sim();
  std::string game_info();

  std::shared_ptr<Partition> get_partition(const GamePiece* gp);
  std::set<std::shared_ptr<Partition>> get_partition_and_nearby(const GamePiece* gp);

private:
  static GameState* p_inst;
  std::vector<GamePiece*> players;
 // std::vector<MapObject*> map_objects;
  float width;
  float height;

  //std::vector<std::vector<Partition*>> spatial_partition;
  std::vector<std::vector<std::shared_ptr<Partition>>> spatial_partition;
};

#endif
