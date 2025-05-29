#include <cassert>
#include <future>

#include "boost-log.hpp"

#include "cycle_dependency.hpp"


int id_cycle_dependency_counter = 0;

int external_dependency_counter = 0;

CycleDependency::CycleDependency(std::string name_) :
  name(name_),
  id(id_cycle_dependency_counter++)
{}

bool CycleDependency::ready_to_begin()
{
  ENTRANCE << *this << " ready_to_begin()";
  return can_start;
}


void CycleDependency::print_dependency_relations()
{
  ENTRANCE << *this << " dependency relations:";
  for (auto& [dep, val] : upstream_start)
  {
    TRACE << "start waits on: " << *dep;
  }
  for (auto& [dep, val] : upstream_finished)
  {
    TRACE << "finished waits on: " << *dep;
  }

  for (auto dep : downstream_start)
  {
    TRACE << "enables start for: " << *dep;
  }
  for (auto dep : downstream_finished)
  {
    TRACE << "enables finish for: " << *dep;
  }
}


void CycleDependency::add_start_dependencies(std::vector<CycleDependency*> upstream)
{
  ENTRANCE << *this << " add_start_dependency() ";
  for (auto up : upstream)
  {
    upstream_start[up] = false;
    up->downstream_start.push_back(this);
  }
}


void CycleDependency::add_finish_dependencies(std::vector<CycleDependency*> upstream)
{
  ENTRANCE << *this << " add_finish_dependency() ";
  for (auto up : upstream)
  {
    TRACE << "adding " << *up;
    upstream_finished[up] = false;
    up->downstream_finished.push_back(this);
  }
}

int CycleDependency::register_external_start_dependency()
{
  external_start[external_dependency_counter] = false;
  return external_dependency_counter++;
}



bool CycleDependency::check_can_start()
{
  // ERROR HERE: external_notifications should be == external_start.size()
  //    - currently set up this way to make sure faster time clocks do not lock up the queues
  //
  return can_start = (
      start_notifications == upstream_start.size() &&
      external_notifications >= external_start.size()
      );
}



// THIS is the one being notified
void CycleDependency::notify_can_start(CycleDependency* dep)
{
  ENTRANCE << *this << " notify_can_start(" << *dep << ")";
  std::unique_lock lock(dependency_lock);

  handle_notification(upstream_start, dep, start_notifications);

  if (check_can_start())
  {
    LOCK << *this << " can start";
    worker_cv.notify_one();
  }
  else
  {
    LOCK << *this << " cannot start";
  }
}


void CycleDependency::external_notify_can_start(int dep)
{
  ENTRANCE << *this << " external_notify_can_start(" << dep << ")";
  std::unique_lock lock(dependency_lock);

  handle_notification(external_start, dep, external_notifications);

  if (check_can_start())
  {
    LOCK << *this << " can start";
    worker_cv.notify_one();
  }
  else
  {
    LOCK << *this << " cannot start";
  }
}


// THIS is hte one being notified
void CycleDependency::notify_can_be_finished(CycleDependency* dep)
{
  ENTRANCE << *this << " notify_can_be_finished(" << *dep << ")";

  {
    std::unique_lock lock(dependency_lock);
  
    handle_notification(upstream_finished, dep, finish_notifications);
  
    if (finish_notifications == upstream_finished.size())
    {
      can_be_finished = true;
      LOCK << *this << " can be finish";
    }
    else
    {
      LOCK << *this << " cannot finish";
    }
  }
  test_finished(true);
}


void CycleDependency::test_finished(bool external_call)
{
  ENTRANCE << *this << " test_finished()";
  if (!unsafe_last_one_done()) return;
  
  std::unique_lock lock(dependency_lock);
  TRACE << *this << " attempting finished test";
  
  if (!last_one_done() || can_start == false)
  {
    TRACE << *this << " not finished";
    return;
  }
  if ((external_call  && waiting_workers == 0 ) ||
      (!external_call && waiting_workers == 1))
  {
    LOCK << *this << " is finished";
  
    notify_dependents();
    reset_cycle();
  }
  else
  {
    TRACE << *this << " not finished. waiting_workers: " << waiting_workers;
  }
}

void CycleDependency::notify_dependents()
{
  for (auto dep : downstream_start)
  {
    dep->notify_can_start(this);
  }
  for (auto dep : downstream_finished)
  {
    TRACE << "notifying " << *dep;
    dep->notify_can_be_finished(this);
  }
}

void CycleDependency::worker_running()
{
  std::unique_lock(worker_waiting_lock);
  waiting_workers--;
}

void CycleDependency::worker_waiting()
{
  std::unique_lock(worker_waiting_lock);
  waiting_workers++;
}

unsigned int CycleDependency::get_waiting_workers()
{
  std::unique_lock(worker_waiting_lock);
  return waiting_workers;
}


// ------------ RESET CYCLE -----------

void CycleDependency::reset_cycle()
{
  ENTRANCE << *this << " reset_cycle()";

  TRACE << *this << " acquired lock for reset_cycle()";
  can_start = upstream_start.size() == 0 && external_start.size() == 0;
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
  for (auto& [key, value] : external_start)
  {
    value = false;
  }
}


bool CycleDependency::operator==(const CycleDependency& other) const
{
  return id == other.id;
}

std::string CycleDependency::print_comprehensive() const
{
  return name + " " + container_info();
}

std::ostream& operator<<(std::ostream& os, const CycleDependency& dep)
{
  return os << dep.name;
}

