#ifndef BLOB_ROYALE_TESTS_FIXTURES_REPLAY_FIXTURE_HPP
#define BLOB_ROYALE_TESTS_FIXTURES_REPLAY_FIXTURE_HPP

#include "command_registry.hpp"
#include "entity_id.hpp"
#include "game_mode_configuration.hpp"
#include "king_of_the_hill/king_of_the_hill_configuration.hpp"
#include "map_definition.hpp"
#include "race/race_configuration.hpp"
#include "royale/royale_configuration.hpp"
#include "simulation_config.hpp"
#include "tick_sequence.hpp"
#include "world_snapshot.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace blob_royale::testing {

namespace simulation = blob_royale::simulation;
namespace gameplay = blob_royale::gameplay;

// canonical: replay_fixture -- the `(map, mode configuration, seed, command log)` gameplay fixture.
//
// **This is the primary gameplay fixture format**, fixed by
// `docs/architecture/0004-gameplay-architecture.md` § "Determinism obligations for framework code":
// a match is reproducible from that tuple, so that tuple is what a test, a bug report, and a replay
// viewer all carry. A replay fixture is a directory under `tests/fixtures/replays/`:
//
//   match.ini     [match] mode, seed, tick_count, lobby_seat_count
//                 [map] name, width_world_units, height_world_units
//                 [simulation] player_radius_world_units, ticks_per_second, spatial_grid_columns,
//                              spatial_grid_rows, drag_per_second
//                 the one section of the mode `[match] mode` names: [royale] with the six keys of
//                 `docs/architecture/0005-royale-mode.md` § "Mode configuration", or
//                 [king_of_the_hill] with the eleven keys or [race] with the eight keys of
//                 ADR 0007's mode tables. A fixture carries
//                 its own mode's section and no other, because a key no reader asked for is a
//                 rejection.
//   markers.csv   marker_kind,position_x_world_units,position_y_world_units
//   commands.csv  tick_sequence,entity_id,command_kind,controller_id,direction_x,direction_y,
//                 seat_index,seat_count,npc_kind
//
// **A line whose first character is `#` is a comment**, in every one of the three files, and may
// appear anywhere including above a header row. It exists because a fixture has to be able to state
// a derivation: "the `start_match` is at tick 2 because tick 1 is when the fourth seat fills" is
// the difference between a number a reader can check and a number a reader has to trust. The rule
// is full-line only, so `#` inside a value is still an ordinary character and no column can be
// truncated by one.
//
// The map travels **with** the replay rather than being named in `maps/`, because `maps/` and its
// loader arrive in plan Step 25 and a fixture that cannot be run is not a fixture. `[map]` and
// `markers.csv` are the same two things a map directory holds, so Step 25's loader replaces this
// reader without changing a single fixture's numbers.
//
// **Every reader here is strict and fails closed**: an unknown section, an unknown key, a missing
// key, a duplicate key, a header that is not exactly the expected one, a wrong column count, a
// non-numeric value, an unknown command kind, and a payload column that is filled for a kind that
// does not use it are each a rejection naming the file, the line, and the cause. A fixture that
// silently parsed differently than it reads would be worse than no fixture at all.
//
// **The reservation policy is reproduced here** because a replay has no runtime to hand it one:
// every tick receives a contiguous block of `spawn_count(tick) + kSystemCreatedEntityHeadroom` ids
// beginning at a monotonic cursor that advances by the same width, so an entity id is a
// deterministic function of the command log alone and every tick has room for the one entity a
// system may create. Plan Step 22 gives the runtime its own allocator; this is what a replay uses
// until then, and `first_reserved_entity_id` is how a test names an id the log did not.
// related: royale_replay_fixture_tests.cpp -- the suite this format exists for.
// related: king_of_the_hill_replay_fixture_tests.cpp -- the hill's suite on the same format.
// related: race_replay_fixture_tests.cpp -- the race's suite on the same format.
// related: game_mode_configuration.hpp -- the validated sections this hands the mode registry.

// A rejection from the replay reader. It is not a `SimulationValidationError` or a
// `GameplayValidationError` because a malformed fixture is a defect in the test data rather than in
// either library, and conflating the two would let a broken fixture pass as a caught rejection.
class ReplayFixtureError final : public std::runtime_error {
public:
  explicit ReplayFixtureError(const std::string& message) : std::runtime_error(message) {}
};

class ReplayFixture final {
public:
  // The width of every tick's EntityIdReservation beyond that tick's spawn count, taken from the
  // one definition in `blob_simulation` rather than restated, so a replay's numbering cannot drift
  // from the production allocator's (`simulation/simulation_limits.hpp`).
  static constexpr std::uint64_t kSystemCreatedEntityHeadroom =
      simulation::kSystemCreatedEntityHeadroom;

  // Reads and validates one replay directory. Throws ReplayFixtureError naming the file and the
  // cause for every malformed input.
  [[nodiscard]] static ReplayFixture load(const std::filesystem::path& replay_directory);

  // Reads the fixture of this name under `tests/fixtures/replays/`.
  [[nodiscard]] static ReplayFixture named(const std::string& fixture_name);

  ReplayFixture(const ReplayFixture&) = default;
  ReplayFixture(ReplayFixture&&) noexcept = default;
  ReplayFixture& operator=(const ReplayFixture&) = delete;
  ReplayFixture& operator=(ReplayFixture&&) = delete;
  ~ReplayFixture() = default;

  [[nodiscard]] const std::string& name() const noexcept { return name_; }
  [[nodiscard]] const std::string& mode_name() const noexcept { return mode_name_; }
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
  [[nodiscard]] std::uint64_t tick_count() const noexcept { return tick_count_; }
  // How many seats the lobby is created with: `[match] lobby_seat_count`, a fact about who plays
  // rather than a royale balance value, exactly as the production configuration has it.
  [[nodiscard]] std::uint64_t lobby_seat_count() const noexcept { return lobby_seat_count_; }
  [[nodiscard]] const simulation::SimulationConfig& configuration() const noexcept {
    return configuration_;
  }
  [[nodiscard]] const simulation::MapDefinition& map() const noexcept { return map_; }
  // The validated sections the mode is built from. The section the fixture's mode names is the
  // fixture's own; every other member holds that mode's declared defaults, which the named mode
  // never reads.
  [[nodiscard]] const gameplay::GameModeConfiguration& mode_configuration() const noexcept {
    return mode_configuration_;
  }
  [[nodiscard]] const gameplay::RoyaleConfiguration& royale() const noexcept {
    return mode_configuration_.royale;
  }
  [[nodiscard]] const gameplay::KingOfTheHillConfiguration& king_of_the_hill() const noexcept {
    return mode_configuration_.king_of_the_hill;
  }
  [[nodiscard]] const gameplay::RaceConfiguration& race() const noexcept {
    return mode_configuration_.race;
  }

  // The lowest id of the block handed to `tick_sequence`, which is the id the first spawn command
  // of that tick created. Throws ReplayFixtureError for a tick outside `[1, tick_count]`.
  [[nodiscard]] simulation::EntityId first_reserved_entity_id(std::uint64_t tick_sequence) const;

  // The id the `spawn_index`-th spawn command of `tick_sequence` created, counting from zero in the
  // batch's canonical order, which is ascending ControllerId.
  [[nodiscard]] simulation::EntityId spawned_entity_id(std::uint64_t tick_sequence,
                                                       std::uint64_t spawn_index) const;

  // Runs the whole match from a fresh simulation and returns one snapshot per committed tick, in
  // order. Called repeatedly, it is the 100-fresh-run bit-identity harness: two returned vectors
  // must compare equal member for member.
  [[nodiscard]] std::vector<simulation::WorldSnapshot> run() const;

private:
  ReplayFixture(std::string name, std::string mode_name, std::uint64_t seed,
                std::uint64_t tick_count, std::uint64_t lobby_seat_count,
                simulation::SimulationConfig configuration, simulation::MapDefinition map,
                gameplay::GameModeConfiguration mode_configuration,
                std::vector<std::vector<simulation::Command>> commands_by_tick,
                std::vector<std::uint64_t> spawn_count_by_tick);

  std::string name_;
  std::string mode_name_;
  std::uint64_t seed_;
  std::uint64_t tick_count_;
  std::uint64_t lobby_seat_count_;
  simulation::SimulationConfig configuration_;
  simulation::MapDefinition map_;
  gameplay::GameModeConfiguration mode_configuration_;
  // Indexed by `tick_sequence - 1`, so entry zero is tick 1. A tick with no command holds an empty
  // vector rather than being absent, because every tick is stepped.
  std::vector<std::vector<simulation::Command>> commands_by_tick_;
  std::vector<std::uint64_t> spawn_count_by_tick_;
};

// The index of the first tick at which two snapshot sequences differ, or zero when they are equal
// member for member. Sequences of different length differ at the first index past the shorter one.
//
// It reports an index rather than a bool because "run 47 diverged" is not actionable and "run 47
// diverged at tick 312" is: the divergence is at the earliest committed tick that differs, which is
// where a determinism defect is diagnosable (`docs/architecture/0003-deterministic-simulation-
// contract.md` § "Fixture contract and expected outcomes").
[[nodiscard]] std::size_t
first_divergent_tick(const std::vector<simulation::WorldSnapshot>& expected,
                     const std::vector<simulation::WorldSnapshot>& actual);

} // namespace blob_royale::testing

#endif
