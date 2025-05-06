#ifndef _GAME_STATE_HPP_
#define _GAME_STATE_HPP_

#include <vector>
#include <string>

#include "player.hpp"
//#include "map_object.hpp"
#include "game_piece.hpp"
#include "partition.hpp"



class GameState {
public:
  void initialize(std::string testfile);
  GameState();
  ~GameState();

  void operator()();
  void run_sim();
  std::string game_info();

private:
  std::vector<Player*> players;
 // std::vector<MapObject*> map_objects;
  float width;
  float height;

  std::vector<Partition*> spatial_partition;

  void build_partition();
  
};

#endif
