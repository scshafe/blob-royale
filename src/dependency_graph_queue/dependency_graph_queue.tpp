#pragma once

#include <queue>
#include <mutex>


/* Locked Queues:
 *
 * start: 
 *  - detect_collisions
 *  - send to update_velocity or calculate_collision
 *  - wait on finished
 *
 * 2.1  calculate_collision:
 *      - calculate_collision
 *      - send to update_position
 *      - wait N/A
 *
 * 2.2  update_velocity
 *      - update_velocity
 *      - send to update_position
 *      - wait N/A
 * 
 * update_position:
 *  - update_position
 *  - send to update_partitions
 *  - waits on detect_collisions
 *
 * update_partitions:
 *  - update_partitions
 *  - send to finished
 *  - waits N/A
 *
 * finished:
 *  - N/A
 *  - send to start
 *  - waits on update_partitions
 * 
 */



#define GRAB_LOCK TRACE << "locking()..."; m.lock(); TRACE << "locked()";






template<typename GamePiece>
class LockedGamePieceQueue : public CycleDependency
{ 
  std::function<bool(std::shared_ptr<GamePiece>)> operation;
  std::function<void(std::shared_ptr<GamePiece>)> send_to_next_queue;

  std::mutex queue_lock;
  std::queue<std::shared_ptr<GamePiece>> q;

  int operations_in_progress = 0;

public:

  LockedGamePieceQueue(int id_, std::function<bool(std::shared_ptr<GamePiece>)> operation_) : 
    CycleDependency(id_),
    operation(operation_)
  {}
  
  void set_send_to_next_queue(std::function<void(std::shared_ptr<GamePiece>)> send_to_next_queue_)
  {
    send_to_next_queue = send_to_next_queue_;
  }


  virtual bool last_one_done()
  { 
    queue_lock.lock();
    bool finished = q.empty() && operations_in_progress == 0;
    queue_lock.unlock();
    return finished;
  }


  bool perform_operation()
  {
    if (finished == true)
    {
      return false;
    }
    if (can_start == false)    // shouldn't need to be locked
    {
      return false;
    }
    if (queue_lock.try_lock() == false)
    {
      return false;
    }
    
    std::shared_ptr<GamePiece> gp = q.front();
    q.pop();
    operations_in_progress++;
    queue_lock.unlock();

    bool success = operation(gp);

    queue_lock.lock();
    operations_in_progress--;
    if (success == false)
    {
      q.push(gp);
      queue_lock.unlock();
      // this only returns true if the queues are being looped through
      // might need to change if making an outer queue of LockedGamePieceQueue's
      return true;
    }
    else
    { 
      queue_lock.unlock();
      send_to_next_queue(gp);
      test_finished();
      return true;
    }
    
  }


  void receive_game_piece(std::shared_ptr<GamePiece> gp)
  {
    queue_lock.lock();
    q.push(gp);
    queue_lock.unlock();
  }












  // -------------------------------------------- last night -----


  void receive_notify_can_be_finished(int i);

  std::unordered_map<bool, std::function<void(std::shared_ptr<GamePiece>)>> send_to_map; // this can be generalized to be another template typename instead of bool as hashkey

  std::function<bool(void)> dependency;


  void set_dependency(std::function<bool(void)> dep)
  {
    dependency = dep;
  }

  void set_send_to_map(std::unordered_map<bool, std::function<void(std::shared_ptr<GamePiece>)>> send_to_map_)
  {
    send_to_map = send_to_map_;
  }

  void set_need_completed(const int& need_completed_)
  {
    need_completed = need_completed_;
  }

  void reset()
  {
    assert(q.empty());
    completed = 0;
    need_completed = 0;
  }

  bool is_completed()
  {
    m.lock();
    if (need_completed == completed)
    {
      assert(q.empty());
      m.unlock();
      return true;
    }
    m.unlock();
    return false;
  }


  void add_to_queue(std::shared_ptr<GamePiece> gp)
  {
    m.lock();
    q.push(gp);
    m.unlock();
  }

  void perform_op()
  {
    m.lock();
    while (!q.empty())
    {




  bool is_complete


