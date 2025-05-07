
#include "boost-log.hpp"


void init_logging()
{
  logging::add_console_log(std::clog, keywords::format = "%TimeStamp%: %Message%");
}
