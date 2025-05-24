#pragma once

#include <queue>
#include <mutex>

#include "cycle_dependency.hpp"

#include "boost-log.hpp"
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



//#define GRAB_LOCK TRACE << "locking()..."; m.lock(); TRACE << "locked()";





template<typename QueueType, typename Object, typename OperationResult>
class LockedDependencyQueue : public CycleDependency
{ 
  //std::function<bool(Object)> operation;
  OperationResult (*operation)(Object);
  

  //std::unordered_map<OperationResult, std::function<void(Object)>> next_queue_map;
  //std::unordered_map<OperationResult, void (LockedDependencyQueue::*)(Object)> next_queue_map;
  std::string queue_name;
  std::unordered_map<OperationResult, std::string> next_queue_name_map;

  std::unordered_map<OperationResult, std::function<void(Object)>> next_queue_map;

  std::mutex queue_lock;
  std::mutex worker_lock;
  std::condition_variable worker_cv;
  QueueType q;

  bool running = false;
  int operations_in_progress = 0;

  std::vector<std::thread*> workers;

public:
 
  LockedDependencyQueue(OperationResult (*operation_)(Object),
                        std::string queue_name_,
                        int worker_count_) : 
    CycleDependency(),
    operation(operation_),
    queue_name(queue_name_)
  {
    for (int i = 0; i < worker_count_; i++)
    {
      std::string thread_name = queue_name + std::to_string(i);
      workers.push_back(new std::thread(&LockedDependencyQueue::initialize_worker, this, thread_name));
      workers[i]->detach();
    }
  }


  void initialize_worker(std::string thread_name_)
  {
    boost::log::core::get()->add_thread_attribute("ThreadName",
        boost::log::attributes::constant< std::string >(thread_name_));
  
    perform_operation_worker();
  }


  void wrap_q_lock()
  {
    LOCK << queue_name << " attempting to lock...";
    queue_lock.lock();
    LOCK << queue_name << " successfully locked";
  }

  void wrap_q_unlock()
  {
    LOCK << queue_name << " attempting to lock...";
    queue_lock.unlock();
    LOCK << queue_name << " successfully locked";
  }

  void add_send_to_option(OperationResult op_res, LockedDependencyQueue* receiving_queue)
  {
    if (next_queue_name_map.find(op_res) != next_queue_name_map.end() ||
        next_queue_map.find(op_res) != next_queue_map.end())
    {
      ERROR << "ERROR: already added " << receiving_queue->queue_name << " to " << queue_name;
    }

    next_queue_name_map[op_res] = receiving_queue->queue_name;
    next_queue_map[op_res] = std::bind(&LockedDependencyQueue::receive_game_piece, receiving_queue, std::placeholders::_1);
  }
  
  void set_next_queue_map(std::unordered_map<OperationResult, std::function<void(Object)>> next_queue_map_)
  {
    next_queue_map = next_queue_map_;
  }
  void set_next_queue_name_map(std::unordered_map<OperationResult, std::string> next_queue_name_map_)
  {
    next_queue_name_map = next_queue_name_map_;
  }

  void set_running(const bool running_)
  {
    running = running_;
    worker_cv.notify_one();
  }


  // a much quicker pre-test to see if locking is worth it
  virtual bool unsafe_last_one_done()
  {
    return q.empty() && operations_in_progress == 0;
  }

  // this one is much slower but guarantees correctness
  virtual bool last_one_done()
  { 
    ENTRANCE << get_queue_name() << " last_one_done()";
    wrap_q_lock();
    bool finished = q.empty() && operations_in_progress == 0;
    wrap_q_unlock();
    return finished;
  }

  virtual std::string get_queue_name()
  {
    return queue_name;
  }

  bool ready_for_work()
  {
    return running &&
           can_start == true &&
           finished == false &&
           !q.empty() ;
  }

  void perform_operation_worker()
  {
    Object obj;
    {
      std::unique_lock lock(worker_lock);
      while (!ready_for_work())
      {
        worker_cv.wait(lock, [this]{ return ready_for_work(); });
      }
      obj = q.front();
      q.pop();
      operations_in_progress++;  
    }

    OperationResult res = operation(obj);
    next_queue_map[res](obj);

    {
      std::unique_lock lock(worker_lock);
      operations_in_progress--;
    }

    test_finished();

    perform_operation_worker();
  }


//  bool perform_operation()
//  {
//    ENTRANCE << get_queue_name() << " perform_operation()";
//    // assert(can_start && "can_start is not true so queue should not be in use");  // this won't be the case for other threads finishing their loop before the outer queue change
//    
//
//    if (finished == true)
//    {
//      return false;
//    }
//    if (queue_lock.try_lock() == false)
//    {
//      return false;
//    }
//    if (q.empty())
//    {
//      wrap_q_unlock();
//      TRACE << get_queue_name() << " skipping empty queue";
//      return false;
//    }
//    TRACE << get_queue_name() << " popping the front";
//    Object gp = q.front();
//    TRACE << get_queue_name() << " popped";
//    q.pop();
//    operations_in_progress++;
//    wrap_q_unlock();
//
//    OperationResult res = operation(gp);
//    next_queue_map[res](gp);
//
//    wrap_q_lock();
//    operations_in_progress--;
//    wrap_q_unlock();
//
//    WARNING << "sending " << *gp << " from " << queue_name << " to " << next_queue_name_map[res];
//    
//    return test_finished();
//    
//  }


  void receive_game_piece(Object gp)
  {
    ENTRANCE << queue_name << " receive_game_piece()";
    wrap_q_lock();
    WARNING << queue_name << " receiving " << *gp;
    q.push(gp);
    wrap_q_unlock();
  }

};






