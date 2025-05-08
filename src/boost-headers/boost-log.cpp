
#include "boost-log.hpp"


/* Notes on ansi formatting found here:
 *
 * https://gist.github.com/fnky/458719343aabd01cfb17a3a4f7296797
 *
 * The formatting for graphics follows the syntax:
 *
 * `ESC[1;34;{...}m`
 *
 * - ESC is the ecsape sequence also denoted: 
 *   - note that this sequence is typed into the terminal using: Ctrl-v Ctrl-[
 *   - it is merely represented using the characters '^['
 *   - thus it is not copy/pastable
 *   - to get around this hiccup, you may also use:
 *      - `\e`   (a special character reference similar to `\n`)
 *      - `\033` (octal)
 *      - \\x1b  (hex)
 *      - \u001b (unicode)
 * 
 *
 */


BOOST_LOG_ATTRIBUTE_KEYWORD(severity, "Severity", severity_level);

src::severity_logger< severity_level > lg;



void coloring_formatter(
    logging::record_view const& rec, logging::formatting_ostream& strm)
{
  auto severity = boost::log::extract<severity_level>("Severity", rec);
  if (severity)
  {
    switch (severity.get())
    {
      case severity_level::verbose: // light green
      strm << "\e[38:5:41m";
      break;
      case severity_level::normal: // darker green
      strm << "\e[38:5:82m";
      break;
    case severity_level::caught_exception: // white
      strm << "\033[38:5:231m";
      break;
    case severity_level::warning: // orange
      strm << "\033[38:5:202m";
      break;
    case severity_level::error: // red
      strm << "\e[38:5:196m";
      break;
    case severity_level::fatal: // bold and red
      strm << "\033[1:38:5:196m";
      break;
    default:
        break;
    }
  }
  auto scope = boost::log::extract<attrs::named_scope::value_type>("Scope", rec);
  if (scope)
  {
    strm << scope;
  }

  // Format the message here...
  strm << rec[logging::expressions::smessage];

  if (severity)
  {
      // Restore the default color
      strm << "\033[0m";
  }
}

void init_logging()
{

  typedef sinks::synchronous_sink< sinks::text_ostream_backend > text_sink;
  boost::shared_ptr< text_sink > sink = boost::make_shared< text_sink >();

  sink->locked_backend()->add_stream(
    boost::shared_ptr< std::ostream >(&std::cout)
  );

  sink->set_formatter(&coloring_formatter);


  boost::shared_ptr< logging::core > core = logging::core::get();
  core->add_sink(sink);

  core->add_global_attribute("Scope", attrs::named_scope());
}



