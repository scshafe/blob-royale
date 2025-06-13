
#include "game-engine.hpp"


int main()
{
  init_logging();


  po::variables_map vm = handle_configuration(argc, argv);


  // initialize game constants
  initialize_constants(vm["game_constants.MAP_HEIGHT"].as<int>(),
                          vm["game_constants.MAP_WIDTH"].as<int>(),
                          vm["game_constants.GAME_TICKS_PER_SECOND"].as<int>(),
                          vm["game_constants.PLAYER_RADIUS"].as<float>(),
                          vm["game_constants.SPATIAL_PARTITION_COLS"].as<int>(),
                          vm["game_constants.SPATIAL_PARTITION_ROWS"].as<int>(),
                          vm["game_constants.WORKER_COUNT"].as<int>()
                          );

  // initialize the game state engine

  GameEngine* gs = GameEngine::get_instance();
  gs->initialize(vm["testfile"].as<std::string>());

  gs->run_benchmark();  

  return EXIT_FAILURE;




}
