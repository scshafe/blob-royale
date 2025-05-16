
#include "boost-log.hpp"
#include "boost-program-options.hpp"

#include "game_engine_parameters.hpp"
#include "game_engine.hpp"

#include "my_listener.hpp"


void start_my_server(std::string address_input, unsigned int port, unsigned int threads, const char* doc_root_input)
{
    auto const address = net::ip::make_address(address_input);
    //auto const port = static_cast<unsigned short>(std::atoi(argv[2]));
    TRACE << "YEET";
    
    auto const doc_root = std::make_shared<std::string>(doc_root_input);
    // The io_context is required for all I/O
    net::io_context ioc{threads};


    TRACE << "2";
    // Create and launch a listening port
    std::make_shared<listener>(
        ioc,
        tcp::endpoint{address, port},
        doc_root)->run();

    TRACE << "3";
    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&](beast::error_code const&, int)
        {
            // Stop the `io_context`. This will cause `run()`
            // to return immediately, eventually destroying the
            // `io_context` and all of the sockets in it.
            ioc.stop();
        });

    TRACE << "4";
    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(threads - 1);
    for(auto i = threads - 1; i > 0; --i)
        v.emplace_back(
        [&ioc]
        {
            ioc.run();
        });
    ioc.run();

    // (If we get here, it means we got a SIGINT or SIGTERM)

    // Block until all the threads exit
    for(auto& t : v)
        t.join();

//    return EXIT_SUCCESS;
}

// ---------------------------------------------------

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

  GameEngine* gs = GameEngine::get_instance();
  gs->initialize(vm["testfile"].as<std::string>());

  
  const auto server_threads = 2;
  //auto const doc_root_str = std::make_shared<std::string>("/home/cole/github-projects/blob/royale/build");
  //auto const threads = std::max<int>(1, std::atoi(argv[4]));
  //auto const doc_root = std::make_shared<std::string>(argv[3]);
  std::string address = vm["IPv4"].as<std::string>();
  unsigned int port = vm["port"].as<unsigned int>();
  //std::string doc_root_string = vm["doc_root"].as<std::string>();
  const char* doc_root = "/home/cole/github-projects/blob-royale/build";
  int threads = 2;
  start_my_server(address, port, threads, doc_root);

  return EXIT_FAILURE;
}
