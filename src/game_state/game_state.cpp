#include <iostream>
#include <sstream>

#include "game_state.hpp"





GameState::GameState()
{
  height = float(PARTITION_SIZE * 18);
  width = float(PARTITION_SIZE * 24);

  Player* p1 = new Player(5.0, 7.0, 1.0, 0.2, 0.3, 0.0);
  players.push_back(p1);

  Player* p2 = new Player(10.0, 20.0, 0.5, 0.8, 0.1, 0.1);
  players.push_back(p2);

  build_partition();
}

GameState::~GameState() {}




void GameState::run_sim()
{
  std::cout << "Running Sim" << std::endl;
}

std::string GameState::game_info()
{
  std::stringstream ss;
  for (auto player : players)
  {
    ss << player->jsonify_pos();
  }
  return ss.str();
}

void GameState::build_partition()
{
  for (auto player : players)
  {
    int index = Partition::get_partition_index(player);
    spatial_partition[index]->add_game_piece(player);
  }
}
