#include "command_sink_error.hpp"

#include <string>
#include <utility>

namespace blob_royale::runtime {

CommandSinkError::CommandSinkError(const CommandSinkErrorCode code, std::string context,
                                   const std::string& detail)
    : std::runtime_error(std::string(command_sink_error_code_name(code)) + " at " + context + ": " +
                         detail),
      code_(code), context_(std::move(context)) {}

} // namespace blob_royale::runtime
