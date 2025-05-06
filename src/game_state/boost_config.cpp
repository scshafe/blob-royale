
#include "boost_config.hpp"

void init_logging()
{
  logging::add_console_log(std::clog, keywords::format = "%TimeStamp%: %Message%");
}
