#include <iostream>
#include <sstream>
#include <cassert>
#include <stdexcept>
#include <algorithm>

// open-source-libs
#include "rapidcsv.h"

// boost-headers
#include "boost-log.hpp"
#include "boost-json.hpp"

// game-state
#include "game_engine_parameters.hpp"
#include "game_state.hpp"
#include "partition.hpp"

GameState* GameState::p_inst = nullptr;

extern src::severity_logger< severity_level > lg;





void GameState::initialize(std::string testfile)
{
  ENTRANCE << "GameState::initialize()";

  for (size_t row = 0; row < SPATIAL_PARTITION_ROWS; row++)
  {
    spatial_partition.emplace_back();
    for (size_t col = 0; col < SPATIAL_PARTITION_COLS; col++)
    {
      std::shared_ptr<Partition> tmp = Partition::buildPartition(row, col);
      spatial_partition[row].push_back(std::move(tmp));
    }
  }

  for (size_t row = 0; row < SPATIAL_PARTITION_ROWS; row++)
  {
    for (size_t col = 0; col < SPATIAL_PARTITION_COLS; col++)
    {
      TRACE << *spatial_partition[row][col];
    }
  }


  rapidcsv::Document doc(testfile);

  for (size_t i = 0; i < doc.GetRowCount(); i++)
  {
    std::vector<std::string> row = doc.GetRow<std::string>(i);
    if (row[0] == "player")
    {
      std::shared_ptr<Player> tmp = std::make_shared<Player>(row);
      std::shared_ptr<GamePiece> gp_tmp = std::dynamic_pointer_cast<GamePiece> (tmp);
      players.push_back(std::move(gp_tmp));

    }
    else
    {
      ERROR << "Error: failed to add player";
      exit(1);
    }
  }
  for (auto p : players)
  {
    p->update_partitions();
  }
}

GameState::GameState() :
  players(),
  width(MAP_WIDTH),
  height(MAP_HEIGHT)
//  spatial_partition(std::vector<std::vector<std::shared_ptr<Partition>>>(SPATIAL_PARTITION_ROWS, std::vector<std::shared_ptr<Partition>>(SPATIAL_PARTITION_COLS)))
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
  sim_loop();
}


void GameState::update_positions()
{
  ENTRANCE << "GameState::update_positions()";
  for (auto p : players)
  {
    p->update_position();
  }
}


// aka: check for collisions
void GameState::update_velocities()
{ 
  ENTRANCE << "GameState::update_velocities()";
  for (auto p : players)
  {
    p->update_velocity();
  }
}

void GameState::calculate_next_velocities()
{
  ENTRANCE << "GameState::calculate_next_velocities()";
  for (auto p : players)
  {
    p->calculate_next_velocity();
  }
}

void GameState::update_partitions()
{
  ENTRANCE << "GameState::update_parititons()";
  for (auto p : players)
  {
    p->update_partitions();
  }
}

void GameState::start_sim()
{
  running = true;
  std::thread t(&GameState::sim_loop, this);
  t.detach();
}


void GameState::sim_loop()
{
  unsigned int tick_count = 0;
  while (running)
  {
    LOG << "GameState::sim_loop() tick: " << tick_count++ << " with period: " << GAME_TICK_PERIOD_US;

    calculate_next_velocities();

    update_velocities();

    update_positions();

    update_partitions();
    // should be able to turn this off in live (after locking)
    usleep(GAME_TICK_PERIOD_US);
  }
}

void GameState::pause_sim()
{
  running = false;
}

boost::json::array GameState::game_info()
{
  boost::json::array json_players;
  for (auto p : players)
  {
    json_players.push_back(dynamic_cast<Player&>(*p).getGamePieceJson());
  }
  return json_players;
}


boost::json::object GameState::game_config()
{
  boost::json::object json_config;
  json_config["width"] = MAP_WIDTH;
  json_config["height"] = MAP_HEIGHT;
  json_config["radius"] = PLAYER_RADIUS;
  json_config["interval"] = GAME_TICK_PERIOD_MS;
  json_config["part_rows"] = SPATIAL_PARTITION_ROWS;
  json_config["part_cols"] = SPATIAL_PARTITION_COLS;
  json_config["player_collision"] = PLAYER_ON_PLAYER_COLLISION;

  return json_config;
}  
  
std::shared_ptr<Partition> GameState::get_partition(std::shared_ptr<GamePiece> gp)
{
  //std::shared_ptr<GamePiece> tmp_gp = gp;
  //std::shared_ptr<GamePiece> tmp_gp = std::make_shared<GamePiece>(gp);
  Cell tmp (gp);
  TRACE << "Cell for get_partition() : " << tmp;
  return spatial_partition[tmp.row()][tmp.col()];
  //return std::make_shared<Partition>(*spatial_partition[tmp.row()][tmp.col()]);
}

void GameState::get_partition_and_nearby(std::shared_ptr<GamePiece> gp, std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts)
{
  Cell tmp (gp); 

  TRACE << "finding partitions around Cell: " << tmp;

  for (size_t row = (tmp.row() == 0 ? 0 : tmp.row() - 1); row <= tmp.row() + 1; row++)
  {
    TRACE << "row: " << row;
    for (size_t col = (tmp.col() == 0 ? 0 : tmp.col() - 1); col <= tmp.col() + 1; col++)
    {
      TRACE << "col: " << col;
      try
      {
        TRACE << "attempting: " << row << "," << col;
        TRACE << *spatial_partition.at(row).at(col);
        //std::shared_ptr<Partition> tmp_ptr = spatial_partition.at(row).at(col);
          //std::make_shared<Partition>(*spatial_partition.at(row).at(col));
        //tmp_parts.insert(std::move(tmp_ptr));
        tmp_parts.emplace(spatial_partition.at(row).at(col));
      }
      catch (const std::out_of_range& oor)
      {
        Cell tmp_2 (row, col);
        TRACE << tmp_2 << " is invalid";
        // Nothing to do here... could add logs
      }
    }
  }
}





