#include "controller_registry.hpp"

#include "controllers_validation_error.hpp"

#include <memory>
#include <string>

namespace blob_royale::controllers {
const ControllerRegistry::Registration*
ControllerRegistry::find(const std::string_view controller_kind) noexcept {
  for (const ControllerRegistry::Registration& registration : kControllerRegistrations) {
    if (registration.name == controller_kind) {
      return &registration;
    }
  }
  return nullptr;
}

std::span<const ControllerRegistry::Registration> ControllerRegistry::registrations() noexcept {
  return kControllerRegistrations;
}

bool ControllerRegistry::contains(const std::string_view controller_kind) noexcept {
  return find(controller_kind) != nullptr;
}

std::unique_ptr<Controller> ControllerRegistry::create(const std::string_view controller_kind,
                                                       const simulation::ControllerId controller,
                                                       const std::uint64_t seed,
                                                       const CreationContext context) {
  const Registration* registration = find(controller_kind);
  if (registration == nullptr) {
    throw ControllersValidationError(
        ControllersValidationCode::kControllerKindUnknown, "controller_registry.controller_kind",
        "controller kind " + std::string(controller_kind) +
            " is registered by no row; the registered kinds are " + registered_names());
  }
  if (registration->requires_profile
          ? (context.profile == nullptr || !context.tactical_identity)
          : (context.profile != nullptr || context.tactical_identity.has_value())) {
    throw ControllersValidationError(
        ControllersValidationCode::kControllerCreationContextInvalid,
        "controller_registry.creation_context",
        registration->requires_profile
            ? "profiled kind requires both profile and tactical identity"
            : "plain kind accepts neither profile nor tactical identity");
  }
  return registration->factory(controller, seed, context);
}

std::string ControllerRegistry::registered_names() {
  std::string names;
  for (const Registration& registration : kControllerRegistrations) {
    if (!names.empty()) {
      names.append(", ");
    }
    names.append(registration.name);
  }
  return names;
}

} // namespace blob_royale::controllers
