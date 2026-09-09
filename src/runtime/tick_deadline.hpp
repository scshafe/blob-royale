#ifndef BLOB_ROYALE_RUNTIME_TICK_DEADLINE_HPP
#define BLOB_ROYALE_RUNTIME_TICK_DEADLINE_HPP

#include <chrono>
#include <cstdint>

namespace blob_royale::runtime {

// canonical: tick_deadline -- when the next tick is due, and whether the clock was re-based.
//
// **A stalled worker catches up a little and then stops trying.** The deadline of tick N+1 is the
// deadline of tick N plus one quantum, which is what makes the cadence a fixed 400 Hz rather than
// "as fast as the last tick allowed". When the worker is late -- a step overran, or the thread was
// descheduled -- the next deadline is already in the past, the wait returns at once, and the worker
// ticks back-to-back until it has caught up. Unbounded, that is exactly the burst shape a CFS quota
// throttles: a throttled 100 ms became 40 consecutive ticks, which was the burst throttled next,
// and with N rooms it would be N synchronised bursts
// (`docs/reviews/2026-09-08-lobby-and-hazard-review.md`, P1). So the catch-up is bounded: once the
// worker is more than `kMaximumCatchUpTicks` quanta behind, the deadline is re-based to now and the
// room runs in slow motion for a moment instead of sprinting.
//
// **Tick numbers never skip.** A re-base moves the wall-clock deadline, not the tick sequence:
// every tick is still committed, in order, with the fixed delta, and determinism is per tick
// sequence rather than per wall clock. What a re-base costs is that the room's clock now runs the
// quanta it fell behind later than wall time, which `TickStatistics` records and the control loop
// logs.
//
// A pure function of its arguments, so the policy is testable with literal time points and the
// worker loop -- which holds a mutex and a stop token -- stays a caller of it.
// related: simulation_runtime.hpp -- the worker that calls this once per tick.
// related: runtime_limits.hpp -- `kMaximumCatchUpTicks` and its reason.
struct TickDeadlineAdvance final {
  // When the next tick is due.
  std::chrono::steady_clock::time_point deadline;
  // Whether `deadline` was re-based to `now` rather than advanced by one quantum.
  bool rebased;
  // Whole quanta `now` lay past the previous deadline: zero for a tick that ended inside its own
  // quantum.
  std::uint64_t ticks_behind;
  // How far `now` lay past the moment the next tick was already due, or zero. Positive is an
  // overrun, whether the step was slow or the thread woke late.
  std::chrono::steady_clock::duration lateness;

  friend bool operator==(const TickDeadlineAdvance&, const TickDeadlineAdvance&) = default;
};

// The next deadline after a tick whose own deadline was `previous_deadline` and which ended at
// `now`. Behind by at most `maximum_catch_up_ticks` quanta: advance by one quantum and let the
// worker catch up. Behind by more: re-base to `now`. `quantum` must be positive.
[[nodiscard]] constexpr TickDeadlineAdvance
advance_tick_deadline(const std::chrono::steady_clock::time_point previous_deadline,
                      const std::chrono::steady_clock::time_point now,
                      const std::chrono::steady_clock::duration quantum,
                      const std::uint64_t maximum_catch_up_ticks) noexcept {
  const std::chrono::steady_clock::time_point advanced = previous_deadline + quantum;
  const std::chrono::steady_clock::duration lateness =
      now > advanced ? now - advanced : std::chrono::steady_clock::duration::zero();
  if (now <= previous_deadline) {
    return {.deadline = advanced, .rebased = false, .ticks_behind = 0, .lateness = lateness};
  }
  const auto behind = static_cast<std::uint64_t>((now - previous_deadline) / quantum);
  if (behind <= maximum_catch_up_ticks) {
    return {.deadline = advanced, .rebased = false, .ticks_behind = behind, .lateness = lateness};
  }
  return {.deadline = now, .rebased = true, .ticks_behind = behind, .lateness = lateness};
}

} // namespace blob_royale::runtime

#endif
