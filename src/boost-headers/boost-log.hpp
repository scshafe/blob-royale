#ifndef _MY_BOOST_LOG_HPP_
#define _MY_BOOST_LOG_HPP_

#include <cassert>

// --------- LOG ----------

#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>

#include <boost/log/attributes/attribute.hpp>
#include <boost/log/attributes/attribute_cast.hpp>
#include <boost/log/attributes/attribute_value.hpp>

namespace logging = boost::log;
namespace sinks = boost::log::sinks;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;
namespace attrs = boost::log::attributes;

enum severity_level
{
  entrance,
  lock,
  trace,
  caught_exception,
  warning,
  error
};

extern src::severity_logger< severity_level > lg;


void init_logging();

#define ENTRANCE  BOOST_LOG_SEV(lg, severity_level::entrance)
#define TRACE     BOOST_LOG_SEV(lg, severity_level::trace)
#define LOGEXCEPT BOOST_LOG_SEV(lg, severity_level::caught_exception)
#define WARNING   BOOST_LOG_SEV(lg, severity_level::warning)
#define ERROR     BOOST_LOG_SEV(lg, severity_level::error)

#define LOG BOOST_LOG_TRIVIAL(severity_level::trace)

// weird that this doesn't work but putting it in manually (in boost-program-options lets it print
//#define LOG BOOST_LOG_SEV(lg, severity_level::verbose)

#endif
