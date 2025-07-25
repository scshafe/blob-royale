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
  OperationResult (*operation)(Object);
  
  std::unordered_map<OperationResult, std::string> next_queue_name_map;
  std::unordered_map<OperationResult, std::function<void(Object)>> next_queue_map;

  QueueType q;
  Object obj;

  bool running = false;

  std::vector<std::thread*> workers;

public:
 
  LockedDependencyQueue(OperationResult (*operation_)(Object),
                        std::string name_,
                        int worker_count_) : 
    CycleDependency(name_),
    operation(operation_)
  {
    for (int i = 0; i < worker_count_; i++)
    {
      std::string thread_name = name + "-" + std::to_string(i);
      workers.push_back(new std::thread(&LockedDependencyQueue::initialize_worker, this, thread_name));
      workers[i]->detach();
    }
    waiting_workers = worker_count_;
    worker_count = worker_count_;
  }


  void initialize_worker(std::string thread_name_)
  {
    boost::log::core::get()->add_thread_attribute("ThreadName",
        boost::log::attributes::constant< std::string >(thread_name_));
  
    perform_operation_worker();
  }



  void add_send_to_option(OperationResult op_res, LockedDependencyQueue* receiving_queue)
  {
    if (next_queue_name_map.find(op_res) != next_queue_name_map.end() ||
        next_queue_map.find(op_res) != next_queue_map.end())
    {
      ERROR << "ERROR: already added " << *receiving_queue << " to " << *this;
    }

    next_queue_name_map[op_res] = receiving_queue->name;
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


  virtual bool last_one_done(bool external_call)
  { 
    ENTRANCE << *this << " last_one_done().  external_call[" << (external_call ? "true" : "false") << "]";
    if (!can_be_finished)            TRACE << "can_be_finished == false";
    if (!q.empty())                  TRACE << "queue not empty";
    TRACE << "operations in progress: " << operations_in_progress;

    bool last_op = external_call ? operations_in_progress == 0 : operations_in_progress == 1;
    TRACE << "last op: " << (last_op ? "true" : "false");

    return can_be_finished && q.empty() && last_op;
  }

  virtual std::string container_info() const
  {
    return "queue-size: [" + std::to_string(q.size()) + "]";
  }

  bool ready_for_work()
  {
    return running &&
           can_start == true &&
           //finished == false &&
           !q.empty() ;
  }

  void perform_operation_worker()
  {
    ENTRANCE << "perform_operation_worker()";

    run_with_worker_lock([this] (std::unique_lock<std::mutex> lock) {
      //worker_running();
      while (!ready_for_work())
      {
        //worker_waiting();
        worker_cv.wait(lock, [this]{ return ready_for_work(); });
        //worker_running();
      }
      obj = q.front();
      q.pop();
      operations_in_progress++;  
          
    });

    OperationResult res = operation(obj);
    next_queue_map[res](obj);

    
    run_with_worker_lock([this] (std::unique_lock<std::mutex> lock) {
      test_finished(false);
      operations_in_progress--;
      //worker_waiting();
    });

    perform_operation_worker();
  }



  void receive_game_piece(Object gp)
  {

    run_with_worker_lock([this, gp] (std::unique_lock<std::mutex> lock) {
      q.push(gp);
      WARNING << *this << " receiving " << *gp;
    });

    worker_cv.notify_one();
  }

  friend std::ostream& operator<<(std::ostream& os, const LockedDependencyQueue& ldq)
  {
    return os << ldq.name;
  }
};






