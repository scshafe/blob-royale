#include <cassert>

#include "boost-log.hpp"

#include "cycle_dependency.hpp"


int id_cycle_dependency_counter = 0;


CycleDependency::CycleDependency(std::function<void(CycleDependency*)> adder_,
                                 std::function<void(CycleDependency*)> remover_) :
  id(id_cycle_dependency_counter++),
  add_self_to_loop(adder_),
  remove_self_from_loop(remover_)
{}

bool CycleDependency::ready_to_begin()
{
  return can_start;
}



void CycleDependency::add_start_dependency(CycleDependency* upstream)
{
  upstream_start[upstream->id] = false;
  upstream->downstream_start.push_back(this);
}


void CycleDependency::add_finish_dependency(CycleDependency* upstream)
{
  upstream_finished[upstream->id] = false;
  upstream->downstream_finished.push_back(this);
}



void CycleDependency::wrap_lock()
{
  TRACE << get_queue_name() << " CycleDependency - attempting to lock...";
  m.lock();
  TRACE << get_queue_name() << " CycleDependency - locked";
}


void CycleDependency::wrap_unlock()
{
  TRACE << get_queue_name() << " CycleDependency - attempting to unlock...";
  m.unlock();
  TRACE << get_queue_name() << " CycleDependency - unlocked";
}

// THIS is the one being notified
void CycleDependency::notify_can_start(int i)
{
  TRACE << get_queue_name() << " notify_can_start()";
  wrap_lock();
  TRACE << get_queue_name() << " successfully locked";
  if (upstream_start.at(i) == false)
  {
    upstream_start.at(i) = true;
    start_notifications++;
    if (start_notifications == upstream_start.size())
    {
      can_start = true;
      add_self_to_loop(this);
    }
  }
  else
  {
    assert(false && "notify_can_start() received repeat call from the same queue");
  }

  wrap_unlock();
}

// THIS is hte one being notified
void CycleDependency::notify_can_be_finished(int i)
{
  TRACE << get_queue_name() << " notify_can_be_finished()";
  wrap_lock();
  if (upstream_finished.at(i) == false)
  {
    upstream_finished.at(i) = true;
    finish_notifications++;
    if (finish_notifications == upstream_finished.size())
    {
      WARNING << get_queue_name() << " can be fully finished";
      can_be_finished = true;
    }
    else
    {
      WARNING << get_queue_name() << " cannot yet be finished";
    }
  }
  else
  {
    assert(false && "notify_can_be_finished() received repeat call from the same queue");
  }
  wrap_unlock();

  test_finished(); // important to test here in case this queue never received any
}


void CycleDependency::test_finished()
{

  if (!unsafe_last_one_done()) return;
  

  TRACE << get_queue_name() << " test_finished()";
  wrap_lock();
  if (can_be_finished == true && last_one_done())
  {
    finished = true;
    for (auto dependency : downstream_start)
    {
      dependency->notify_can_start(id);
    }
    for (auto dependency : downstream_finished)
    {
      dependency->notify_can_be_finished(id);
    }

  }
  wrap_unlock();

  remove_self_from_loop(this);
}


// ------------ RESET CYCLE -----------

void CycleDependency::reset_cycle()
{
  finished = false;
  can_start = upstream_start.size() == 0;
  can_be_finished = upstream_finished.size() == 0;
  start_notifications = 0;
  finish_notifications = 0; 
}


