
#ifndef _MY_SERVER_HPP_
#define _MY_SERVER_HPP_

#include <functional>

#include "boost-log.hpp"

#include "game_engine_parameters.hpp"
#include "game_engine.hpp"

#include "helpers.hpp"


// Echoes back all received WebSocket messages
class websocket_session : public std::enable_shared_from_this<websocket_session>
{
  websocket::stream<beast::tcp_stream> ws_;
  beast::flat_buffer buffer_;

  beast::flat_buffer write_buffer;

public:
  // Take ownership of the socket
  explicit
  websocket_session(tcp::socket&& socket)
      : ws_(std::move(socket))
  {
  }

  //void send_game_state(std::string game_state);

  // Start the asynchronous accept operation
  template<class Body, class Allocator>
  void
  do_accept(http::request<Body, http::basic_fields<Allocator>> req)
  {
      // Set suggested timeout settings for the websocket
      ws_.set_option(
          websocket::stream_base::timeout::suggested(
              beast::role_type::server));

      // Set a decorator to change the Server of the handshake
      ws_.set_option(websocket::stream_base::decorator(
          [](websocket::response_type& res)
          {
              res.set(http::field::server,
                  "blob-royale");
          }));

      // Accept the websocket handshake
      ws_.async_accept(
          req,
          beast::bind_front_handler(
              &websocket_session::on_accept,
              shared_from_this()));
  }

private:
  void
  on_accept(beast::error_code ec);
  
  void
  do_read();
  
  void
  on_read(
      beast::error_code ec,
      std::size_t bytes_transferred);
  
  void
  on_write(
      beast::error_code ec,
      std::size_t bytes_transferred);
    
};


#endif
