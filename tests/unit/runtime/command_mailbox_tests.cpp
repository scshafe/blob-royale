#include "command_kind_mask.hpp"
#include "command_mailbox.hpp"
#include "command_registry.hpp"
#include "command_submission_result.hpp"
#include "controller_id.hpp"
#include "entity_id.hpp"
#include "runtime_limits.hpp"
#include "vector2.hpp"

#include <catch2/catch_test_macros.hpp>

#include <barrier>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace runtime = blob_royale::runtime;
namespace simulation = blob_royale::simulation;

namespace {

constexpr std::uint64_t kFirstEntityId = 1;
constexpr std::uint64_t kSecondEntityId = 2;
constexpr std::uint64_t kFirstControllerId = 1;
constexpr std::size_t kConcurrentSubmitterCount = 6;
constexpr std::size_t kSubmissionsPerSubmitter = 64;

[[nodiscard]] simulation::Command thrust_fixture(const std::uint64_t entity_id, const double x,
                                                 const double y) {
  return simulation::ThrustCommand{.entity = simulation::EntityId::create(entity_id),
                                   .direction = simulation::Vector2::create(x, y)};
}

[[nodiscard]] simulation::Command despawn_fixture(const std::uint64_t entity_id) {
  return simulation::DespawnCommand{.entity = simulation::EntityId::create(entity_id)};
}

[[nodiscard]] simulation::Command spawn_fixture(const std::uint64_t controller_id) {
  return simulation::SpawnCommand{.controller = simulation::ControllerId::create(controller_id)};
}

// Fills the mailbox to capacity with distinct thrusts, which is the only shape that reaches the
// bound without superseding: one slot per commanded entity.
void fill_with_distinct_thrusts(runtime::CommandMailbox& mailbox, const std::size_t count) {
  for (std::size_t index = 0; index < count; ++index) {
    REQUIRE(mailbox.submit(thrust_fixture(static_cast<std::uint64_t>(index) + 1, 0.0, 0.0)) ==
            runtime::CommandSubmissionResult::kAccepted);
  }
}

} // namespace

TEST_CASE("CommandMailbox classifies spawn and despawn as entity lifecycle commands",
          "[unit][runtime][mailbox]") {
  REQUIRE(runtime::is_entity_lifecycle_command(simulation::CommandKind::kSpawn));
  REQUIRE(runtime::is_entity_lifecycle_command(simulation::CommandKind::kDespawn));
  REQUIRE_FALSE(runtime::is_entity_lifecycle_command(simulation::CommandKind::kThrust));
}

TEST_CASE("CommandMailbox accepts a command of an accepted kind and hands it to one drain",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());

  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(mailbox.statistics().pending_command_count == 1);

  const std::vector<simulation::Command> drained = mailbox.drain();

  REQUIRE(drained.size() == 1);
  REQUIRE(drained[0] == thrust_fixture(kFirstEntityId, 1.0, 0.0));
  REQUIRE(mailbox.statistics().pending_command_count == 0);
  REQUIRE(mailbox.statistics().drained_command_count == 1);
}

TEST_CASE("CommandMailbox rejects an unaccepted kind and counts the rejection",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(
      simulation::CommandKindMask::create({simulation::CommandKind::kThrust}));

  REQUIRE(mailbox.submit(spawn_fixture(kFirstControllerId)) ==
          runtime::CommandSubmissionResult::kRejectedUnacceptedKind);

  const runtime::CommandMailbox::Statistics statistics = mailbox.statistics();
  REQUIRE(statistics.rejected_unaccepted_kind_count == 1);
  REQUIRE(statistics.pending_command_count == 0);
  REQUIRE(statistics.accepted_command_count == 0);
}

TEST_CASE("CommandMailbox supersedes a pending command of the same kind for the same entity",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());

  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 0.0, 1.0)) ==
          runtime::CommandSubmissionResult::kSuperseded);

  const std::vector<simulation::Command> drained = mailbox.drain();

  // Lossless: InputBatch::create keeps the last command of a kind per identity, so this is the
  // batch an appending mailbox would have produced.
  REQUIRE(drained.size() == 1);
  REQUIRE(drained[0] == thrust_fixture(kFirstEntityId, 0.0, 1.0));
  REQUIRE(mailbox.statistics().superseded_command_count == 1);
  REQUIRE(mailbox.statistics().dropped_command_count == 0);
}

TEST_CASE("CommandMailbox keeps one slot per entity for one kind", "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());

  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(mailbox.submit(thrust_fixture(kSecondEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);

  REQUIRE(mailbox.drain().size() == 2);
}

TEST_CASE("CommandMailbox keeps one slot per kind for one entity", "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());

  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);
  REQUIRE(mailbox.submit(despawn_fixture(kFirstEntityId)) ==
          runtime::CommandSubmissionResult::kAccepted);

  REQUIRE(mailbox.drain().size() == 2);
}

TEST_CASE("CommandMailbox drains each command exactly once", "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kAccepted);

  REQUIRE(mailbox.drain().size() == 1);
  REQUIRE(mailbox.drain().empty());

  const runtime::CommandMailbox::Statistics statistics = mailbox.statistics();
  REQUIRE(statistics.drain_count == 2);
  REQUIRE(statistics.drained_command_count == 1);
}

TEST_CASE("CommandMailbox drops an incoming thrust when full and counts the drop",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  fill_with_distinct_thrusts(mailbox, runtime::kMaximumMailboxCommandCount);

  const std::uint64_t overflowing_entity_id = runtime::kMaximumMailboxCommandCount + 1;
  REQUIRE(mailbox.submit(thrust_fixture(overflowing_entity_id, 1.0, 0.0)) ==
          runtime::CommandSubmissionResult::kDroppedMailboxFull);

  const runtime::CommandMailbox::Statistics statistics = mailbox.statistics();
  REQUIRE(statistics.pending_command_count == runtime::kMaximumMailboxCommandCount);
  REQUIRE(statistics.dropped_command_count == 1);
  REQUIRE(statistics.dropped_entity_lifecycle_command_count == 0);
}

TEST_CASE("CommandMailbox still supersedes a pending entity's thrust while full",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  fill_with_distinct_thrusts(mailbox, runtime::kMaximumMailboxCommandCount);

  // The property that stops one client evicting another's input: a spamming client occupies the one
  // slot it already holds however full the mailbox is.
  REQUIRE(mailbox.submit(thrust_fixture(kFirstEntityId, 0.0, 1.0)) ==
          runtime::CommandSubmissionResult::kSuperseded);
  REQUIRE(mailbox.statistics().dropped_command_count == 0);
}

TEST_CASE("CommandMailbox evicts the oldest non-lifecycle command to admit a spawn",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  fill_with_distinct_thrusts(mailbox, runtime::kMaximumMailboxCommandCount);

  REQUIRE(mailbox.submit(spawn_fixture(kFirstControllerId)) ==
          runtime::CommandSubmissionResult::kAccepted);

  const std::vector<simulation::Command> drained = mailbox.drain();
  REQUIRE(drained.size() == runtime::kMaximumMailboxCommandCount);
  // The oldest thrust left and the spawn is last; no spawn or despawn was ever a candidate.
  REQUIRE(drained.front() == thrust_fixture(kSecondEntityId, 0.0, 0.0));
  REQUIRE(drained.back() == spawn_fixture(kFirstControllerId));
  REQUIRE(mailbox.statistics().dropped_command_count == 1);
  REQUIRE(mailbox.statistics().dropped_entity_lifecycle_command_count == 0);
}

TEST_CASE("CommandMailbox evicts the oldest non-lifecycle command to admit a despawn",
          "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  fill_with_distinct_thrusts(mailbox, runtime::kMaximumMailboxCommandCount);

  const std::uint64_t despawned_entity_id = runtime::kMaximumMailboxCommandCount + 1;
  REQUIRE(mailbox.submit(despawn_fixture(despawned_entity_id)) ==
          runtime::CommandSubmissionResult::kAccepted);

  const std::vector<simulation::Command> drained = mailbox.drain();
  REQUIRE(drained.back() == despawn_fixture(despawned_entity_id));
  REQUIRE(mailbox.statistics().dropped_command_count == 1);
}

TEST_CASE(
    "CommandMailbox refuses an incoming spawn rather than evicting a queued lifecycle command",
    "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  for (std::size_t index = 0; index < runtime::kMaximumMailboxCommandCount; ++index) {
    REQUIRE(mailbox.submit(despawn_fixture(static_cast<std::uint64_t>(index) + 1)) ==
            runtime::CommandSubmissionResult::kAccepted);
  }

  const std::uint64_t overflowing_controller_id = runtime::kMaximumMailboxCommandCount + 1;
  REQUIRE(mailbox.submit(spawn_fixture(overflowing_controller_id)) ==
          runtime::CommandSubmissionResult::kDroppedMailboxFull);

  const runtime::CommandMailbox::Statistics statistics = mailbox.statistics();
  REQUIRE(statistics.pending_command_count == runtime::kMaximumMailboxCommandCount);
  // A lost roster change is counted separately from a lost steering intent.
  REQUIRE(statistics.dropped_entity_lifecycle_command_count == 1);
  REQUIRE(statistics.dropped_command_count == 1);

  // Every queued despawn survived, which is the half of the policy that must never bend.
  const std::vector<simulation::Command> drained = mailbox.drain();
  REQUIRE(drained.size() == runtime::kMaximumMailboxCommandCount);
  for (const simulation::Command& command : drained) {
    REQUIRE(simulation::command_kind_of(command) == simulation::CommandKind::kDespawn);
  }
}

TEST_CASE("CommandMailbox statistics account for every submission", "[unit][runtime][mailbox]") {
  runtime::CommandMailbox mailbox(
      simulation::CommandKindMask::create({simulation::CommandKind::kThrust}));
  fill_with_distinct_thrusts(mailbox, runtime::kMaximumMailboxCommandCount);
  static_cast<void>(mailbox.submit(thrust_fixture(kFirstEntityId, 0.5, 0.5)));
  static_cast<void>(
      mailbox.submit(thrust_fixture(runtime::kMaximumMailboxCommandCount + 1, 0.0, 0.0)));
  static_cast<void>(mailbox.submit(spawn_fixture(kFirstControllerId)));

  const runtime::CommandMailbox::Statistics statistics = mailbox.statistics();
  REQUIRE(statistics.submitted_command_count == runtime::kMaximumMailboxCommandCount + 3);
  REQUIRE(statistics.accepted_command_count + statistics.dropped_command_count +
              statistics.rejected_unaccepted_kind_count ==
          statistics.submitted_command_count);
}

TEST_CASE("CommandMailbox loses no command under concurrent submitters",
          "[unit][runtime][mailbox][concurrency]") {
  runtime::CommandMailbox mailbox(simulation::CommandKindMask::all());
  std::barrier start_line(static_cast<std::ptrdiff_t>(kConcurrentSubmitterCount));
  std::vector<std::jthread> submitters;
  submitters.reserve(kConcurrentSubmitterCount);

  for (std::size_t submitter_index = 0; submitter_index < kConcurrentSubmitterCount;
       ++submitter_index) {
    submitters.emplace_back([&, submitter_index] {
      start_line.arrive_and_wait();
      for (std::size_t submission = 0; submission < kSubmissionsPerSubmitter; ++submission) {
        // Each submitter owns a distinct entity range, so every submission needs its own slot and
        // none of them supersedes another submitter's command.
        const std::uint64_t entity_id =
            static_cast<std::uint64_t>(submitter_index * kSubmissionsPerSubmitter + submission) + 1;
        static_cast<void>(mailbox.submit(thrust_fixture(entity_id, 0.0, 0.0)));
      }
    });
  }
  for (std::jthread& submitter : submitters) {
    submitter.join();
  }

  const std::size_t expected = kConcurrentSubmitterCount * kSubmissionsPerSubmitter;
  REQUIRE(expected <= runtime::kMaximumMailboxCommandCount);
  REQUIRE(mailbox.drain().size() == expected);
  REQUIRE(mailbox.statistics().dropped_command_count == 0);
}
