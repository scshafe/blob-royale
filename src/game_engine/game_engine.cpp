#include <iostream>
#include <sstream>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <functional>
#include <chrono>
#include <future>

// open-source-libs
#include "rapidcsv.h"

// boost-headers
#include "boost-log.hpp"
#include "boost-json.hpp"

// game-state
#include "game_engine_parameters.hpp"
#include "game_engine.hpp"
#include "partition.hpp"
#include "game_piece.hpp"

GameEngine* GameEngine::p_inst = nullptr;

extern src::severity_logger< severity_level > lg;





bool NearestCollisionComparator::operator()( const std::shared_ptr<GamePiece> a, const std::shared_ptr<GamePiece> b) const
{
  if (a->nearest_collision_distance < b->nearest_collision_distance)
  {
    return true;
  }
  if (a->nearest_collision_distance > b->nearest_collision_distance)
  {
    return false;
  }
  return a->id < b->id;
}


static QueueOperationResults detect_collisions(std::shared_ptr<GamePiece> gp)
{
   return gp->detect_collisions(); 
}

static QueueOperationResults collision_velocity(std::shared_ptr<GamePiece> gp)
{
  return gp->collision_velocity();
}

static QueueOperationResults simple_velocity(std::shared_ptr<GamePiece> gp)
{
  return gp->simple_velocity();
}

static QueueOperationResults update_position(std::shared_ptr<GamePiece> gp)
{
  return gp->update_position();
}

static QueueOperationResults update_partitions(std::shared_ptr<GamePiece> gp)
{
  return gp->update_partitions();
}

static QueueOperationResults handle_finished(std::shared_ptr<GamePiece> gp)
{
  return gp->handle_finished();
}





void GameEngine::initialize(std::string testfile)
{
  ENTRANCE << "GameEngine::initialize()";

  for (size_t row = 0; row < SPATIAL_PARTITION_ROWS; row++)
  {
    spatial_partition.emplace_back();
    for (size_t col = 0; col < SPATIAL_PARTITION_COLS; col++)
    {
      std::shared_ptr<Partition> tmp = Partition::buildPartition(row, col);
      spatial_partition[row].push_back(std::move(tmp));
    }
  }

  rapidcsv::Document doc(testfile);

  for (size_t i = 0; i < doc.GetRowCount(); i++)
  {
    std::vector<std::string> row = doc.GetRow<std::string>(i);
    if (row[0] == "player")
    {
      std::shared_ptr<Player> tmp = std::make_shared<Player>(id_counter++, row);
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

//  for (size_t i = 0; i < 5; ++i)
//  {
//    std::shared_ptr<MapObject> obj ();
//    players.push_back(std::move(std::dynamic_pointer_cast<GamePiece> (obj)));
//  }
//  for (auto mo : stationary_map_objects)
//  {
//    mo->find_starting_partitions();
//  }


// ADD DEPENDENCIES

  //simple_velocity_queue.add_finish_dependencies({&detect_collision_queue, &collision_velocity_queue});

  collision_velocity_queue.add_start_dependencies({&detect_collision_queue});
  collision_velocity_queue.add_finish_dependencies({&detect_collision_queue});

  simple_velocity_queue.add_start_dependencies({&finished_queue});
  simple_velocity_queue.add_finish_dependencies({&detect_collision_queue, &collision_velocity_queue});

  position_queue.add_start_dependencies({&detect_collision_queue});
  //position_queue.add_finish_dependencies({&collision_velocity_queue});
  position_queue.add_finish_dependencies({&simple_velocity_queue});

  partition_queue.add_start_dependencies({&detect_collision_queue});
  partition_queue.add_finish_dependencies({&position_queue});

  finished_queue.add_start_dependencies({&partition_queue});
  finished_queue.add_finish_dependencies({&partition_queue});


  // can probably relax to:                   partition_queue
  detect_collision_queue.add_start_dependencies({&finished_queue});
  // can probably relax to:                    partition_queue
  detect_collision_queue.add_finish_dependencies({&finished_queue});

  external_queue_notification_id = detect_collision_queue.register_external_start_dependency();
  
  // ADD SEND MAPS

//typedef std::function<void(std::shared_ptr<GamePiece>)> AddQueueFunc;
  AddQueueFunc dc_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &detect_collision_queue,    std::placeholders::_1);
  AddQueueFunc sv_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &simple_velocity_queue,     std::placeholders::_1);
  AddQueueFunc cv_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &collision_velocity_queue,  std::placeholders::_1);
  AddQueueFunc pos_ptr =  std::bind(&GamePieceQueue::receive_game_piece, &position_queue,     std::placeholders::_1);
  AddQueueFunc part_ptr = std::bind(&GamePieceQueue::receive_game_piece, &partition_queue,    std::placeholders::_1);
  AddQueueFunc uf_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &finished_queue,     std::placeholders::_1);

  detect_collision_queue.add_send_to_option(QueueOperationResults::send_back, &detect_collision_queue);
  detect_collision_queue.add_send_to_option(QueueOperationResults::option1,   &simple_velocity_queue);
  detect_collision_queue.add_send_to_option(QueueOperationResults::option2,   &collision_velocity_queue);


  collision_velocity_queue.add_send_to_option(QueueOperationResults::send_back, &collision_velocity_queue);
  collision_velocity_queue.add_send_to_option(QueueOperationResults::option1, &position_queue);
  collision_velocity_queue.add_send_to_option(QueueOperationResults::option2, &simple_velocity_queue);


  //simple_velocity_queue.add_send_to_option(QueueOperationResults::send_back, &simple_velocity_queue);
  simple_velocity_queue.add_send_to_option(QueueOperationResults::option1, &position_queue);

  //position_queue.add_send_to_option(QueueOperationResults::send_back, &position_queue);
  position_queue.add_send_to_option(QueueOperationResults::option1, &partition_queue);


  //partition_queue.add_send_to_option(QueueOperationResults::send_back, &partition_queue);
  partition_queue.add_send_to_option(QueueOperationResults::option1, &finished_queue);


  //finished_queue.add_send_to_option(QueueOperationResults::send_back, &finished_queue);
  finished_queue.add_send_to_option(QueueOperationResults::option1, &detect_collision_queue);

  for (auto p : players)
  {
    detect_collision_queue.receive_game_piece(p);
  }

  detect_collision_queue.notify_can_start(dynamic_cast<CycleDependency*>(&finished_queue));
  detect_collision_queue.notify_can_be_finished(dynamic_cast<CycleDependency*>(&finished_queue));
  auto success = std::async(&CycleDependency::external_notify_can_start, dynamic_cast<CycleDependency*>(&detect_collision_queue), external_queue_notification_id);

  simple_velocity_queue.notify_can_start(dynamic_cast<CycleDependency*>(&finished_queue));

  detect_collision_queue.print_dependency_relations();
  collision_velocity_queue.print_dependency_relations();
  simple_velocity_queue.print_dependency_relations();
  position_queue.print_dependency_relations();
  partition_queue.print_dependency_relations();
  finished_queue.print_dependency_relations();

}

GameEngine::GameEngine() :
  players(),
  width(MAP_WIDTH),
  height(MAP_HEIGHT),
  detect_collision_queue(detect_collisions, "detect_collisions",     1),
  simple_velocity_queue(simple_velocity, "simple_velocity",          1),
  collision_velocity_queue(collision_velocity, "collision_velocity", 1),
  position_queue(update_position, "position",                 1),
  partition_queue(update_partitions, "partitions",            1),
  finished_queue(handle_finished, "finished",                 1),
  running(false)

{

}

GameEngine* GameEngine::get_instance()
{
  if (!p_inst)
  {
    GameEngine::p_inst = new GameEngine();
  }
  return p_inst;
}

GameEngine::~GameEngine() {}


void GameEngine::start_sim()
{
  detect_collision_queue.set_running(true);
  simple_velocity_queue.set_running(true);
  collision_velocity_queue.set_running(true);
  position_queue.set_running(true);
  partition_queue.set_running(true);
  finished_queue.set_running(true);


  game_clock_thread = new std::thread(&GameEngine::run_game_clock, this);
  game_clock_thread->detach();
}

void GameEngine::run_game_clock()
{
  auto next = std::chrono::system_clock::now() + std::chrono::milliseconds{int(GAME_TICK_PERIOD_MS)};
  auto success = std::async(&CycleDependency::external_notify_can_start, dynamic_cast<CycleDependency*>(&detect_collision_queue), external_queue_notification_id);
  while (running)
  {
    std::this_thread::sleep_until(next);
    success = std::async(&CycleDependency::external_notify_can_start, dynamic_cast<CycleDependency*>(&detect_collision_queue), external_queue_notification_id);
    next += std::chrono::milliseconds{int(GAME_TICK_PERIOD_MS)};

    TRACE << "Game tick: " << game_tick++;
    // if game parameters are switched to compile time variables, should be able to use something like:
    //next += GAME_TICK_PERIOD_MSms;
    // ( this operator: `operator""ms` is a constexpr operator for hardcoded values
  }
}





void GameEngine::pause_sim()
{
  running = false;
  detect_collision_queue.set_running(false);
  simple_velocity_queue.set_running(false);
  collision_velocity_queue.set_running(false);
  position_queue.set_running(false);
  partition_queue.set_running(false);
  finished_queue.set_running(false);
}

boost::json::array GameEngine::game_info()
{
  boost::json::array json_players;
  for (auto p : players)
  {
    json_players.push_back(dynamic_cast<Player&>(*p).getGamePieceJson());
  }
  return json_players;
}

std::string GameEngine::game_info_serialized()
{
  boost::json::array json_players = game_info();
  return boost::json::serialize(json_players);
}

void GameEngine::input_player_controls()
{
  detect_collision_queue.external_notify_can_start(external_queue_notification_id);
}


boost::json::object GameEngine::game_config()
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

  
std::shared_ptr<Partition> GameEngine::get_partition(std::shared_ptr<GamePiece> gp)
{
  Cell tmp (gp);
  TRACE << "Cell for get_partition() : " << tmp;
  return spatial_partition[tmp.row()][tmp.col()];
}

void GameEngine::get_partition_and_nearby(std::shared_ptr<GamePiece> gp, std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts)
{
  Cell tmp (gp); 

  TRACE << "finding partitions around Cell: " << tmp;

  for (size_t row = (tmp.row() == 0 ? 0 : tmp.row() - 1); row <= tmp.row() + 1; row++)
  {
    for (size_t col = (tmp.col() == 0 ? 0 : tmp.col() - 1); col <= tmp.col() + 1; col++)
    {
      try
      {
        assert(spatial_partition.at(row).at(col) != nullptr);
        tmp_parts.emplace(spatial_partition.at(row).at(col));
      }
      catch (const std::out_of_range& oor)
      {
        // Nothing to do here... could add logs
      }
    }
  }
}





