#ifndef BLOB_ROYALE_APPLICATION_MATCH_CONFIGURATION_HPP
#define BLOB_ROYALE_APPLICATION_MATCH_CONFIGURATION_HPP

#include "bot_profile_name.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blob_royale::application {

// canonical: match_configuration -- the validated `[match]` section: which game, on which map,
// with which bots.
//
// **This is the one value that says what is being played.** `mode` resolves through
// `gameplay::GameModeRegistry`, `map` names a directory under `maps_directory`, and each `bots`
// entry resolves through `controllers::ControllerRegistry`
// (`docs/architecture/0005-royale-mode.md` § "Mode configuration"). None of the four is readable
// from inside a tick, and no rule knows whether a command came from a browser or a bot.
//
// **Every name is validated against the protocol v2 grammar it will be published under**, not just
// against the registry, so a mode or map that could never be encoded is a startup rejection rather
// than a frame every client must close on: `mode` is `common.schema.json#/$defs/kind_name` and
// `map` is `#/$defs/map_name`. The map grammar admits no `/` and no leading `.`, which is also what
// keeps `map=` a name rather than a path a configuration file could point outside `maps_directory`
// with.
//
// **Two keys ADR 0005's worked example does not show, and why each is here.**
//
//   * `maps_directory` is where `map` is resolved. The ADR writes `maps/<name>` relative to the
//     repository, but a deployed process runs from a different working directory with its inputs
//     mounted somewhere else, and a silently defaulted root would resolve to whichever directory
//     the process happened to start in. Naming it is the same discipline `--config` follows.
//   * `seed` is the match seed. `GameWorld` is constructed with it and every bot is constructed
//     with it, and ADR 0004 § "Determinism obligations for framework code" fixes a match as
//     reproducible from `(map, mode configuration, seed, command log)` -- so a match with no
//     configured seed would be reproducible only by accident.
//
// related: map_loader.hpp -- what `maps_directory / map` is read by.
// related: ../gameplay/game_mode_registry.hpp -- what `mode` resolves through.
// related: ../controllers/controller_registry.hpp -- what each `bots` kind resolves through.
class MatchConfiguration final {
public:
  // One `kind:count` or `kind@profile:count` term. The kind is a registry key; profile names
  // resolve against the immutable catalogue at ApplicationConfig's cross-value boundary.
  struct BotRosterEntry final {
    std::string controller_kind;
    std::uint64_t count{};
    std::optional<simulation::BotProfileName> profile_name{};

    friend bool operator==(const BotRosterEntry&, const BotRosterEntry&) = default;
  };

  // Distinct kind/profile declarations one roster line may name.
  static constexpr std::size_t kMaximumBotRosterEntryCount = 16;
  // Bots one roster may seat. Each bot opens a `ControllerDirectory` entry exactly as a browser
  // does, so the honest ceiling is the host's; the tighter startup bound is the snapshot entity
  // budget, which `require_match_fits_snapshot_bound` checks against the loaded map.
  static constexpr std::uint64_t kMaximumBotCount = 256;

  // Validates every field and throws ApplicationInputError naming the one that failed:
  // `APPLICATION.MATCH.MODE_NAME_INVALID`, `MODE_UNKNOWN`, `MAP_NAME_INVALID`,
  // `LOBBY_SEAT_COUNT_OUT_OF_RANGE`, `BOT_ROSTER_INVALID`, `BOT_KIND_UNKNOWN`, and
  // `BOT_ROSTER_TOO_LARGE`, `BOT_PROFILE_REQUIRED`, and `BOT_PROFILE_UNEXPECTED`.
  // Direct roster vectors obey the same nonzero-count, unique-pair, and term-count rules as text.
  //
  // `lobby_seat_count` is how many seats the pre-match lobby is created with: a fact about who is
  // playing this match rather than a mode's balance number, which is why it lives here and not in
  // `[royale]` (`docs/architecture/0006-lobbies-as-rooms.md` § "Rooms"). It is bounded by the
  // engine's roster bound here and by the map's spawn markers in `match_startup_validation.hpp`,
  // and a mode without a lobby ignores it.
  [[nodiscard]] static MatchConfiguration create(std::string mode_name, std::string map_name,
                                                 std::filesystem::path maps_directory,
                                                 std::uint64_t seed, std::uint64_t lobby_seat_count,
                                                 std::vector<BotRosterEntry> bot_roster);

  // Parses comma-separated `kind:count` or `kind@profile:count`, or empty for no bots.
  // Throws ApplicationInputError with `APPLICATION.MATCH.BOT_ROSTER_INVALID` for a term with no
  // colon, an empty kind/profile, an absent, empty, non-numeric, or zero count, and a repeated
  // pair. Invalid profile identity propagates SimulationValidationError. Registry membership and
  // required/unexpected profile selection are `create`'s checks, not this one's.
  [[nodiscard]] static std::vector<BotRosterEntry> parse_bot_roster(std::string_view value);

  MatchConfiguration(const MatchConfiguration&) = default;
  MatchConfiguration(MatchConfiguration&&) noexcept = default;
  MatchConfiguration& operator=(const MatchConfiguration&) = default;
  MatchConfiguration& operator=(MatchConfiguration&&) noexcept = default;
  ~MatchConfiguration() = default;

  [[nodiscard]] const std::string& mode_name() const& noexcept { return mode_name_; }
  [[nodiscard]] const std::string& mode_name() const&& = delete;
  [[nodiscard]] const std::string& map_name() const& noexcept { return map_name_; }
  [[nodiscard]] const std::string& map_name() const&& = delete;
  [[nodiscard]] const std::filesystem::path& maps_directory() const& noexcept {
    return maps_directory_;
  }
  [[nodiscard]] const std::filesystem::path& maps_directory() const&& = delete;
  [[nodiscard]] std::uint64_t seed() const noexcept { return seed_; }
  // An *initial* value and not a rule: the roster it sizes is `MatchState` and a player in the
  // lobby changes it, so nothing reads this again once the initial world has been built.
  [[nodiscard]] std::uint64_t lobby_seat_count() const noexcept { return lobby_seat_count_; }
  [[nodiscard]] std::span<const BotRosterEntry> bot_roster() const& noexcept { return bot_roster_; }
  [[nodiscard]] std::span<const BotRosterEntry> bot_roster() const&& = delete;

  // The directory one `MapLoader::load` reads. Derived rather than stored, because the two parts
  // are separately validated and a stored join would be a second place they could disagree.
  [[nodiscard]] std::filesystem::path map_directory() const { return maps_directory_ / map_name_; }

  // Every bot the roster seats, summed over its terms.
  [[nodiscard]] std::uint64_t total_bot_count() const noexcept;

  friend bool operator==(const MatchConfiguration&, const MatchConfiguration&) = default;

private:
  MatchConfiguration(std::string mode_name, std::string map_name,
                     std::filesystem::path maps_directory, std::uint64_t seed,
                     std::uint64_t lobby_seat_count,
                     std::vector<BotRosterEntry> bot_roster) noexcept;

  std::string mode_name_;
  std::string map_name_;
  std::filesystem::path maps_directory_;
  std::uint64_t seed_;
  std::uint64_t lobby_seat_count_;
  std::vector<BotRosterEntry> bot_roster_;
};

} // namespace blob_royale::application

#endif
