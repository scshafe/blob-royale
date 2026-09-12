#ifndef BLOB_ROYALE_TESTING_TACTICAL_PROFILE_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_TACTICAL_PROFILE_FIXTURE_HPP

#include "tactical_profile.hpp"
#include "tactical_profile_catalogue.hpp"
#include "tactical_seed_identity.hpp"

#include <array>
#include <limits>
#include <string>
#include <vector>

namespace blob_royale::testing::tactical_profile_fixture {

inline constexpr std::string_view kName = "steady";
inline constexpr std::string_view kOtherName = "quick";
inline constexpr controllers::TacticalSeedIdentity kIdentity{20260911, 2, 3};
inline constexpr std::array<std::string_view, 6> kInvalidNames{"",    "Upper", "space name",
                                                               "a-b", "1name", "_name"};
inline constexpr std::array<double, 5> kInvalidProbabilities{
    -0.001, 1.001, std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};
inline constexpr std::array<double, 5> kInvalidAimErrors{
    -0.001, 0.251, std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()};

[[nodiscard]] inline controllers::TacticalProfile::Section configured_section() {
  return {std::string{kName}, 1.0, 80, 0.05, 400};
}
[[nodiscard]] inline controllers::TacticalProfile::Section immediate_section() {
  return {std::string{kName}, 1.0, 0, 0.0, 5};
}
[[nodiscard]] inline controllers::TacticalProfile profile() {
  return controllers::TacticalProfile::create(immediate_section());
}
[[nodiscard]] inline std::vector<controllers::TacticalProfile> profiles(const std::size_t count) {
  std::vector<controllers::TacticalProfile> result;
  for (std::size_t index = 0; index < count; ++index) {
    auto section = immediate_section();
    section.profile_name = "profile_" + std::to_string(index);
    result.push_back(controllers::TacticalProfile::create(section));
  }
  return result;
}

// Independent constants and operation order, not production mixing or controller allocation.
[[nodiscard]] inline std::uint64_t frozen_mix(std::uint64_t bits) noexcept {
  bits = (bits ^ (bits >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
  bits = (bits ^ (bits >> 27U)) * 0x94D0'49BB'1331'11EBULL;
  return bits ^ (bits >> 31U);
}
[[nodiscard]] inline std::uint64_t frozen_seed(const controllers::TacticalSeedIdentity identity,
                                               const std::string_view name,
                                               const std::uint64_t running_tick) noexcept {
  auto bits = frozen_mix(0x7461'6374'6963'616cULL ^ identity.match_seed);
  bits = frozen_mix(bits ^ identity.lobby_id);
  bits = frozen_mix(bits ^ identity.seat_index);
  bits = frozen_mix(bits ^ static_cast<std::uint64_t>(name.size()));
  for (const char character : name) {
    bits = frozen_mix(bits ^ static_cast<unsigned char>(character));
  }
  return frozen_mix(bits ^ running_tick);
}

} // namespace blob_royale::testing::tactical_profile_fixture

#endif
