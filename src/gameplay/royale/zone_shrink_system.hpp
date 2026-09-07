#ifndef BLOB_ROYALE_GAMEPLAY_ROYALE_ZONE_SHRINK_SYSTEM_HPP
#define BLOB_ROYALE_GAMEPLAY_ROYALE_ZONE_SHRINK_SYSTEM_HPP

#include "map_definition.hpp"
#include "royale/royale_configuration.hpp"
#include "simulation_system.hpp"
#include "vector2.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace blob_royale::gameplay {

// canonical: royale_zone_geometry -- the safe zone's centre, full radius, and shrink curve.
// @extension-point simulation_system
//
// The three functions below are the whole of the zone's shape, written as pure functions of the
// map's bounds and one integer tick difference so that **the zone carries no accumulated
// floating-point state**: any committed snapshot fully determines every subsequent radius, and a
// replay from an arbitrary snapshot reproduces the zone exactly
// (`docs/architecture/0005-royale-mode.md` § "Safe zone").
//
// A moving zone is `zone_center` becoming a function of the same integer `e`. That is a versioned
// rule change because match outcomes change; it needs no new state, no new component, and no new
// seam.

// `(width / 2, height / 2)`. The zone is centred on the arena and, under `royale`, never moves.
[[nodiscard]] simulation::Vector2 zone_center(const simulation::ArenaBounds& bounds);

// `R_full = sqrt((width / 2)^2 + (height / 2)^2)`, the circumscribed radius of the arena rectangle.
//
// Starting there is what guarantees no player is outside at the moment `running` begins: ADR 0003
// confines a committed centre to `[r, width - r] x [r, height - r]`, whose farthest point from the
// centre is `sqrt((width/2 - r)^2 + (height/2 - r)^2)`, strictly less than `R_full` for any valid
// `r > 0`.
[[nodiscard]] double zone_full_radius(const simulation::ArenaBounds& bounds);

// The radius after `elapsed_running_ticks` committed ticks of `running`, with `shrink_ticks` the
// configured `T`:
//
//     radius(e) = R_min                                  when T == 0, or when e >= T
//     radius(e) = R_full - (R_full - R_min) * (e / T)     when 0 <= e < T
//
// **The division precedes the multiplication and that order is the contract**
// (`docs/architecture/0003-deterministic-simulation-contract.md` § "Floating-point contract"
// forbids reassociating it). The `e >= T` branch returns `R_min` by assignment rather than by
// arithmetic, so the held radius is exactly `R_min` rather than a rounding of it, and the `T == 0`
// case is defined instead of dividing by zero.
[[nodiscard]] double zone_radius(double full_radius, double minimum_radius,
                                 std::uint64_t elapsed_running_ticks, std::uint64_t shrink_ticks);

// canonical: zone_shrink -- the first kPostKernel system: it writes the `Zone` component.
//
// It writes the `Zone` component and nothing else. The radius it writes is a pure function of the
// committed phase and the committed tick counters:
//
//     lobby      R_full
//     countdown  R_full
//     running    radius(tick_sequence - running_started_tick)
//     ended      radius(phase_started_tick - running_started_tick), frozen at the final `running`
//                value
//
// Both boundary ticks are consistent without a re-evaluation after the transition. On the tick that
// enters `running` this system still observes `countdown` and writes `R_full`, which is exactly
// what `radius(0)` returns for any `T > 0`; on the first `ended` tick, `phase_started_tick` is the
// tick the transition committed, so the frozen value is exactly what the last `running` tick wrote.
//
// **It creates the zone entity from the tick's `EntityIdReservation` on the first tick it observes
// no entity carrying `Zone`**, and assigns the component on every tick after that. The zone entity
// owns no `PhysicsBody` and no `Controllable`, so it never enters a contact pair, never integrates,
// is never counted alive, and is never wiped between matches: it persists for the life of the
// simulation. A tick whose reservation is empty cannot create anything at all
// (`entity_id_reservation.hpp`), so that is a hard failure naming the cause rather than a match
// played with no zone.
// related: royale/zone_elimination_system.hpp -- the system that reads what this writes.
// related: components/zone_component.hpp -- the value it writes.
class ZoneShrinkSystem final : public simulation::SimulationSystem {
public:
  // The stable name the pipeline, diagnostics, and fixtures know this system by.
  static constexpr std::string_view kSystemName = "zone_shrink";

  [[nodiscard]] static std::unique_ptr<const simulation::SimulationSystem>
  create(RoyaleConfiguration configuration);

  // Public because the pipeline holds `std::unique_ptr<const SimulationSystem>` and
  // `std::make_unique` needs an accessible constructor. The configuration is held **by value**, so
  // nothing this system reads points back at the mode the engine destroys at construction.
  explicit ZoneShrinkSystem(RoyaleConfiguration configuration) noexcept;

  [[nodiscard]] std::string_view name() const noexcept override { return kSystemName; }

  void apply(simulation::GameWorld& world, const simulation::TickContext& context) const override;

private:
  RoyaleConfiguration configuration_;
};

} // namespace blob_royale::gameplay

#endif
