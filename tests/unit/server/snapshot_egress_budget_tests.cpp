#include "game_server_error.hpp"
#include "server_limits.hpp"
#include "snapshot_egress_budget.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace server = blob_royale::server;

TEST_CASE("SnapshotEgressBudget bounds aggregate worst-case payload ownership",
          "[unit][server][egress][ownership]") {
  const auto now = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{now};
  std::vector<server::SnapshotEgressLease> leases;
  const std::size_t reservation_count =
      server::ServerLimits::kSnapshotEgressActiveMaximumByteCount /
      server::ServerLimits::kSnapshotEgressReservationByteCount;

  for (std::size_t reservation = 0; reservation < reservation_count; ++reservation) {
    std::optional<server::SnapshotEgressLease> lease = budget.try_reserve(reservation + 1, now);
    REQUIRE(lease.has_value());
    lease->commit_encoded_bytes(server::ServerLimits::kSnapshotEgressReservationByteCount);
    leases.push_back(std::move(*lease));
  }

  CHECK(budget.active_byte_count() == server::ServerLimits::kSnapshotEgressActiveMaximumByteCount);
  CHECK_FALSE(budget.try_reserve(reservation_count + 1, now).has_value());
  CHECK(budget.waiting_client_count() == 1);

  const auto one_reservation = std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(
      (server::ServerLimits::kSnapshotEgressReservationByteCount * 1'000'000'000ULL) /
      server::ServerLimits::kSnapshotEgressRefillByteCountPerSecond)};
  CHECK_FALSE(budget.try_reserve(reservation_count + 1, now + one_reservation).has_value());
  CHECK(budget.waiting_client_count() == 1);

  leases.front().release();
  CHECK(budget.try_reserve(reservation_count + 1, now + one_reservation).has_value());
  CHECK(budget.waiting_client_count() == 0);
}

TEST_CASE("SnapshotEgressBudget recovers deterministically at its documented byte rate",
          "[unit][server][egress][rate]") {
  const auto started_at = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{started_at};
  std::vector<server::SnapshotEgressLease> leases;
  const std::size_t reservation_count =
      server::ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount /
      server::ServerLimits::kSnapshotEgressReservationByteCount;
  for (std::size_t reservation = 0; reservation < reservation_count; ++reservation) {
    std::optional<server::SnapshotEgressLease> lease =
        budget.try_reserve(reservation + 1, started_at);
    REQUIRE(lease.has_value());
    lease->commit_encoded_bytes(server::ServerLimits::kSnapshotEgressReservationByteCount);
    leases.push_back(std::move(*lease));
  }
  for (server::SnapshotEgressLease& lease : leases) {
    lease.release();
  }

  const auto one_byte_short = std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(
      ((server::ServerLimits::kSnapshotEgressReservationByteCount - 1) * 1'000'000'000ULL) /
      server::ServerLimits::kSnapshotEgressRefillByteCountPerSecond)};
  CHECK_FALSE(budget.try_reserve(reservation_count + 1, started_at + one_byte_short).has_value());

  const auto one_reservation = std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(
      (server::ServerLimits::kSnapshotEgressReservationByteCount * 1'000'000'000ULL) /
      server::ServerLimits::kSnapshotEgressRefillByteCountPerSecond)};
  CHECK(budget.try_reserve(reservation_count + 1, started_at + one_reservation).has_value());
}

TEST_CASE("SnapshotEgressBudget refunds unused encoding reservation but charges actual bytes",
          "[unit][server][egress][rate]") {
  const auto now = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{now};
  const std::size_t encoded_byte_count = 1'024;
  const std::size_t initial_tokens = budget.available_token_byte_count();

  std::optional<server::SnapshotEgressLease> lease = budget.try_reserve(1, now);
  REQUIRE(lease.has_value());
  lease->commit_encoded_bytes(encoded_byte_count);

  CHECK(budget.active_byte_count() == encoded_byte_count);
  CHECK(budget.available_token_byte_count() == initial_tokens - encoded_byte_count);
  lease->release();
  CHECK(budget.active_byte_count() == 0);
  CHECK(budget.available_token_byte_count() == initial_tokens - encoded_byte_count);
}

TEST_CASE("SnapshotEgressBudget cancels an uncommitted encoding reservation exactly once",
          "[unit][server][egress][ownership]") {
  const auto now = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{now};
  const std::size_t initial_tokens = budget.available_token_byte_count();

  {
    std::optional<server::SnapshotEgressLease> lease = budget.try_reserve(1, now);
    REQUIRE(lease.has_value());
    CHECK_THROWS_AS(
        lease->commit_encoded_bytes(server::ServerLimits::kSnapshotEgressReservationByteCount + 1),
        server::GameServerError);
  }

  CHECK(budget.active_byte_count() == 0);
  CHECK(budget.available_token_byte_count() == initial_tokens);
  CHECK_THROWS_AS(budget.try_reserve(0, now), server::GameServerError);
}

TEST_CASE("SnapshotEgressBudget rotates saturated clients in fixed waiter order",
          "[unit][server][egress][fairness]") {
  const auto started_at = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{started_at};
  std::vector<server::SnapshotEgressLease> consumed;
  const std::size_t reservation_count =
      server::ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount /
      server::ServerLimits::kSnapshotEgressReservationByteCount;
  for (std::size_t reservation = 0; reservation < reservation_count; ++reservation) {
    std::optional<server::SnapshotEgressLease> lease =
        budget.try_reserve(reservation + 1, started_at);
    REQUIRE(lease.has_value());
    lease->commit_encoded_bytes(server::ServerLimits::kSnapshotEgressReservationByteCount);
    consumed.push_back(std::move(*lease));
  }
  for (server::SnapshotEgressLease& lease : consumed) {
    lease.release();
  }

  const server::SnapshotEgressBudget::ClientId first_waiter = 101;
  const server::SnapshotEgressBudget::ClientId second_waiter = 102;
  REQUIRE_FALSE(budget.try_reserve(first_waiter, started_at).has_value());
  REQUIRE_FALSE(budget.try_reserve(second_waiter, started_at).has_value());
  REQUIRE(budget.waiting_client_count() == 2);

  const auto one_reservation = std::chrono::nanoseconds{static_cast<std::chrono::nanoseconds::rep>(
      (server::ServerLimits::kSnapshotEgressReservationByteCount * 1'000'000'000ULL) /
      server::ServerLimits::kSnapshotEgressRefillByteCountPerSecond)};
  CHECK_FALSE(budget.try_reserve(1, started_at + one_reservation).has_value());
  std::optional<server::SnapshotEgressLease> first =
      budget.try_reserve(first_waiter, started_at + one_reservation);
  REQUIRE(first.has_value());
  first->commit_encoded_bytes(server::ServerLimits::kSnapshotEgressReservationByteCount);
  first->release();

  std::optional<server::SnapshotEgressLease> second =
      budget.try_reserve(second_waiter, started_at + (2 * one_reservation));
  REQUIRE(second.has_value());
  CHECK(budget.waiting_client_count() == 1);
}

TEST_CASE("SnapshotEgressBudget removes a closing queue head without waking a burst",
          "[unit][server][egress][fairness]") {
  const auto now = server::SnapshotEgressBudget::Clock::time_point{};
  server::SnapshotEgressBudget budget{now};
  std::vector<server::SnapshotEgressLease> leases;
  const std::size_t reservation_count =
      server::ServerLimits::kSnapshotEgressTokenBucketCapacityByteCount /
      server::ServerLimits::kSnapshotEgressReservationByteCount;
  for (std::size_t reservation = 0; reservation < reservation_count; ++reservation) {
    std::optional<server::SnapshotEgressLease> lease = budget.try_reserve(reservation + 1, now);
    REQUIRE(lease.has_value());
    leases.push_back(std::move(*lease));
  }
  REQUIRE_FALSE(budget.try_reserve(201, now).has_value());
  REQUIRE_FALSE(budget.try_reserve(202, now).has_value());

  budget.cancel_waiter(201);
  CHECK(budget.waiting_client_count() == 1);
  leases.front().release();
  CHECK_FALSE(budget.try_reserve(999, now).has_value());
  CHECK(budget.try_reserve(202, now).has_value());
}
