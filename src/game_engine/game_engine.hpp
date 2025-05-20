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
  void pause_sim();
  boost::json::array game_info();
  std::string game_info_serialized();
  boost::json::object game_config();

  //std::vector<std::function<void(std::string)>> socket_callbacks;
  //std::vector<std::shared_ptr<websocket_session>> sockets;
  //void register_game_socket(std::function<void(std::string)> callback);
  //void register_game_socket(std::shared_ptr<websocket_session> sock)
  void send_game_state();




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
  void initialize_worker(std::string thread_name_);

  std::vector<GamePieceQueue*> game_loop_queue;
  std::vector<GamePieceQueue*> ready_to_start_queue;

  GamePieceQueue detect_collision_queue;
  GamePieceQueue simple_velocity_queue;
  GamePieceQueue collision_velocity_queue;
  GamePieceQueue update_position_queue;
  GamePieceQueue update_partition_queue;
  GamePieceQueue update_finished_queue;

  void print_queue_change_info(const char* msg = "");
  void gain_sim_loop_access();
  void letgo_sim_loop_access();
  void add_queue_to_loop(CycleDependency* gp_queue);
  void rem_queue_from_loop(CycleDependency* gp_queue);
  void disown_outer_queue();
  void own_outer_queue();
  // in future, potentially make collision_velocity_queue a priority_queue

  //LockedGamePieceQueue<std::priority_queue<std::shared_ptr<GamePiece>, std::vector<std::shared_ptr<GamePiece>>, NearestCollisionComparator>, std::shared_ptr<GamePiece>, VelocityResults> collision_velocity_queue;



  //std::vector<std::vector<Partition*>> spatial_partition;
  std::vector<std::vector<std::shared_ptr<Partition>>> spatial_partition;

//  void update_partitions();
//  void update_positions();
//  void update_velocities();
//  void calculate_next_velocities();
};

#endif
