#include <cassert>

#include "boost-log.hpp"

#include "cycle_dependency.hpp"


int id_cycle_dependency_counter = 0;


CycleDependency::CycleDependency() :
  id(id_cycle_dependency_counter++)
{}

bool CycleDependency::ready_to_begin()
{
  ENTRANCE << get_queue_name() << " ready_to_begin()";
  return can_start;
}



void CycleDependency::add_start_dependency(CycleDependency* upstream)
{
  ENTRANCE << get_queue_name() << " add_start_dependency() " << upstream->get_queue_name();
  upstream_start[upstream->id] = false;
  upstream->downstream_start.push_back(this);
}


void CycleDependency::add_finish_dependency(CycleDependency* upstream)
{
  ENTRANCE << get_queue_name() << " add_finish_dependency() " << upstream->get_queue_name();
  upstream_finished[upstream->id] = false;
  upstream->downstream_finished.push_back(this);
}



void CycleDependency::wrap_lock()
{
  LOCK << get_queue_name() << " CycleDependency - attempting to lock...";
  m.lock();
  LOCK << get_queue_name() << " CycleDependency - locked";
}


void CycleDependency::wrap_unlock()
{
  LOCK << get_queue_name() << " CycleDependency - attempting to unlock...";
  m.unlock();
  LOCK << get_queue_name() << " CycleDependency - unlocked";
}

// THIS is the one being notified
void CycleDependency::notify_can_start(int i)
{
  ENTRANCE << get_queue_name() << " notify_can_start()";
  wrap_lock();

  if (finished)
  {
    reset_cycle();
  }

  try
  {
    if (upstream_start.at(i) == false)
    {
      upstream_start.at(i) = true;
      start_notifications++;
      if (start_notifications == upstream_start.size())
      {
        WARNING << get_queue_name() <<  " can start - adding self to outer queue";
        can_start = true;
      }
    }
    else
    {
      assert(false && "notify_can_start() received repeat call from the same queue");
    }
  }
  catch (const std::out_of_range& oor)
  {
    assert(false && "notify_can_start() received invalid id");
  }

  wrap_unlock();
}

// THIS is hte one being notified
void CycleDependency::notify_can_be_finished(int i)
{
  ENTRANCE << get_queue_name() << " notify_can_be_finished()";
  wrap_lock();
  try
  {
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
  }
  catch (const std::out_of_range& oor)
  {
    assert(false && "notify_can_be_finished() received invalid id");
  }
  wrap_unlock();

  test_finished(); // important to test here in case this queue never received any
}


bool CycleDependency::test_finished()
{
  ENTRANCE << get_queue_name() << " test_finished()";

  if (!unsafe_last_one_done()) return false;
  

  wrap_lock();
  if (can_be_finished == true && finished == false && last_one_done())
  {
    WARNING << get_queue_name() << " is finished";
    finished = true;
    wrap_unlock();
    for (auto dependency : downstream_start)
    {
      dependency->notify_can_start(id);
    }
    for (auto dependency : downstream_finished)
    {
      dependency->notify_can_be_finished(id);
    }
    return true;
  }
  wrap_unlock();
  return false;

}


// ------------ RESET CYCLE -----------

void CycleDependency::reset_cycle()
{
  finished = false;
  can_start = upstream_start.size() == 0;
  can_be_finished = upstream_finished.size() == 0;
  start_notifications = 0;
  finish_notifications = 0; 
  for (auto& [key, value] : upstream_start)
  {
    value = false;
  }
  for (auto& [key, value] : upstream_finished)
  {
    value = false;
  }
}


bool CycleDependency::operator==(const CycleDependency& other) const
{
  return id == other.id;
}
