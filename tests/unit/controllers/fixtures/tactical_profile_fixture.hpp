#ifndef BLOB_ROYALE_TESTING_TACTICAL_PROFILE_FIXTURE_HPP
#define BLOB_ROYALE_TESTING_TACTICAL_PROFILE_FIXTURE_HPP

#include "tactical_profile.hpp"
#include "tactical_profile_catalogue.hpp"
#include "tactical_seed_identity.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace blob_royale::testing::tactical_profile_fixture {

inline constexpr std::string_view kName = "steady";

// Equal POSITIVE weights, which is how a profile authors "no preference". All-zero weights are a
// named rejection rather than a neutral setting, and a *short* list is no longer a silent zero
// here: `AuthoredObjectiveWeight` has no default constructor, so a braced list one member behind
// the kind enum fails to compile at this line rather than handing every controllers case a bot
// that ignores one objective (`controllers/tactical_profile.hpp`). Equal weights keep selection
// ordered by distance, which is what every case written before Step 22a expects.
inline constexpr controllers::TacticalObjectiveWeights kEqualWeights{1.0, 1.0, 1.0, 1.0, 1.0};

// The two combat settings both sections below author, named so a case can assert them rather than
// re-spell them. **They are positive on purpose.** Zero is a legal authored answer for each -- a
// screen that examines nothing, a profile that never anticipates -- and both are plain scalars of
// `Section`, so a fixture left behind by one of them would compile, load, validate, and quietly
// disable the behaviour under test. `tactical_profile_tests.cpp` asserts both are positive for
// exactly that reason; the weights above need no such assertion because the compiler makes one.
inline constexpr double kChargeScreenDiagonalFraction = 0.5;
inline constexpr std::uint64_t kShieldAnticipationTicks = 8;

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
  return {std::string{kName},
          1.0,
          80,
          0.05,
          400,
          kEqualWeights,
          0.5,
          80,
          kChargeScreenDiagonalFraction,
          kShieldAnticipationTicks};
}
[[nodiscard]] inline controllers::TacticalProfile::Section immediate_section() {
  return {
      std::string{kName},      1.0, 0, 0.0, 5, kEqualWeights, 0.5, 0, kChargeScreenDiagonalFraction,
      kShieldAnticipationTicks};
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
