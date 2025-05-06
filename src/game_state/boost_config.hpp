#ifndef _MY_BOOST_CONFIG_HPP_
#define _MY_BOOST_CONFIG_HPP_

// -------- BEAST ---------

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

namespace beast = boost::beast;         // from <boost/beast.hpp>
namespace http = beast::http;           // from <boost/beast/http.hpp>
namespace net = boost::asio;            // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;       // from <boost/asio/ip/tcp.hpp>



// --------- PROGRAM OPTIONS

#include <boost/program_options.hpp>

namespace po = boost::program_options;



// --------- LOG ----------

#include <boost/log/trivial.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sources/severity_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/support/date_time.hpp>

namespace logging = boost::log;
namespace src = boost::log::sources;
namespace expr = boost::log::expressions;
namespace keywords = boost::log::keywords;

void init_logging();


// ---------- JSON ----------

#include <boost/json.hpp>



// ---------- GAME CONSTANTS ----------

#include <cmath>

#define SPATIAL_PARTITION_COUNT 25 // should be a square for 2d partition array indexing
#define SPATIAL_PARTITION_COL_COUNT sqrt(SPATIAL_PARTITION_COUNT)
#define MAP_WIDTH 1200
#define MAP_HEIGHT 800
#define PLAYER_RADIUS 10
#define GAME_TICKS_PER_SEC 5

#define PLAYER_ON_PLAYER_COLLISION PLAYER_RADIUS * 2
#define PLAYER_ON_WALL_COLLISION PLAYER_RADIUS

#define PARTITION_WIDTH MAP_WIDTH / SPATIAL_PARTITION_COUNT
#define PARTITION_HEIGHT MAP_HEIGHT / SPATIAL_PARTITION_COUNT
#define GAME_TICK_PERIOD_SEC 1 / GAME_TICKS_PER_SEC
#define GAME_TICK_PERIOD_US GAME_TICK_PERIOD_SEC * 1000000

#define GET_PARTITION_INDEX_FROM_COORDINATES(x,y) floor(x / PARTITION_WIDTH) + SPATIAL_PARTITION_COL_COUNT * floor(y / PARTITION_HEIGHT)


#endif
