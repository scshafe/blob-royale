#include "input_batch.hpp"

#include "simulation_limits.hpp"
#include "simulation_validation_error.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace blob_royale::simulation {
namespace {

// One submitted command carried with every key the canonical order and the de-duplication need.
struct OrderedCommand final {
  std::uint32_t application_rank;
  std::uint64_t addressed_identity;
  std::size_t submission_index;
  Command command;
};

// The identity a command addresses. A spawn addresses its ControllerId because the engine, not
// the command, chooses the EntityId (`commands/spawn_command.hpp`); every other kind addresses
// the EntityId it names. Adding a kind adds its arm here.
[[nodiscard]] std::uint64_t addressed_identity_of(const Command& command) noexcept {
  return std::visit(
      []<typename CommandType>(const CommandType& value) -> std::uint64_t {
        if constexpr (std::is_same_v<CommandType, SpawnCommand>) {
          return value.controller.value();
        } else {
          return value.entity.value();
        }
      },
      command);
}

[[nodiscard]] std::string command_position(const std::size_t submission_index) {
  return " (submitted command " + std::to_string(submission_index) + ")";
}

// Written as a negated `<=` so a non-finite component is rejected too, although Vector2::create
// already makes one unreachable: it rejects a non-finite component with
// SIMULATION.PHYSICAL_SCALAR_NOT_FINITE before a ThrustCommand can hold it.
[[nodiscard]] bool within_thrust_direction_range(const double component) noexcept {
  return std::abs(component) <= kMaximumThrustDirectionComponentMagnitude;
}

void validate_thrust_direction(const ThrustCommand& thrust, const std::size_t submission_index) {
  if (within_thrust_direction_range(thrust.direction.x()) &&
      within_thrust_direction_range(thrust.direction.y())) {
    return;
  }
  throw SimulationValidationError(
      SimulationValidationCode::kInputBatchThrustDirectionOutOfRange,
      "input_batch.commands.thrust.direction",
      "thrust direction (" + std::to_string(thrust.direction.x()) + ", " +
          std::to_string(thrust.direction.y()) + ") for EntityId " +
          std::to_string(thrust.entity.value()) + " has a component outside [-1, 1]" +
          command_position(submission_index));
}

// A despawn naming an id this batch's reservation would issue is the "spawn and despawn in one
// batch" rejection of `docs/architecture/0003-deterministic-simulation-contract.md`
// § "Accepted simulation input", expressed over the identities the command shapes carry; see
// input_batch.hpp for why the two identity spaces cannot be joined here.
void validate_despawn_target(const DespawnCommand& despawn,
                             const EntityIdReservation& entity_id_reservation,
                             const std::size_t submission_index) {
  if (!entity_id_reservation.contains(despawn.entity)) {
    return;
  }
  throw SimulationValidationError(
      SimulationValidationCode::kInputBatchSpawnAndDespawnConflict,
      "input_batch.commands.despawn.entity",
      "despawn of EntityId " + std::to_string(despawn.entity.value()) +
          " names an id inside this tick's reservation of " +
          std::to_string(entity_id_reservation.count()) + " ids from EntityId " +
          std::to_string(entity_id_reservation.first_entity_id().value()) +
          ", so it both spawns and despawns in one batch" + command_position(submission_index));
}

void validate_command(const Command& command, const CommandKindMask accepted_kinds,
                      const EntityIdReservation& entity_id_reservation,
                      const std::size_t submission_index) {
  const CommandKind kind = command_kind_of(command);
  if (!accepted_kinds.contains(kind)) {
    throw SimulationValidationError(
        SimulationValidationCode::kInputBatchCommandKindNotAccepted, "input_batch.commands.kind",
        "command kind " + std::string(command_kind_name_of(kind)) +
            " is absent from the mode's accepted command kinds" +
            command_position(submission_index));
  }

  if (const auto* thrust = std::get_if<ThrustCommand>(&command); thrust != nullptr) {
    validate_thrust_direction(*thrust, submission_index);
  }
  if (const auto* despawn = std::get_if<DespawnCommand>(&command); despawn != nullptr) {
    validate_despawn_target(*despawn, entity_id_reservation, submission_index);
  }
}

} // namespace

InputBatch InputBatch::create(std::vector<Command> commands, const CommandKindMask accepted_kinds,
                              const EntityIdReservation entity_id_reservation) {
  if (commands.size() > kMaximumInputBatchCommandCount) {
    throw SimulationValidationError(SimulationValidationCode::kInputBatchCommandLimitExceeded,
                                    "input_batch.commands",
                                    "submitted command count " + std::to_string(commands.size()) +
                                        " exceeds the accepted limit " +
                                        std::to_string(kMaximumInputBatchCommandCount));
  }

  std::vector<OrderedCommand> ordered;
  ordered.reserve(commands.size());
  for (std::size_t index = 0; index < commands.size(); ++index) {
    validate_command(commands[index], accepted_kinds, entity_id_reservation, index);
    const std::uint32_t application_rank =
        command_kind_application_rank(command_kind_of(commands[index]));
    const std::uint64_t addressed_identity = addressed_identity_of(commands[index]);
    ordered.push_back(
        OrderedCommand{application_rank, addressed_identity, index, std::move(commands[index])});
  }

  // Submission index is unique, so this comparison is a strict total order and the sort needs no
  // stability guarantee to be deterministic.
  std::sort(ordered.begin(), ordered.end(),
            [](const OrderedCommand& left, const OrderedCommand& right) {
              if (left.application_rank != right.application_rank) {
                return left.application_rank < right.application_rank;
              }
              if (left.addressed_identity != right.addressed_identity) {
                return left.addressed_identity < right.addressed_identity;
              }
              return left.submission_index < right.submission_index;
            });

  // Each (rank, identity) run now holds one kind's commands for one identity in submission order,
  // so keeping the run's final element is "the last command of a kind for an identity wins".
  std::vector<Command> canonical;
  canonical.reserve(ordered.size());
  for (std::size_t index = 0; index < ordered.size(); ++index) {
    const bool is_last_of_run =
        index + 1 == ordered.size() ||
        ordered[index + 1].application_rank != ordered[index].application_rank ||
        ordered[index + 1].addressed_identity != ordered[index].addressed_identity;
    if (is_last_of_run) {
      canonical.push_back(std::move(ordered[index].command));
    }
  }

  return InputBatch(std::move(canonical), entity_id_reservation);
}

InputBatch InputBatch::empty() { return InputBatch({}, EntityIdReservation::none()); }

InputBatch::InputBatch(std::vector<Command> commands,
                       const EntityIdReservation entity_id_reservation) noexcept
    : commands_(std::move(commands)), entity_id_reservation_(entity_id_reservation) {}

} // namespace blob_royale::simulation
