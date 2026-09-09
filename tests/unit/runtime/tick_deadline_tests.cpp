#include "tick_deadline.hpp"

#include "fixed_delta.hpp"
#include "runtime_limits.hpp"
#include "simulation_limits.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

// Literal time points: the policy is a function of three durations and a count, and nothing here
// reads a clock.
constexpr Clock::time_point kPrevious{1'000'000'000ns};
constexpr Clock::duration kQuantum = 2'500'000ns;

} // namespace

TEST_CASE("the catch-up bound is four quanta, which is ten milliseconds at the fixed cadence",
          "[unit][runtime][tick_deadline]") {
  // Written out because the reason in `runtime_limits.hpp` is stated in milliseconds: the burst a
  // late worker may run must stay well inside one 100 ms CFS throttle window.
  STATIC_REQUIRE(runtime::kMaximumCatchUpTicks == 4);
  STATIC_REQUIRE(simulation::FixedDelta::canonical().duration() == kQuantum);
  STATIC_REQUIRE(runtime::kMaximumCatchUpTicks * kQuantum == 10ms);
}

TEST_CASE("a tick that ends inside its quantum advances the deadline by one quantum",
          "[unit][runtime][tick_deadline]") {
  // Ended before its own deadline (woke early and finished early) or inside the quantum: the next
  // deadline is one quantum on, nothing is behind, and nothing is late.
  for (const Clock::time_point now : {kPrevious - 1ms, kPrevious, kPrevious + 1ms}) {
    const runtime::TickDeadlineAdvance advance =
        runtime::advance_tick_deadline(kPrevious, now, kQuantum, runtime::kMaximumCatchUpTicks);
    CHECK(advance.deadline == kPrevious + kQuantum);
    CHECK_FALSE(advance.rebased);
    CHECK(advance.ticks_behind == 0);
    CHECK(advance.lateness == Clock::duration::zero());
  }
}

TEST_CASE("a tick that ends after the next was due is an overrun the worker catches up",
          "[unit][runtime][tick_deadline]") {
  // Ended half a quantum after the next tick was due: the deadline still advances by exactly one
  // quantum -- into the past, so the wait returns at once -- and the lateness is the half quantum.
  const Clock::time_point now = kPrevious + kQuantum + kQuantum / 2;
  const runtime::TickDeadlineAdvance advance =
      runtime::advance_tick_deadline(kPrevious, now, kQuantum, runtime::kMaximumCatchUpTicks);
  CHECK(advance.deadline == kPrevious + kQuantum);
  CHECK_FALSE(advance.rebased);
  CHECK(advance.ticks_behind == 1);
  CHECK(advance.lateness == kQuantum / 2);
}

TEST_CASE("the worker catches up through the bound and is re-based one quantum past it",
          "[unit][runtime][tick_deadline]") {
  // Exactly four quanta behind (and a hair): still a catch-up. Five quanta behind: re-based to now,
  // and the ticks it fell behind are reported so the control loop can say how far the clock moved.
  const Clock::time_point at_bound = kPrevious + runtime::kMaximumCatchUpTicks * kQuantum + 1us;
  const runtime::TickDeadlineAdvance caught_up =
      runtime::advance_tick_deadline(kPrevious, at_bound, kQuantum, runtime::kMaximumCatchUpTicks);
  CHECK(caught_up.deadline == kPrevious + kQuantum);
  CHECK_FALSE(caught_up.rebased);
  CHECK(caught_up.ticks_behind == runtime::kMaximumCatchUpTicks);
  CHECK(caught_up.lateness == (runtime::kMaximumCatchUpTicks - 1) * kQuantum + 1us);

  const Clock::time_point past_bound = kPrevious + (runtime::kMaximumCatchUpTicks + 1) * kQuantum;
  const runtime::TickDeadlineAdvance rebased = runtime::advance_tick_deadline(
      kPrevious, past_bound, kQuantum, runtime::kMaximumCatchUpTicks);
  CHECK(rebased.deadline == past_bound);
  CHECK(rebased.rebased);
  CHECK(rebased.ticks_behind == runtime::kMaximumCatchUpTicks + 1);
  CHECK(rebased.lateness == runtime::kMaximumCatchUpTicks * kQuantum);

  // A re-based deadline is never earlier than the advanced one would have been: the clock moves
  // later, never back, so no tick is ever scheduled before the one it follows.
  CHECK(rebased.deadline > kPrevious + kQuantum);
}

TEST_CASE("a long stall is one re-base whose deadline is now, however long the stall was",
          "[unit][runtime][tick_deadline]") {
  // A throttled 100 ms under the deployed CFS quota: forty quanta behind. Unbounded, that would be
  // forty back-to-back ticks; bounded, it is one tick now and a clock forty quanta behind.
  const Clock::time_point after_stall = kPrevious + 100ms;
  const runtime::TickDeadlineAdvance advance = runtime::advance_tick_deadline(
      kPrevious, after_stall, kQuantum, runtime::kMaximumCatchUpTicks);
  CHECK(advance.rebased);
  CHECK(advance.deadline == after_stall);
  CHECK(advance.ticks_behind == 40);
  CHECK(advance.lateness == 100ms - kQuantum);
}
