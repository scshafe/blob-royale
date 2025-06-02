#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <vector>
#include <functional>
#include <unordered_map>
#include <array>




class CycleDependency 
{
public:
  bool ready_to_begin();

  // ----------- SETTERS ---------------
  void print_dependency_relations();
  void add_start_dependencies(std::vector<CycleDependency*> upstream);
  void add_finish_dependencies(std::vector<CycleDependency*> upstream);
  int register_external_start_dependency();

  CycleDependency(std::string name_);

protected:

  std::string name = "example";

  // these are used by the above layer
  std::mutex worker_lock;
  std::condition_variable worker_cv;
  unsigned int waiting_workers = 0;
  unsigned int worker_count = 0;

//  void worker_running();
//  void worker_waiting();
//  unsigned int get_waiting_workers();
  void run_with_worker_lock(std::function<void(std::unique_lock<std::mutex>)> func);
  void run_with_dependency_lock(std::function<void(std::unique_lock<std::mutex>)> func);

  bool check_can_start();
  bool can_start = false;
  bool can_be_finished = false;
  //bool finished = false;
  void test_finished(bool external_call);
  unsigned int operations_in_progress = 0;

  virtual bool last_one_done(bool external_call) = 0;
  virtual std::string container_info() const = 0;

private:

  std::function<void(CycleDependency*)> remove_self_from_loop;
  std::function<void(CycleDependency*)> add_self_to_loop;


  std::mutex dependency_lock;
  std::condition_variable dependency_cv;
  std::mutex worker_waiting_lock;
  int id;
 

  std::vector<CycleDependency*> downstream_start;
  std::vector<CycleDependency*> downstream_finished;

  int external_notifications = 0;
  int start_notifications = 0;
  int finish_notifications = 0;

  std::unordered_map<int, bool> external_start;
  std::unordered_map<CycleDependency*, bool> upstream_start;
  std::unordered_map<CycleDependency*, bool> upstream_finished;


  // ----------- NOTIFIERS ---------------
public:
  // THIS is the one being notified
  void notify_can_start(CycleDependency* dep);
  
  // THIS is hte one being notified
  void notify_can_be_finished(CycleDependency* dep);
  
  void external_notify_can_start(int dep);

  // ------------ RESET CYCLE -----------

  void notify_dependents();
  void reset_cycle();
  
  bool operator==(const CycleDependency& other) const;
  friend std::ostream& operator<<(std::ostream& os, const CycleDependency& dep);
  std::string print_comprehensive() const;

private:

  template<typename T, typename K>
  void handle_notification(T container, K key, int& notification_count)
  {
    try
    {
      if (container.at(key) == false)
      {
        container.at(key) = true;
        notification_count++;
      }
      else
      {
  
      }
    }
    catch (const std::out_of_range& oor)
    {
      assert(false && "handle_notifications() received duplicate");
    }
  }
};





