#include "game_server_error.hpp"
#include "server_test_fixture.hpp"
#include "snapshot_delivery_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>

namespace server = blob_royale::server;
namespace fixture = blob_royale::server::test_fixture;
namespace simulation = blob_royale::simulation;

namespace {

[[nodiscard]] std::shared_ptr<const simulation::WorldSnapshot> snapshot(const std::size_t tick) {
  return std::make_shared<const simulation::WorldSnapshot>(fixture::snapshot_after_steps(tick));
}

} // namespace

TEST_CASE("SnapshotDeliveryState starts one write and suppresses its duplicate",
          "[unit][server][snapshot-delivery]") {
  server::SnapshotDeliveryState state;
  const auto tick_one = snapshot(1);

  state.observe(tick_one);
  const auto first = state.begin_active_write();
  REQUIRE(first.has_value());
  CHECK(first->snapshot == tick_one);
  CHECK(first->message_sequence == 1);
  CHECK(state.write_active());
  state.observe(tick_one);
  CHECK_FALSE(state.begin_active_write().has_value());

  state.complete_active_write();
  state.observe(tick_one);
  CHECK_FALSE(state.begin_active_write().has_value());
  CHECK(state.last_delivered_tick() == 1);
}

TEST_CASE("SnapshotDeliveryState retains only the newest pending snapshot",
          "[unit][server][snapshot-delivery]") {
  server::SnapshotDeliveryState state;
  state.observe(snapshot(1));
  REQUIRE(state.begin_active_write().has_value());

  state.observe(snapshot(2));
  CHECK(state.has_pending_snapshot());
  state.observe(snapshot(3));
  state.observe(snapshot(2));

  state.complete_active_write();
  state.observe(snapshot(2));
  const auto next_slot = state.begin_active_write();
  REQUIRE(next_slot.has_value());
  CHECK(next_slot->snapshot->tick_sequence().value() == 3);
  CHECK(next_slot->message_sequence == 2);
}

TEST_CASE("SnapshotDeliveryState never bursts pending data on write completion",
          "[unit][server][snapshot-delivery]") {
  server::SnapshotDeliveryState state;
  state.observe(snapshot(1));
  REQUIRE(state.begin_active_write().has_value());
  state.observe(snapshot(2));

  state.complete_active_write();

  CHECK_FALSE(state.write_active());
  CHECK(state.has_pending_snapshot());
  CHECK(state.delivered_message_count() == 1);
}

TEST_CASE("SnapshotDeliveryState discards pending state during coordinated close",
          "[unit][server][snapshot-delivery]") {
  server::SnapshotDeliveryState state;
  state.observe(snapshot(1));
  REQUIRE(state.begin_active_write().has_value());
  state.observe(snapshot(2));

  state.discard_pending();

  CHECK(state.write_active());
  CHECK_FALSE(state.has_pending_snapshot());
}

TEST_CASE("SnapshotDeliveryState rejects a null publication reference",
          "[unit][server][snapshot-delivery]") {
  server::SnapshotDeliveryState state;
  CHECK_THROWS_AS(state.observe(nullptr), server::GameServerError);
}

TEST_CASE("SnapshotDeliveryState coalesces denied work without consuming a sequence",
          "[unit][server][snapshot-delivery][egress]") {
  server::SnapshotDeliveryState state;

  state.observe(snapshot(1));
  state.observe(snapshot(2));
  state.observe(snapshot(3));

  CHECK_FALSE(state.write_active());
  CHECK(state.has_pending_snapshot());
  CHECK(state.delivered_message_count() == 0);

  const auto admitted = state.begin_active_write();
  REQUIRE(admitted.has_value());
  CHECK(admitted->snapshot->tick_sequence().value() == 3);
  CHECK(admitted->message_sequence == 1);
}
