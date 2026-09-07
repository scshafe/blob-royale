#include "runtime_controller_directory_view.hpp"

#include "controller_presentation.hpp"

#include <optional>
#include <utility>

namespace blob_royale::server {

RuntimeControllerDirectoryView::RuntimeControllerDirectoryView(
    const runtime::ControllerDirectory& controller_directory) noexcept
    : controller_directory_(&controller_directory) {}

std::optional<protocol::PublishedController>
RuntimeControllerDirectoryView::find_controller(const simulation::ControllerId controller) const {
  std::optional<runtime::ControllerPresentation> presentation =
      controller_directory_->find(controller);
  if (!presentation.has_value()) {
    return std::nullopt;
  }
  return protocol::PublishedController{.controller_kind = std::move(presentation->controller_kind),
                                       .display_name = std::move(presentation->display_name)};
}

} // namespace blob_royale::server
