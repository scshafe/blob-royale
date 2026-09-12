#ifndef BLOB_ROYALE_TESTING_TICK_WINDOW_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_TICK_WINDOW_FIXTURE_HPP

#include "commands/thrust_command.hpp"
#include "controller_id.hpp"
#include "tick_window.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

namespace blob_royale::testing::tick_window_fixture {

inline constexpr std::uint64_t kActivation = 7;
inline constexpr std::uint64_t kDuration = 3;
inline constexpr std::uint64_t kExpiry = 10;
inline constexpr std::uint64_t kEntity = 4;
inline constexpr std::uint64_t kController = 9;
inline constexpr std::uint64_t kOneTick = 1;
inline constexpr std::uint64_t kEmptyDuration = 0;

struct WindowCase final {
  std::uint64_t activation;
  std::uint64_t duration;
};
inline constexpr std::array kOverflowCases{
    WindowCase{simulation::TickSequence::kMaximumValue, 1},
    WindowCase{simulation::TickSequence::kMaximumValue - 1, 2},
    WindowCase{1, std::numeric_limits<std::uint64_t>::max()}};
inline constexpr std::array kExactMaximumCases{
    WindowCase{simulation::TickSequence::kMaximumValue, 0},
    WindowCase{simulation::TickSequence::kMaximumValue - 1, 1},
    WindowCase{0, simulation::TickSequence::kMaximumValue}};

[[nodiscard]] inline simulation::TickSequence tick(const std::uint64_t value = kActivation) {
  return simulation::TickSequence::create(value);
}
[[nodiscard]] inline simulation::TickWindow window() {
  return simulation::TickWindow::create(tick(), kDuration);
}
[[nodiscard]] inline simulation::Vector2 direction() {
  return simulation::Vector2::create(0.25, -0.75);
}
[[nodiscard]] inline simulation::ThrustCommand
command(std::optional<simulation::TickSequence> generation = {}) {
  return {simulation::EntityId::create(kEntity), direction(), generation};
}

} // namespace blob_royale::testing::tick_window_fixture

#endif
