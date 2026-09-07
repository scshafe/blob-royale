#ifndef BLOB_ROYALE_SIMULATION_MATCH_OUTCOME_HPP
#define BLOB_ROYALE_SIMULATION_MATCH_OUTCOME_HPP

#include "entity_id.hpp"
#include "team_id.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace blob_royale::simulation {

// canonical: match_outcome_kind -- which of the four answers an outcome is.
//
// Named separately from the value so the protocol boundary and a diagnostic can switch on one
// enumerator instead of probing two optionals, and so a fifth answer would be a compile error at
// every switch rather than a silently unhandled case.
enum class MatchOutcomeKind : std::uint8_t {
  kUndecided = 0,
  kWonByEntity = 1,
  kWonByTeam = 2,
  kDrawn = 3,
};

[[nodiscard]] constexpr std::string_view
match_outcome_kind_name(const MatchOutcomeKind kind) noexcept {
  switch (kind) {
  case MatchOutcomeKind::kUndecided:
    return "undecided";
  case MatchOutcomeKind::kWonByEntity:
    return "won_by_entity";
  case MatchOutcomeKind::kWonByTeam:
    return "won_by_team";
  case MatchOutcomeKind::kDrawn:
    return "drawn";
  }
  return "match_outcome_kind_invalid";
}

// canonical: match_outcome -- who won, expressed once for every mode.
//
// The four factories are the complete vocabulary. `won_by_team` exists because a team mode would
// otherwise have to encode a side in an EntityId, and it was added to this interface *because* the
// three-mode table in `docs/architecture/0004-gameplay-architecture.md` § "Game modes and the
// match lifecycle" found capture the flag unable to express its ending without it.
//
// The winner is carried in **distinct members** rather than one variant field, because that is the
// shape protocol v2 publishes: a decided outcome names either an entity or a team, and a reader
// that asks the wrong question gets a defined absence instead of a reinterpreted integer.
// related: match_objective.hpp -- the mode declaration that produces one.
// related: match_state.hpp -- the world state that commits one.
class MatchOutcome final {
public:
  // No decision yet. This is what a mode returns in every phase but the one it ends in, and the
  // value MatchState carries until a match is decided.
  [[nodiscard]] static MatchOutcome undecided() noexcept { return MatchOutcome(); }

  [[nodiscard]] static MatchOutcome won_by_entity(const EntityId winner) noexcept {
    MatchOutcome outcome;
    outcome.kind_ = MatchOutcomeKind::kWonByEntity;
    outcome.winning_entity_ = winner;
    return outcome;
  }

  [[nodiscard]] static MatchOutcome won_by_team(const TeamId winner) noexcept {
    MatchOutcome outcome;
    outcome.kind_ = MatchOutcomeKind::kWonByTeam;
    outcome.winning_team_ = winner;
    return outcome;
  }

  // Decided with no winner: a mutual finish, an empty field, or a clock expiring level.
  [[nodiscard]] static MatchOutcome drawn() noexcept {
    MatchOutcome outcome;
    outcome.kind_ = MatchOutcomeKind::kDrawn;
    return outcome;
  }

  MatchOutcome(const MatchOutcome&) = default;
  MatchOutcome(MatchOutcome&&) noexcept = default;
  MatchOutcome& operator=(const MatchOutcome&) = default;
  MatchOutcome& operator=(MatchOutcome&&) noexcept = default;
  ~MatchOutcome() = default;

  [[nodiscard]] MatchOutcomeKind kind() const noexcept { return kind_; }

  // Whether the match is over. This is the whole question the engine's lifecycle system asks, so
  // `running -> ended` never has to know which kind of decision was reached.
  [[nodiscard]] bool is_decided() const noexcept { return kind_ != MatchOutcomeKind::kUndecided; }

  // The winning entity, or nullopt for every other kind including a team win.
  [[nodiscard]] std::optional<EntityId> winning_entity() const noexcept { return winning_entity_; }

  // The winning team, or nullopt for every other kind including an entity win.
  [[nodiscard]] std::optional<TeamId> winning_team() const noexcept { return winning_team_; }

  friend bool operator==(const MatchOutcome&, const MatchOutcome&) = default;

private:
  MatchOutcome() noexcept = default;

  MatchOutcomeKind kind_{MatchOutcomeKind::kUndecided};
  std::optional<EntityId> winning_entity_;
  std::optional<TeamId> winning_team_;
};

} // namespace blob_royale::simulation

#endif
