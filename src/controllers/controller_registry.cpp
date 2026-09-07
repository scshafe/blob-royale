#include "controller_registry.hpp"

#include "controllers_validation_error.hpp"

#include <memory>
#include <string>

namespace blob_royale::controllers {
namespace {

[[nodiscard]] const ControllerRegistry::Registration*
find_registration(const std::string_view controller_kind) noexcept {
  for (const ControllerRegistry::Registration& registration : kControllerRegistrations) {
    if (registration.name == controller_kind) {
      return &registration;
    }
  }
  return nullptr;
}

} // namespace

std::span<const ControllerRegistry::Registration> ControllerRegistry::registrations() noexcept {
  return kControllerRegistrations;
}

bool ControllerRegistry::contains(const std::string_view controller_kind) noexcept {
  return find_registration(controller_kind) != nullptr;
}

std::unique_ptr<Controller> ControllerRegistry::create(const std::string_view controller_kind,
                                                       const simulation::ControllerId controller,
                                                       const std::uint64_t seed) {
  const Registration* registration = find_registration(controller_kind);
  if (registration == nullptr) {
    throw ControllersValidationError(
        ControllersValidationCode::kControllerKindUnknown, "controller_registry.controller_kind",
        "controller kind " + std::string(controller_kind) +
            " is registered by no row; the registered kinds are " + registered_names());
  }
  return registration->factory(controller, seed);
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
