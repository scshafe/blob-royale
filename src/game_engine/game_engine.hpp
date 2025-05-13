#ifndef _GAME_STATE_HPP_
#define _GAME_STATE_HPP_

#include <thread>
#include <vector>
#include <string>
#include <memory>
#include <set>

#include "player.hpp"
//#include "map_object.hpp"
#include "game_piece.hpp"
#include "partition.hpp"






class GameEngine {
public:
  void initialize(std::string testfile);
  GameEngine();
  ~GameEngine();

  static GameEngine* get_instance();

  void operator()();
  void sim_loop();
  void start_sim();
  void pause_sim();
  boost::json::array game_info();
  boost::json::object game_config();

  std::shared_ptr<Partition> get_partition(std::shared_ptr<GamePiece> gp);
  void get_partition_and_nearby(std::shared_ptr<GamePiece> gp, std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);

private:
  static GameEngine* p_inst;
  std::vector<std::shared_ptr<GamePiece>> players;
 // std::vector<MapObject*> map_objects;
  float width;
  float height;
  bool running;

  //std::vector<std::vector<Partition*>> spatial_partition;
  std::vector<std::vector<std::shared_ptr<Partition>>> spatial_partition;

  void update_partitions();
  void update_positions();
  void update_velocities();
  void calculate_next_velocities();
};

#endif
