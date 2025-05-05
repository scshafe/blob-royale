#include <iostream>
#include <sstream>

#include "game_state.hpp"

// -------- Boost Logging ---------

#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;

// -------- Boost Json ---------

#include <boost/json.hpp>


GameState::GameState()
{
  BOOST_LOG_TRIVIAL(info) << "Initializing GameState";
  height = float(PARTITION_SIZE * 18);
  width = float(PARTITION_SIZE * 24);


  Player* p1 = new Player(1, 50.0, 70.0, 1.0, 0.2, 0.3, 0.0);
  players.push_back(p1);

  Player* p2 = new Player(2, 100.0, 200.0, 0.5, 0.8, 0.1, 0.1);
  players.push_back(p2);

  for (int i = 0; i < PARTITION_SIZE; i++)
  {
    Partition* p = new Partition();
    spatial_partition.push_back(p);
  }
  build_partition();
}

GameState::~GameState() {}




void GameState::run_sim()
{
  BOOST_LOG_TRIVIAL(info) << "GameState::run_sim()";
  for (auto p : players)
  {
    p->run_sim();
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
