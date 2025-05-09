#include <thread>

#include "boost-log.hpp"
#include "boost-program-options.hpp"

#include "game_engine_parameters.hpp"
#include "game_state.hpp"

#include "server.hpp"



int main(int argc, char** argv)
{
  // setup logging with boost
  init_logging();

  // get command line and configuration file variable map
  po::variables_map vm = handle_configuration(argc, argv);

  // initialize game constants
  initialize_constants(vm["game_constants.MAP_HEIGHT"].as<int>(),
                          vm["game_constants.MAP_WIDTH"].as<int>(),
                          vm["game_constants.GAME_TICKS_PER_SECOND"].as<int>(),
                          vm["game_constants.PLAYER_RADIUS"].as<float>(),
                          vm["game_constants.SPATIAL_PARTITION_COLS"].as<int>(),
                          vm["game_constants.SPATIAL_PARTITION_ROWS"].as<int>()
                          );

  // initialize the game state engine

  GameState* gs = GameState::get_instance();
  gs->initialize(vm["testfile"].as<std::string>());

  std::thread simulation(*GameState::get_instance());
  
  std::string address = vm["IPv4"].as<std::string>();
  unsigned int port = vm["port"].as<unsigned int>();
  start_server(address, port);

  return EXIT_FAILURE;
}
