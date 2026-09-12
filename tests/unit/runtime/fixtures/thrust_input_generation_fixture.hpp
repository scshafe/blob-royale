#ifndef BLOB_ROYALE_TESTS_UNIT_RUNTIME_FIXTURES_THRUST_INPUT_GENERATION_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_UNIT_RUNTIME_FIXTURES_THRUST_INPUT_GENERATION_FIXTURE_HPP

#include "command_registry.hpp"

#include <array>
#include <optional>
#include <string_view>

namespace blob_royale::runtime::thrust_input_fixture {

inline constexpr std::array<std::uint64_t, 3> kAcceptedGenerations{
    1, 100, simulation::TickSequence::kMaximumValue};
inline constexpr std::array<std::string_view, 2> kControllerKinds{"session", "wanderer"};

[[nodiscard]] inline simulation::Command
command(const std::uint64_t entity, const std::optional<simulation::TickSequence> generation,
        const bool release = false) {
  return simulation::ThrustCommand{simulation::EntityId::create(entity),
                                   simulation::Vector2::create(release ? 0.0 : 1.0, 0.0),
                                   generation};
}

} // namespace blob_royale::runtime::thrust_input_fixture

#endif
