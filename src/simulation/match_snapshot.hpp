#ifndef BLOB_ROYALE_SIMULATION_MATCH_SNAPSHOT_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_SNAPSHOT_HPP

#include "match_state.hpp"

#include <string>
#include <utility>

namespace blob_royale::simulation {

// canonical: match_snapshot -- the match section of one published snapshot.
//
// It is the committed MatchState plus the running mode's name, which is the whole generic
// lifecycle header protocol v2 publishes: mode name, phase, phase start tick, running start tick,
// outcome, and one mode-state block identified by its schema id
// (`docs/architecture/0004-gameplay-architecture.md` § "Snapshots and protocol shape";
// `docs/architecture/0005-royale-mode.md` § "Match section fields").
//
// The mode name is copied rather than referenced because a snapshot is an owning publication value
// that outlives no simulation in particular, and it is a `std::string` rather than a bounded name
// because a snapshot is copied at presentation cadence, not per tick, and every shipped mode name
// fits a small-string buffer.
//
// **Zone radius and alive count are deliberately not fields here.** The zone is a `Zone` component
// on an entity and travels with the entity list; the alive count is the number of published
// entities carrying both a PhysicsBody and a Controllable. Publishing either twice would give a
// client two sources for one value and a way to disagree with itself.
// related: world_snapshot.hpp -- the publication this is a section of.
// related: match_state.hpp -- the world state this copies.
class MatchSnapshot final {
public:
  [[nodiscard]] static MatchSnapshot create(std::string mode_name, MatchState state) {
    return MatchSnapshot(std::move(mode_name), std::move(state));
  }

  MatchSnapshot(const MatchSnapshot&) = default;
  MatchSnapshot(MatchSnapshot&&) noexcept = default;
  MatchSnapshot& operator=(const MatchSnapshot&) = default;
  MatchSnapshot& operator=(MatchSnapshot&&) noexcept = default;
  ~MatchSnapshot() = default;

  [[nodiscard]] std::string_view mode_name() const& noexcept { return mode_name_; }
  [[nodiscard]] std::string_view mode_name() const&& = delete;

  [[nodiscard]] MatchPhase phase() const noexcept { return state_.phase; }
  [[nodiscard]] TickSequence phase_started_tick() const noexcept {
    return state_.phase_started_tick;
  }
  [[nodiscard]] TickSequence running_started_tick() const noexcept {
    return state_.running_started_tick;
  }
  [[nodiscard]] const MatchOutcome& outcome() const& noexcept { return state_.outcome; }
  [[nodiscard]] const MatchOutcome& outcome() const&& = delete;

  // The mode's own match-state block. `mode_match_state_schema_id_of` names which arm it holds.
  [[nodiscard]] const ModeMatchState& mode_state() const& noexcept { return state_.mode_state; }
  [[nodiscard]] const ModeMatchState& mode_state() const&& = delete;

  friend bool operator==(const MatchSnapshot&, const MatchSnapshot&) = default;

private:
  MatchSnapshot(std::string mode_name, MatchState state)
      : mode_name_(std::move(mode_name)), state_(std::move(state)) {}

  std::string mode_name_;
  MatchState state_;
};

} // namespace blob_royale::simulation

#endif
