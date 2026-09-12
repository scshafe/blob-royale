#ifndef BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP
#define BLOB_ROYALE_CONTROLLERS_TACTICAL_PROFILE_HPP

#include "bot_profile_name.hpp"

#include <cstdint>
#include <string>

namespace blob_royale::controllers {

// canonical: tactical_profile -- four validated, active settings for one tactical algorithm.
class TacticalProfile final {
public:
  struct Section final {
    std::string profile_name;
    double objective_seek_probability;
    std::uint64_t reaction_delay_ticks;
    double aim_error;
    std::uint64_t target_persistence_ticks;
    friend bool operator==(const Section&, const Section&) = default;
  };

  // Validates in authored order. Throws CONTROLLERS.TACTICAL_PROFILE_* with the failed key;
  // required key presence and lexical integer validation belong to the configuration parser.
  [[nodiscard]] static TacticalProfile create(const Section& section);
  [[nodiscard]] const simulation::BotProfileName& name() const& noexcept { return name_; }
  const simulation::BotProfileName& name() const&& = delete;
  [[nodiscard]] double objective_seek_probability() const noexcept {
    return objective_seek_probability_;
  }
  [[nodiscard]] std::uint64_t reaction_delay_ticks() const noexcept {
    return reaction_delay_ticks_;
  }
  [[nodiscard]] double aim_error() const noexcept { return aim_error_; }
  [[nodiscard]] std::uint64_t target_persistence_ticks() const noexcept {
    return target_persistence_ticks_;
  }
  friend bool operator==(const TacticalProfile&, const TacticalProfile&) = default;

private:
  TacticalProfile(simulation::BotProfileName name, const Section& section) noexcept;
  simulation::BotProfileName name_;
  double objective_seek_probability_;
  std::uint64_t reaction_delay_ticks_;
  double aim_error_;
  std::uint64_t target_persistence_ticks_;
};

} // namespace blob_royale::controllers

#endif
