#ifndef _MY_LISTENER_HPP_
#define _MY_LISTENER_HPP_

#include "boost-log.hpp"

#include "game_engine_parameters.hpp"
#include "game_engine.hpp"

#include "helpers.hpp"
#include "my_http_server.hpp"



// Accepts incoming connections and launches the sessions
class listener : public std::enable_shared_from_this<listener>
{
  net::io_context& ioc_;
  tcp::acceptor acceptor_;
  std::shared_ptr<std::string const> doc_root_;

public:
  listener(
      net::io_context& ioc,
      tcp::endpoint endpoint,
      std::shared_ptr<std::string const> const& doc_root);
      
  

  // Start accepting incoming connections
  void
  run();
  
private:
  void
  do_accept();
  
  void
  on_accept(beast::error_code ec, tcp::socket socket);
  
};



#endif
