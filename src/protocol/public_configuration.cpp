#include "public_configuration.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"

#include <cmath>
#include <string_view>

namespace blob_royale::protocol {
namespace {

void require_public_world_scalar(const double value, const std::string_view context) {
  if (!std::isfinite(value) || value <= 0.0 || value > kMaximumPublicWorldDimension) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kPublicConfigurationInvalid,
                                std::string{context},
                                "value must be finite, greater than zero, and at most 1000000000"};
  }
}

} // namespace

PublicConfiguration PublicConfiguration::create(const double world_width, const double world_height,
                                                const double player_radius,
                                                const std::uint64_t snapshots_per_second) {
  require_public_world_scalar(world_width, "configuration.world.width_world_units");
  require_public_world_scalar(world_height, "configuration.world.height_world_units");
  require_public_world_scalar(player_radius, "configuration.world.player_radius_world_units");

  if (world_width <= 2.0 * player_radius || world_height <= 2.0 * player_radius) {
    throw ProtocolEncodingError{
        ProtocolEncodingErrorCode::kPublicConfigurationInvalid, "configuration.world",
        "width and height must each be greater than twice the player radius"};
  }
  if (snapshots_per_second < kMinimumSnapshotsPerSecond ||
      snapshots_per_second > kMaximumSnapshotsPerSecond) {
    throw ProtocolEncodingError{ProtocolEncodingErrorCode::kPublicConfigurationInvalid,
                                "configuration.presentation.snapshots_per_second",
                                "value must be between 1 and 60"};
  }

  return PublicConfiguration{world_width, world_height, player_radius, snapshots_per_second};
}

PublicConfiguration::PublicConfiguration(const double world_width, const double world_height,
                                         const double player_radius,
                                         const std::uint64_t snapshots_per_second) noexcept
    : world_width_(world_width), world_height_(world_height), player_radius_(player_radius),
      snapshots_per_second_(snapshots_per_second) {}

} // namespace blob_royale::protocol
