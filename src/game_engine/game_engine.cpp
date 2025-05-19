#include <iostream>
#include <sstream>
#include <cassert>
#include <stdexcept>
#include <algorithm>
#include <functional>

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

  collision_velocity_queue.add_start_dependency(&detect_collision_queue);
  collision_velocity_queue.add_finish_dependency(&detect_collision_queue);

  simple_velocity_queue.add_start_dependency(&update_finished_queue);
  simple_velocity_queue.add_finish_dependency(&detect_collision_queue);
  simple_velocity_queue.add_finish_dependency(&collision_velocity_queue);

  update_position_queue.add_start_dependency(&detect_collision_queue);
  //update_position_queue.add_finish_dependency(&collision_velocity_queue);
  update_position_queue.add_finish_dependency(&simple_velocity_queue);

  update_partition_queue.add_start_dependency(&detect_collision_queue);
  update_partition_queue.add_finish_dependency(&update_position_queue);

  update_finished_queue.add_start_dependency(&update_partition_queue);
  update_finished_queue.add_finish_dependency(&update_partition_queue);


  // can probably relax to:                   update_partition_queue
  detect_collision_queue.add_start_dependency(&update_finished_queue);
  // can probably relax to:                    update_partition_queue
  detect_collision_queue.add_finish_dependency(&update_finished_queue);
  
  // ADD SEND MAPS

//typedef std::function<void(std::shared_ptr<GamePiece>)> AddQueueFunc;
  AddQueueFunc dc_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &detect_collision_queue,    std::placeholders::_1);
  AddQueueFunc sv_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &simple_velocity_queue,     std::placeholders::_1);
  AddQueueFunc cv_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &collision_velocity_queue,  std::placeholders::_1);
  AddQueueFunc pos_ptr =  std::bind(&GamePieceQueue::receive_game_piece, &update_position_queue,     std::placeholders::_1);
  AddQueueFunc part_ptr = std::bind(&GamePieceQueue::receive_game_piece, &update_partition_queue,    std::placeholders::_1);
  AddQueueFunc uf_ptr =   std::bind(&GamePieceQueue::receive_game_piece, &update_finished_queue,     std::placeholders::_1);


  detect_collision_queue.add_send_to_option(QueueOperationResults::send_back, &detect_collision_queue);
  detect_collision_queue.add_send_to_option(QueueOperationResults::option1,   &simple_velocity_queue);
  detect_collision_queue.add_send_to_option(QueueOperationResults::option2,   &collision_velocity_queue);


  collision_velocity_queue.add_send_to_option(QueueOperationResults::send_back, &collision_velocity_queue);
  collision_velocity_queue.add_send_to_option(QueueOperationResults::option1, &update_position_queue);
  collision_velocity_queue.add_send_to_option(QueueOperationResults::option2, &simple_velocity_queue);


  //simple_velocity_queue.add_send_to_option(QueueOperationResults::send_back, &simple_velocity_queue);
  simple_velocity_queue.add_send_to_option(QueueOperationResults::option1, &update_position_queue);

  //update_position_queue.add_send_to_option(QueueOperationResults::send_back, &update_position_queue);
  update_position_queue.add_send_to_option(QueueOperationResults::option1, &update_partition_queue);


  //update_partition_queue.add_send_to_option(QueueOperationResults::send_back, &update_partition_queue);
  update_partition_queue.add_send_to_option(QueueOperationResults::option1, &update_finished_queue);


  //update_finished_queue.add_send_to_option(QueueOperationResults::send_back, &update_finished_queue);
  update_finished_queue.add_send_to_option(QueueOperationResults::option1, &detect_collision_queue);

  active_threads += 1;

  for (auto p : players)
  {
    detect_collision_queue.receive_game_piece(p);
  }


  detect_collision_queue.notify_can_start(5);
  detect_collision_queue.notify_can_be_finished(5);

  simple_velocity_queue.notify_can_start(5);

  active_threads -= 1;
}

GameEngine::GameEngine() :
  players(),
  width(MAP_WIDTH),
  height(MAP_HEIGHT),
  detect_collision_queue(detect_collisions, "detect_collisions",     std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  simple_velocity_queue(simple_velocity, "simple_velocity",          std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  collision_velocity_queue(collision_velocity, "collision_velocity", std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  update_position_queue(update_position, "position",                 std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  update_partition_queue(update_partitions, "partitions",            std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  update_finished_queue(handle_finished, "finished",                 std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1), std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1)),
  running(false)


//  spatial_partition(std::vector<std::vector<std::shared_ptr<Partition>>>(SPATIAL_PARTITION_ROWS, std::vector<std::shared_ptr<Partition>>(SPATIAL_PARTITION_COLS)))
{
  std::function<void(CycleDependency*)> add_to_loop = std::bind(&GameEngine::add_queue_to_loop, this, std::placeholders::_1);
  std::function<void(CycleDependency*)> rem_from_loop = std::bind(&GameEngine::rem_queue_from_loop, this, std::placeholders::_1);

//  detect_collision_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
//  simple_velocity_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
//  collision_velocity_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
//  update_position_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
//  update_partition_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
//  update_finished_queue.set_outer_loop_callbacks(add_to_loop, rem_from_loop);
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

// allows the class object to be passed to a new thread
void GameEngine::operator()()
{
  sim_loop();
}


void GameEngine::initialize_worker(std::string thread_name_)
{
  boost::log::core::get()->add_thread_attribute("ThreadName",
      boost::log::attributes::constant< std::string >(thread_name_));

  sim_loop();
}

void GameEngine::start_sim()
{
  running = true;
  for (int i = 1; i <= WORKER_COUNT; i++)
  {
    std::string thread_name = "work" + std::to_string(i);
    //std::thread t = std::thread(&GameEngine::initialize_worker, this, thread_name);
    workers.push_back(new std::thread(&GameEngine::initialize_worker, this, thread_name));
    
  }
  for (auto& w : workers)
  {
    w->detach();
  }
}





void GameEngine::add_queue_to_loop(CycleDependency* gp_queue)
{ 
  LOCK << "attempting change outer queue lock...";  
  std::unique_lock<std::mutex> lock(m);
  LOCK << "outer queue locked.";  
  queue_changing = true;
  changing_threads += 1;

  
  OUTER_QUEUE_CHANGE << "active threads: " << active_threads;
  OUTER_QUEUE_CHANGE << "changing threads: " << changing_threads;
  
  while (active_threads - changing_threads != 0)
  {
    cv.wait(lock, [this]{ return active_threads - changing_threads == 0; });
    WARNING << "cv.wait() wakeup in add_queue_to_loop()";
  }
 
  game_loop_queue.push_back(dynamic_cast<GamePieceQueue*>(gp_queue));
  TRACE << "Game loop queue new size: " << game_loop_queue.size();

  changing_threads -= 1;
  queue_changing = !(changing_threads == 0);
  OUTER_QUEUE_CHANGE << "changing threads: " << changing_threads;
  OUTER_QUEUE_CHANGE << "queue changing: " << (queue_changing ? "true" : "false");
  lock.unlock();
  cv.notify_all();
}


void GameEngine::print_queue_change_info(const char* msg)
{
  OUTER_QUEUE_CHANGE << "active threads: " << active_threads;
  OUTER_QUEUE_CHANGE << "changing threads: " << changing_threads;
}

void GameEngine::rem_queue_from_loop(CycleDependency* gp_queue)
{
  ENTRANCE << "rem_queue_from_loop()";
  LOCK << "attempting change outer queue lock...";  
  std::unique_lock<std::mutex> lock(m);
  LOCK << "outer queue locked.";  
  queue_changing = true;
  changing_threads += 1;

  print_queue_change_info();
  
  while (active_threads - changing_threads != 0)
  {
    cv.wait(lock, [this]{ return active_threads - changing_threads == 0; });
    WARNING << "cv.wait() wakeup in rem_queue_to_loop()";
  }
  
  for (auto it = game_loop_queue.begin(); it != game_loop_queue.end(); ++it)
  {
    if ((*dynamic_cast<CycleDependency*>(*it)) == *gp_queue)
    {
      game_loop_queue.erase(it);
      break;
    }
  }
  TRACE << "Game loop queue new size: " << game_loop_queue.size();
  changing_threads -= 1;
  queue_changing = changing_threads == 0 ? false : true;
  OUTER_QUEUE_CHANGE << "changing threads: " << changing_threads;
  OUTER_QUEUE_CHANGE << "queue changing: " << (queue_changing ? "true" : "false");
  lock.unlock();
  cv.notify_all();
}

void GameEngine::gain_sim_loop_access()
{
  ENTRANCE << "gain_sim_loop_access()";
  std::unique_lock<std::mutex> lock(m);
  while (queue_changing)
  {
    cv.wait(lock, [this](){ return !queue_changing; });
  }
  active_threads += 1;
  OUTER_QUEUE_CHANGE << "active threads: " << active_threads;
  OUTER_QUEUE_CHANGE << "queue changing: " << (queue_changing ? "true" : "false");
}

void GameEngine::letgo_sim_loop_access()
{
  ENTRANCE << "letgo_sim_loop_access()";
  std::unique_lock<std::mutex> lock(m);
  active_threads -= 1;
  OUTER_QUEUE_CHANGE << "active threads: " << active_threads;
  lock.unlock();
  cv.notify_all();  // can probably use 2 separate CV's (one for workers in sim loop, and one for workers changing the queue
}

void GameEngine::sim_loop()
{
  while (true)
  {
    gain_sim_loop_access();

    unsigned int tick_count = 0;
    while (!queue_changing)
    {
      LOG << "GameEngine::sim_loop() tick: " << tick_count++ << " with period: " << GAME_TICK_PERIOD_US; // not thread safe but doesn't matter
    
      for (int i = 0; i < game_loop_queue.size() && !queue_changing; i++)
      {
        if (game_loop_queue[i]->perform_operation()) break;
      }
      //usleep(GAME_TICK_PERIOD_US);
    }
    letgo_sim_loop_access(); 
  }
}

void GameEngine::pause_sim()
{
  running = false;
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





