#include <iostream>
#include <sstream>
#include <cassert>
#include <stdexcept>

// open-source-libs
#include "rapidcsv.h"

// boost-headers
#include "boost-log.hpp"
#include "boost-json.hpp"

// game-state
#include "game_engine_parameters.hpp"
#include "game_state.hpp"

GameState* GameState::p_inst = nullptr;

extern src::severity_logger< severity_level > lg;


struct Cell
{
  size_t row;
  size_t col; 
  
  Cell(const GamePiece* gp)
  {
    row = floor(gp->get_position().y / PARTITION_HEIGHT);
    col = floor(gp->get_position().x / PARTITION_WIDTH);
  }

  Cell(const float& x, const float& y)
  {
    row = floor(y / PARTITION_HEIGHT);
    col = floor(x / PARTITION_WIDTH);
  }

  Cell(const GamePiece& gp)
  {
    row = floor(gp.get_position().y / PARTITION_HEIGHT);
    col = floor(gp.get_position().x / PARTITION_WIDTH);
  }
};

std::ostream& operator<<(std::ostream& os, const Cell& c)
{
  return os << "[" << c.row << "," << c.col << "]";
}


void GameState::initialize(std::string testfile)
{
  ENTRANCE << "GameState::initialize()";

  rapidcsv::Document doc(testfile);

  for (size_t i = 0; i < doc.GetRowCount(); i++)
  {
    std::vector<std::string> row = doc.GetRow<std::string>(i);
    if (row[0] == "player")
    {
      players.emplace_back(new Player(row));

    }
    else
    {
      ERROR << "Error: failed to add player";
      exit(1);
    }
  }
}


GameState::GameState() :
  players(),
  width(MAP_WIDTH),
  height(MAP_HEIGHT),
  spatial_partition(std::vector<std::vector<Partition*>>(SPATIAL_PARTITION_ROWS, std::vector<Partition*>(SPATIAL_PARTITION_COLS, new Partition)))
{}

GameState* GameState::get_instance()
{
  if (!p_inst)
  {
    GameState::p_inst = new GameState();
  }
  return p_inst;
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
    LOG << "GameState::run_sim() tick: " << tick_count++ << " with period: " << GAME_TICK_PERIOD_US;
    for (auto p_it = players.begin(); p_it != players.end(); ++p_it)
    {
      (*p_it)->run_sim();
    }
    usleep(GAME_TICK_PERIOD_US);
  }
}

std::string GameState::game_info()
{
  boost::json::array json_players;
  for (auto p_it = players.begin(); p_it != players.end(); ++p_it)
  {
    json_players.push_back((*p_it)->getGamePieceJson());
  }
  return boost::json::serialize(json_players);
}

Partition* GameState::get_partition(const GamePiece* gp)
{
  Cell tmp (gp);
  return spatial_partition[tmp.row][tmp.col];
}

std::set<Partition*> GameState::get_partition_and_nearby(const GamePiece* gp)
{
  std::set<Partition*> tmp_parts;
  Cell tmp (gp); 

  for (int row = tmp.row - 1; row <= tmp.row + 1; row++)
  {
    for (int col = tmp.col - 1; col <= tmp.col + 1; col++)
    {
      try
      {
        tmp_parts.insert(spatial_partition.at(row).at(col));
      }
      catch (const std::out_of_range& oor)
      {
        // Nothing to do here... could add logs
      }
    }
  }
  return tmp_parts;
}





