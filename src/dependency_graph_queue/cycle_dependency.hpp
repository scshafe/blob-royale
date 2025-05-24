#pragma once


#include <mutex>
#include <queue>
#include <vector>
#include <functional>
#include <unordered_map>





class CycleDependency
{
public:
  bool ready_to_begin();

  // ----------- SETTERS ---------------
  void add_start_dependency(CycleDependency* upstream);
  void add_finish_dependency(CycleDependency* upstream);

  CycleDependency();

protected:

  bool can_start = false;
  bool can_be_finished = false;
  bool finished = false;
  bool test_finished();

  virtual bool unsafe_last_one_done() = 0;
  virtual bool last_one_done() = 0;
  virtual std::string get_queue_name() = 0;

private:

  std::function<void(CycleDependency*)> remove_self_from_loop;
  std::function<void(CycleDependency*)> add_self_to_loop;

  std::mutex m;
  int id;
  
  std::vector<CycleDependency*> downstream_start;
  std::vector<CycleDependency*> downstream_finished;

  int start_notifications = 0;
  int finish_notifications = 0;
  std::unordered_map<int, bool> upstream_start;
  std::unordered_map<int, bool> upstream_finished;

  void wrap_lock();
  void wrap_unlock();

  // ----------- NOTIFIERS ---------------
public:
  // THIS is the one being notified
  void notify_can_start(int i);
  
  // THIS is hte one being notified
  void notify_can_be_finished(int i);
  

  // ------------ RESET CYCLE -----------

  void reset_cycle();
  
  bool operator==(const CycleDependency& other) const;
};





