#include <iostream>
#include <sstream>
#include <cassert>
#include <stdexcept>

#include "game_state.hpp"
#include "boost_config.hpp"

#include "rapidcsv.h"

Player* build_player(std::vector<std::string> row, size_t row_num)
{
  int id;
  float px, py, vx, vy, ax, ay;
  Player* p;

  BOOST_LOG_TRIVIAL(info) << "Adding Player: (" 
                          << row[0] << ", " 
                          << row[1] << ", " 
                          << row[2] << ", " 
                          << row[3] << ", " 
                          << row[4] << ", " 
                          << row[5] << ", " 
                          << row[6] << ", " 
                          << row[7] << ")"; 

  try
  {
    id = std::stoi(row[1]);
    px = std::stof(row[2]);
    py = std::stof(row[3]);
    vx = std::stof(row[4]);
    vy = std::stof(row[5]);
    ax = std::stof(row[6]);
    ay = std::stof(row[7]);
    p = new Player(id, px, py, vx, vy, ax, ay);
  }
  catch (...)
  {
    BOOST_LOG_TRIVIAL(fatal) << "Fatal: unable to create person from row [" << row_num << "]";
    exit(1);
  }
  return p;
}


void GameState::initialize(std::string testfile)
{
  BOOST_LOG_TRIVIAL(info) << "Initializing GameState";
  rapidcsv::Document doc(testfile);

  BOOST_LOG_TRIVIAL(info) << "Reading in GamePieces";
  for (size_t i = 0; i < doc.GetRowCount(); i++)
  {
    std::vector<std::string> row = doc.GetRow<std::string>(i);
    if (row[0] == "player")
    {
      players.push_back(build_player(row, i));
    }
    else
    {
      BOOST_LOG_TRIVIAL(info) << "Error: unable to read testfile row [" << i << "]";
      exit(1);
    }

  }
  build_partition();
}


GameState::GameState()
{

  BOOST_LOG_TRIVIAL(info) << "Constructing GameState";
  height = float(MAP_HEIGHT);
  width = float(MAP_WIDTH);
}

//  Player* p1 = new Player(1, 15.0, 70.0, -1.0, 0.2, 0.0, 0.0);
//  players.push_back(p1);
//
//  Player* p2 = new Player(2, 500.0, 780.0, 0.5, 0.8, 0.0, 0.0);
//  players.push_back(p2);





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
    usleep(10000);
  }
}

std::string GameState::game_info()
{
  boost::json::array json_players;

  for (auto p : players)
  {
    json_players.push_back(p->getPlayerJson());
  }
  return boost::json::serialize(json_players);
}


size_t get_partition_index(GamePiece* game_piece)
{ 
  PhyVector gp_pos = game_piece->get_position();
  size_t index = GET_PARTITION_INDEX_FROM_COORDINATES(gp_pos.x, gp_pos.y);
  size_t index_test = 0;
  size_t col = floor(gp_pos.x / PARTITION_WIDTH);
  size_t row = SPATIAL_PARTITION_COL_COUNT * floor(gp_pos.y / PARTITION_HEIGHT);
  BOOST_LOG_TRIVIAL(info) << "partition selected. row: " << row << ", col: " << col;
  index_test = col + row;
  assert(index_test == index);
  return index;
}

void GameState::build_partition()
{ 
  for (int i = 0; i < SPATIAL_PARTITION_COUNT; i++)
  {
    Partition* p = new Partition();
    spatial_partition.push_back(p);
  }

  BOOST_LOG_TRIVIAL(info) << "Populating Spatial Partition";
  for (auto player : players)
  {
    size_t index = get_partition_index(player);
    BOOST_LOG_TRIVIAL(info) << "Adding " << *player << " to partition " << index;
    spatial_partition[index]->add_game_piece(player);
  }
}



