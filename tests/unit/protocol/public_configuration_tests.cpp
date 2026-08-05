#include "protocol_test_fixture.hpp"

#include "protocol_constants.hpp"
#include "protocol_encoding_error.hpp"
#include "public_configuration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <limits>

namespace protocol = blob_royale::protocol;
namespace fixture = blob_royale::protocol::test_fixture;

TEST_CASE("PublicConfiguration retains all validated presentation-facing values",
          "[unit][protocol][configuration]") {
  const protocol::PublicConfiguration configuration = fixture::golden_configuration();

  CHECK(configuration.world_width() == 960.0);
  CHECK(configuration.world_height() == 640.0);
  CHECK(configuration.player_radius() == 10.0);
  CHECK(configuration.snapshots_per_second() == 30);
}

TEST_CASE("PublicConfiguration accepts both presentation cadence boundaries",
          "[unit][protocol][configuration]") {
  const protocol::PublicConfiguration minimum =
      protocol::PublicConfiguration::create(3.0, 3.0, 1.0, protocol::kMinimumSnapshotsPerSecond);
  const protocol::PublicConfiguration maximum = protocol::PublicConfiguration::create(
      protocol::kMaximumPublicWorldDimension, protocol::kMaximumPublicWorldDimension, 499'999'999.0,
      protocol::kMaximumSnapshotsPerSecond);

  CHECK(minimum.snapshots_per_second() == protocol::kMinimumSnapshotsPerSecond);
  CHECK(maximum.snapshots_per_second() == protocol::kMaximumSnapshotsPerSecond);
}

TEST_CASE("PublicConfiguration rejects non-finite world scalars",
          "[unit][protocol][configuration]") {
  for (const double non_finite :
       {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::quiet_NaN()}) {
    fixture::require_protocol_error_code(
        [non_finite] {
          static_cast<void>(protocol::PublicConfiguration::create(non_finite, 10.0, 1.0, 30));
        },
        protocol::ProtocolEncodingErrorCode::kPublicConfigurationInvalid);
  }
}

TEST_CASE("PublicConfiguration rejects out-of-schema world scalar bounds",
          "[unit][protocol][configuration]") {
  for (const double invalid_width : {0.0, -1.0, protocol::kMaximumPublicWorldDimension + 1.0}) {
    fixture::require_protocol_error_code(
        [invalid_width] {
          static_cast<void>(protocol::PublicConfiguration::create(invalid_width, 10.0, 1.0, 30));
        },
        protocol::ProtocolEncodingErrorCode::kPublicConfigurationInvalid);
  }
}

TEST_CASE("PublicConfiguration rejects worlds that cannot contain the player disc",
          "[unit][protocol][configuration]") {
  fixture::require_protocol_error_code(
      [] { static_cast<void>(protocol::PublicConfiguration::create(20.0, 21.0, 10.0, 30)); },
      protocol::ProtocolEncodingErrorCode::kPublicConfigurationInvalid);
}

TEST_CASE("PublicConfiguration rejects presentation cadence outside one through sixty",
          "[unit][protocol][configuration]") {
  for (const std::uint64_t invalid_cadence : {0ULL, 61ULL}) {
    fixture::require_protocol_error_code(
        [invalid_cadence] {
          static_cast<void>(
              protocol::PublicConfiguration::create(100.0, 100.0, 10.0, invalid_cadence));
        },
        protocol::ProtocolEncodingErrorCode::kPublicConfigurationInvalid);
  }
}
