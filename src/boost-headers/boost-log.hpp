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
  verbose,
  normal,
  caught_exception,
  warning,
  error,
  fatal
};

extern src::severity_logger< severity_level > lg;


void init_logging();

template<class... Types>
void manual_log(severity_level lvl, Types... args)
{
  logging::record rec = lg.open_record(keywords::severity = lvl);
  if (rec)
  {
    logging::record_ostream strm(rec);
    
    (strm << ... << args);

    strm.flush();
    lg.push_record(boost::move(rec));
  }
}

template<class... Types>
void verbose_log(Types... args)
{
  manual_log(severity_level::verbose, args...);
}


#define MY_LOG() 

#endif
