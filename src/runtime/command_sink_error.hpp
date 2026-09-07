#ifndef BLOB_ROYALE_RUNTIME_COMMAND_SINK_ERROR_HPP
#define BLOB_ROYALE_RUNTIME_COMMAND_SINK_ERROR_HPP

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

namespace blob_royale::runtime {

// Why one session could not be opened. Every value names a bounded resource or a rejected
// proxy-supplied string, so a caller can decide between refusing the connection and retrying.
enum class CommandSinkErrorCode : std::uint8_t {
  kControllerDirectoryFull = 0,
  kControllerKindRejected = 1,
  kDisplayNameRejected = 2,
  kControllerIdExhausted = 3,
};

[[nodiscard]] constexpr std::string_view
command_sink_error_code_name(const CommandSinkErrorCode code) noexcept {
  switch (code) {
  case CommandSinkErrorCode::kControllerDirectoryFull:
    return "RUNTIME.CONTROLLER_DIRECTORY_FULL";
  case CommandSinkErrorCode::kControllerKindRejected:
    return "RUNTIME.CONTROLLER_KIND_REJECTED";
  case CommandSinkErrorCode::kDisplayNameRejected:
    return "RUNTIME.DISPLAY_NAME_REJECTED";
  case CommandSinkErrorCode::kControllerIdExhausted:
    return "RUNTIME.CONTROLLER_ID_EXHAUSTED";
  }
  return "RUNTIME.COMMAND_SINK_ERROR_INVALID";
}

// canonical: command_sink_error -- the one refusal `CommandSink::open_session` can produce.
//
// Opening a session is the sink's only operation that can fail without producing a value: `submit`
// and `close_session` both return a total result and never throw, because a command source is a
// network session and a hard failure would let one client stop the match. Opening is different --
// there is no `ControllerId` to hand back -- so it throws a named, coded error the connection
// handler translates into a close code.
// related: command_sink.hpp -- the capability that throws this.
class CommandSinkError final : public std::runtime_error {
public:
  CommandSinkError(CommandSinkErrorCode code, std::string context, const std::string& detail);

  [[nodiscard]] CommandSinkErrorCode error_code() const noexcept { return code_; }
  [[nodiscard]] std::string_view code() const noexcept {
    return command_sink_error_code_name(code_);
  }
  [[nodiscard]] const std::string& context() const& noexcept { return context_; }
  [[nodiscard]] const std::string& context() const&& = delete;

private:
  CommandSinkErrorCode code_;
  std::string context_;
};

} // namespace blob_royale::runtime

#endif
