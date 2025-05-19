#include <stdlib.h>
#include <fstream>
#include <cassert>

#include "boost-program-options.hpp"
#include "boost-log.hpp"


po::variables_map handle_configuration(int argc, char** argv)
{
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
    ("IPv4", po::value<std::string>()->default_value("192.168.86.12"), "set an IPv4 address")
    ("IPv6", po::value<std::string>(),                                 "set an IPv6 address")
    ("port", po::value<unsigned int>()->default_value(8000), "port")
    ("config", po::value<std::string>()->default_value("../config/blob-royale.cfg"), "configuration file with game engine parameters")
    ("testfile", po::value<std::string>()->default_value("../test/player-on-wall-collision-test"), "testfile with map and player information")
  ;

  TRACE << "Parsing command line options";

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);

  if (vm.count("help"))
  {
    std::cout << desc;
    exit(0);
  }

  po::options_description config("Configuration");
  config.add_options()
    ("game_constants.MAP_HEIGHT"            , po::value<int>()->default_value(800), "map height")
    ("game_constants.MAP_WIDTH"             , po::value<int>()->default_value(1200), "map width")
    ("game_constants.GAME_TICKS_PER_SECOND" , po::value<int>()->default_value(60), "number of game ticks per second")
    ("game_constants.PLAYER_RADIUS"         , po::value<float>()->default_value(10.0), "radius of players")
    ("game_constants.SPATIAL_PARTITION_COLS", po::value<int>()->default_value(8), "number of partitions used to divide the map widthwise")
    ("game_constants.SPATIAL_PARTITION_ROWS", po::value<int>()->default_value(8), "number of partitions used to divided the map heightwise")
    ("game_constants.WORKER_COUNT"          , po::value<int>()->default_value(1), "number of worker threads spawned to run the game engine")
    ;


  // ----- Initialize Config values -----
  po::store(po::parse_config_file(vm["config"].as<std::string>().c_str(), config), vm);
  po::notify(vm);

  TRACE << "vm[\"game_constants.GAME_TICKS_PER_SECOND\"]:  " << vm["game_constants.GAME_TICKS_PER_SECOND"].as<int>();



  return vm;
}


