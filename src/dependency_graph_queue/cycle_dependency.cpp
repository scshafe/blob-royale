#include "cycle_dependency.hpp"


bool CycleDependency::ready_to_begin()
{
  return can_start;
}



void CycleDependency::add_can_start_dependency(CycleDependency* upstream)
{
  upstream_start[upstream->id] = false;
  upstream->downstream_start.push_back(this);
}


void CycleDependency::add_can_be_finished_dependency(CycleDependency* upstream)
{
  upstream_finished[upstream->id] = false;
  upstream->downstream_finished.push_back(this);
}




// THIS is the one being notified
void CycleDependency::notify_can_start(int i)
{
  m.lock();
  if (upstream_start.at(i) == false)
  {
    upstream_start.at(i) = true;
    start_notifications++;
    if (start_notifications == upstream_start.size())
    {
      can_start = true;
    }
  }
  else
  {
    assert(false && "notify_can_start() received repeat call from the same queue");
  }
  m.unlock();
}

// THIS is hte one being notified
void CycleDependency::notify_can_be_finished(int i)
{
  m.lock();
  if (upstream_finished.at(i) == false)
  {
    upstream_finished.at(i) = true;
    finish_notifications++;
    if (finish_notifications == upstream_finished.size())
    {
      can_be_finished = true;
    }
  }
  else
  {
    assert(false && "notify_can_be_finished() received repeat call from the same queue");
  m.unlock();
}

void CycleDependency::test_finished()
{
  m.lock();
  if (can_be_finished == true && last_one_done())
  {
    finished = true;
    for (auto dependency : downstream_can_start)
    {
      dependency->notify_can_start(id);
    }
    for (auto dependency : downstream_can_be_finished)
    {
      dependency->notify_can_be_finished(id);
    }
  }
  m.unlock();
}


// ------------ RESET CYCLE -----------

void CycleDependency::reset_cycle()
{
  can_start = wait_to_start.size() == 0;
  can_be_finished = wait_to_can_be_finished.size() == 0;
  start_notifications = 0;
  finish_notifications = 0; 
}




