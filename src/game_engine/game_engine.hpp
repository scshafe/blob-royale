#ifndef _GAME_STATE_HPP_
#define _GAME_STATE_HPP_

#include <thread>
#include <vector>
#include <string>
#include <memory>
#include <set>
#include <queue>
#include <mutex>
#include <condition_variable>

#include "player.hpp"
//#include "map_object.hpp"
#include "game_piece.hpp"
#include "partition.hpp"

#include "dependency_graph_queue.tpp"


// Custom types to use with LockedDependencyQueue interface


typedef LockedDependencyQueue<std::queue<std::shared_ptr<GamePiece>>, std::shared_ptr<GamePiece>, QueueOperationResults> GamePieceQueue;

//typedef void (GamePieceQueue::*AddQueueFunc)(std::shared_ptr<GamePiece>); // define name for functions that add objects to next queue

//typedef std::function<void((GamePieceQueue::*)(std::shared_ptr<GamePiece>), GamePieceQueue*, const std::Playerholder<1>&>::type)> AddQueueFunc;


//typedef std::function<void, GamePieceQueue::*receive_game_piece, GamePieceQueue*, std::shared_ptr<GamePiece>)> AddQueueFunc;

typedef std::function<void(std::shared_ptr<GamePiece>)> AddQueueFunc;

typedef std::function<void(CycleDependency*)> LoopModifyingCallback;


typedef std::unordered_map<QueueOperationResults, AddQueueFunc> QueueMap;


struct  NearestCollisionComparator {
  bool operator()( const std::shared_ptr<GamePiece> a, const std::shared_ptr<GamePiece> b) const;
} ;


static QueueOperationResults detect_collisions(std::shared_ptr<GamePiece> gp);
static QueueOperationResults collision_velocity(std::shared_ptr<GamePiece> gp);
static QueueOperationResults simple_velocity(std::shared_ptr<GamePiece> gp);
static QueueOperationResults update_position(std::shared_ptr<GamePiece> gp);
static QueueOperationResults update_partitions(std::shared_ptr<GamePiece> gp);
static QueueOperationResults handle_finished(std::shared_ptr<GamePiece> gp);




//class GameEngine : public CycleDependencyExternalInterface {
class GameEngine {
public:
  void initialize(std::string testfile);
  GameEngine();
  ~GameEngine();

  static GameEngine* get_instance();

  // ---- external facing ----
  void operator()();
  void sim_loop();
  void start_sim();
  void run_game_clock();
  void run_benchmark();
  void pause_sim();
  boost::json::array game_info();
  std::string game_info_serialized();
  boost::json::object game_config();





  std::shared_ptr<Partition> get_partition(std::shared_ptr<GamePiece> gp);
  void get_partition_and_nearby(std::shared_ptr<GamePiece> gp, std::set<std::shared_ptr<Partition>, std::less<std::shared_ptr<Partition>>>& tmp_parts);

private:
  static GameEngine* p_inst;
  std::vector<std::shared_ptr<GamePiece>> players;

  std::vector<std::shared_ptr<GamePiece>> stationary_map_objects;
 // std::vector<MapObject*> map_objects;
  float width;
  float height;
  bool running;
  int id_counter = 0;


  std::condition_variable cv;
  std::mutex m;
  bool queue_changing = false;
  int changing_threads = 0;
  int active_threads = 0;
  std::vector<std::thread*> workers;
  std::thread* game_clock_thread;
  unsigned int game_tick = 0;
  void initialize_worker(std::string thread_name_);

  std::vector<GamePieceQueue*> game_loop_queue;
  std::vector<GamePieceQueue*> ready_to_start_queue;

  GamePieceQueue detect_collision_queue;
  GamePieceQueue simple_velocity_queue;
  GamePieceQueue collision_velocity_queue;
  GamePieceQueue position_queue;
  GamePieceQueue partition_queue;
  GamePieceQueue finished_queue;

  int external_queue_notification_id;
  void input_player_controls();

  std::vector<std::vector<std::shared_ptr<Partition>>> spatial_partition;

};

#endif
