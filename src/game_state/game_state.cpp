#include <iostream>
#include <sstream>

#include "game_state.hpp"
#include "boost_config.hpp"

GameState::GameState()
{
  BOOST_LOG_TRIVIAL(info) << "Initializing GameState";
  height = float(PARTITION_SIZE * 18);
  width = float(PARTITION_SIZE * 24);


  Player* p1 = new Player(1, 50.0, 70.0, 1.0, 0.2, 0.3, 0.0);
  players.push_back(p1);

  Player* p2 = new Player(2, 600.0, 300.0, 0.5, 0.8, 0.1, 0.1);
  players.push_back(p2);

  for (int i = 0; i < PARTITION_SIZE; i++)
  {
    Partition* p = new Partition();
    spatial_partition.push_back(p);
  }
  build_partition();

}

GameState::~GameState() {}

// allows the class object to be passed to a new thread
void GameState::operator()()
{
  run_sim();
}


void GameState::run_sim()
{
  unsigned int tick_count = 0;
  while (true)
  {
    //BOOST_LOG_TRIVIAL(info) << "GameState::run_sim()" << tick_count++;
    for (auto p : players)
    {
      p->run_sim();
    }
    usleep(1000000);
  }
}

std::string GameState::game_info()
{
  boost::json::array json_players;

  for (auto p : players)
  {
    json_players.push_back(p->getJson());
  }
  return boost::json::serialize(json_players);
}


void GameState::build_partition()
{
  for (auto player : players)
  {
    int index = Partition::get_partition_index(player);
    spatial_partition[index]->add_game_piece(player);
  }
}
