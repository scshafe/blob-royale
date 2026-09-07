#include "observation.hpp"

#include "component_store.hpp"
#include "components/controllable_component.hpp"
#include "controllers_validation_error.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace blob_royale::controllers {
namespace {

// canonical: controller_body_lookup -- the one resolution of ControllerId to the body it drives.
//
// The published `Controllable` store is the only place the two identity spaces meet, and it is in
// strict ascending `EntityId` order, so a controller driving two bodies is unrepresentable and the
// first match is the only match. The scan is linear over a store bounded by
// `simulation::kMaximumEntityCount`; a controller decides at presentation cadence, not per tick, so
// there is no index to keep in agreement with the store -- which is the class of duplication this
// codebase removes rather than adds.
[[nodiscard]] std::optional<simulation::EntityId>
entity_driven_by(const simulation::WorldSnapshot& snapshot,
                 const simulation::ControllerId controller) noexcept {
  for (const simulation::ComponentStore<simulation::Controllable>::Entry& entry :
       snapshot.components<simulation::Controllable>()) {
    if (entry.value.controller_id == controller) {
      return entry.entity;
    }
  }
  return std::nullopt;
}

} // namespace

Observation Observation::create(std::shared_ptr<const simulation::WorldSnapshot> snapshot,
                                const simulation::ControllerId controller) {
  if (snapshot == nullptr) {
    throw ControllersValidationError(
        ControllersValidationCode::kObservationSnapshotAbsent, "observation.snapshot",
        "controller " + std::to_string(controller.value()) +
            " cannot observe an absent world; SnapshotPublication::latest() is never null");
  }
  const std::optional<simulation::EntityId> entity = entity_driven_by(*snapshot, controller);
  return Observation(std::move(snapshot), controller, entity);
}

Observation::Observation(std::shared_ptr<const simulation::WorldSnapshot> snapshot,
                         const simulation::ControllerId controller,
                         const std::optional<simulation::EntityId> entity) noexcept
    : snapshot_(std::move(snapshot)), controller_(controller), entity_(entity) {}

} // namespace blob_royale::controllers
