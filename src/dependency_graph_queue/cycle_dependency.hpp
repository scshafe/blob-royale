#pragma once


#include <mutex>
#include <queue>
#include <vector>
#include <function>
#include <unordered_map>





class CycleDependency
{
public:
  bool ready_to_begin();

  // ----------- SETTERS ---------------
  void add_start_dependency(CycleDependency* upstream);
  void add_finished_dependency(CycleDependency* upstream);


protected:

  bool finished = false;
  void test_finished();

  virtual bool last_one_done() = 0;

private:
  std::mutex m;
  int id;
  bool can_start = false;
  bool can_be_finished = false;
  
  std::vector<CycleDependency*> downstream_start;
  std::vector<CycleDependency*> downstream_finished;

  int start_notifications = 0;
  int finish_notifications = 0;
  std::unordered_map<int, bool> upstream_start;
  std::unordered_map<int, bool> upstream_finished;

  CycleDependency(id_) : id(id_){}


  // ----------- NOTIFIERS ---------------

  // THIS is the one being notified
  void notify_can_start(int i);
  
  // THIS is hte one being notified
  void notify_can_be_finished(int i);
  
  

  // ------------ RESET CYCLE -----------

  void reset_cycle();
  

};









// ----------------- This could be implemented with pointers to functions instead of other CycleDependency
//  test later to see if it's faster?



//class CycleDependency
//{
//
//  int id;
//  bool can_start = false;
//  bool can_be_finished = false;
//  std::mutex m;
//  std::queue<std::shared_ptr<GamePiece>> q;
//  
//  std::vector<std::function<void(int)> notify_can_start;
//  std::vector<std::function<void(int)> notify_can_be_finished;
//
//  int notifications_received_wait_to_start = 0;
//  int notifications_received_wait_to_can_be_finished = 0;
//  std::unordered_map<int, bool> wait_to_start;
//  std::unordered_map<int, bool> wait_to_can_be_finished;
//
//  CycleDependency(id_) : id(id_){}
//
//  // ----------- SETTERS ---------------
//
//  void add_can_start_dependency(CycleDependency* upstream);
//
//  void add_can_be_finished_dependency(CycleDependency* upstream);
//
//  void set_notify_can_start_vector(std::vector<std::function<void(int)> vec)
//  {
//    notify_can_start = vec;
//  }
//
//  void set_notify_can_be_finished(std::vector<std::function<void(int)> vec)
//  {
//    notifyc_can_be_finished = vec;
//  }
//
//  void set_wait_to_start(std::unordered_map<int, bool> wait_m)
//  {
//    wait_to_start = wait_m;
//  }
//
//  void set_wait_to_can_be_finished(std::unordered_map<int, bool> wait_m)
//  {
//    wait_to_can_be_finished = wait_m;
//  }
//
//
//  // ----------- NOTIFIERS ---------------
//
//  void receive_notify_can_start(int i)
//  {
//    m.lock();
//    if (wait_to_start.at(i) == false)
//    {
//      wait_to_start.at(i) = true;
//      notifications_received_wait_to_start++;
//      if (notifications_receieved_wait_to_start == wait_to_start.size())
//      {
//        can_start = true;
//      }
//    }
//    m.unlock();
//  }
//
//  void receive_notify_can_be_finished(int i)
//  {
//    m.lock();
//    if (wait_to_can_be_finished.at(i) == false)
//    {
//      wait_to_can_be_finished.at(i) = true;
//      notifications_received_wait_to_can_be_finished++;
//      if (notifications_received_wait_to_can_be_finished == wait_to_can_be_finished.size())
//      {
//        can_be_finished = true;
//      }
//    }
//    m.unlock();
//  }
//
//  void test_finished()
//  {
//    m.lock();
//    if (can_be_finished == true && q.empty())
//    {
//      for (auto notify : notify_can_start)
//      {
//        
//      }
//      for (auto notify : notify_can_be_finished)
//      {
//        notify(id);
//      }
//    }
//  }
//
//
//  // ------------ RESET CYCLE -----------
//
//  void reset_cycle()
//  {
//    can_start = wait_to_start.size() == 0;
//    can_be_finished = wait_to_can_be_finished.size() == 0;
//    notifications_received_wait_to_start = 0;
//    notifications_received_wait_to_can_be_finished = 0; 
//  }
//
//
//};
//
//
//
//
//
//
//
//
//
//
//
//
//
