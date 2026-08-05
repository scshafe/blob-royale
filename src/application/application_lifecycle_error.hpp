#ifndef BLOB_ROYALE_APPLICATION_APPLICATION_LIFECYCLE_ERROR_HPP
#define BLOB_ROYALE_APPLICATION_APPLICATION_LIFECYCLE_ERROR_HPP

#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::application {

enum class ApplicationLifecycleErrorCode {
  kRunAlreadyInvoked,
  kServerStartupTimedOut,
  kServerTerminatedUnexpectedly,
  kControlWaitFailed,
};

[[nodiscard]] constexpr std::string_view
application_lifecycle_error_code_name(const ApplicationLifecycleErrorCode error_code) noexcept {
  switch (error_code) {
  case ApplicationLifecycleErrorCode::kRunAlreadyInvoked:
    return "APPLICATION.LIFECYCLE.RUN_ALREADY_INVOKED";
  case ApplicationLifecycleErrorCode::kServerStartupTimedOut:
    return "APPLICATION.LIFECYCLE.SERVER_STARTUP_TIMED_OUT";
  case ApplicationLifecycleErrorCode::kServerTerminatedUnexpectedly:
    return "APPLICATION.LIFECYCLE.SERVER_TERMINATED_UNEXPECTEDLY";
  case ApplicationLifecycleErrorCode::kControlWaitFailed:
    return "APPLICATION.LIFECYCLE.CONTROL_WAIT_FAILED";
  }
  return "APPLICATION.LIFECYCLE.ERROR_CODE_INVALID";
}

// canonical: application_lifecycle_error -- a composition-root lifecycle invariant failure.
class ApplicationLifecycleError final : public std::runtime_error {
public:
  ApplicationLifecycleError(ApplicationLifecycleErrorCode error_code, std::string context,
                            std::string detail);

  [[nodiscard]] ApplicationLifecycleErrorCode error_code() const noexcept { return error_code_; }
  [[nodiscard]] std::string_view code() const noexcept {
    return application_lifecycle_error_code_name(error_code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;
  [[nodiscard]] const std::string& detail() const& noexcept { return detail_; }
  [[nodiscard]] const std::string& detail() const&& = delete;

private:
  [[nodiscard]] static std::string build_message(ApplicationLifecycleErrorCode error_code,
                                                 std::string_view context, std::string_view detail);

  ApplicationLifecycleErrorCode error_code_;
  std::string context_;
  std::string detail_;
};

} // namespace blob_royale::application

#endif
