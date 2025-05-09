#include "boost-log.hpp"

#include "server.hpp"



http_connection::http_connection(tcp::socket socket)
  : socket_(std::move(socket))
  {
  }


// Initiate the asynchronous operations associated with the connection.
void http_connection::start()
{
  read_request();
  check_deadline();
}

// Asynchronously receive a complete request message.
void http_connection::read_request()
{
  auto self = shared_from_this();

  http::async_read(
  socket_,
  buffer_,
  request_,
  [self](beast::error_code ec,
      std::size_t bytes_transferred)
  {
      boost::ignore_unused(bytes_transferred);
      if(!ec)
          self->process_request();
  });
}

// Determine what needs to be done with the request message.
void http_connection::process_request()
{
  response_.version(request_.version());
  response_.keep_alive(false);

  switch(request_.method())
  {
  case http::verb::get:
  response_.result(http::status::ok);
  response_.set(http::field::server, "Beast");
  create_response();
  break;

  default:
  // We return responses indicating an error if
  // we do not recognize the request method.
  response_.result(http::status::bad_request);
  response_.set(http::field::content_type, "text/plain");
  beast::ostream(response_.body())
      << "Invalid request-method '"
      << std::string(request_.method_string())
      << "'";
  break;
  }

  write_response();
}

// Construct a response message based on the program state.
void http_connection::create_response()
{
  if (request_.target() == "/game-state")
  {
  BOOST_LOG_TRIVIAL(info) << "game-state endpoint received";
  response_.set(http::field::content_type, "application/json");
  response_.set(http::field::access_control_allow_origin, "*");
  response_.set(http::field::access_control_allow_headers, "*");
  beast::ostream(response_.body())
  << GameState::get_instance()->game_info();
  }
  
  else
  {
  response_.result(http::status::not_found);
  response_.set(http::field::content_type, "text/plain");
  beast::ostream(response_.body()) << "File not found\r\n";
  }
}

// Asynchronously transmit the response message.
void http_connection::write_response()
{
  auto self = shared_from_this();

  response_.content_length(response_.body().size());

  http::async_write(
  socket_,
  response_,
  [self](beast::error_code ec, std::size_t)
  {
      self->socket_.shutdown(tcp::socket::shutdown_send, ec);
      self->deadline_.cancel();
  });
}

// Check whether we have spent enough time on this connection.
void http_connection::check_deadline()
{
  auto self = shared_from_this();

  deadline_.async_wait(
  [self](beast::error_code ec)
  {
      if(!ec)
      {
          // Close socket to cancel any outstanding operation.
          self->socket_.close(ec);
      }
  });
}


// "Loop" forever accepting new connections.
void http_server(tcp::acceptor& acceptor, tcp::socket& socket)
{
  acceptor.async_accept(socket,
  [&](beast::error_code ec)
  {
    if(!ec)
        std::make_shared<http_connection>(std::move(socket))->start();
    http_server(acceptor, socket);
  });
}


void start_server(std::string address_input, unsigned int port_input)
{
  try
  {
    BOOST_LOG_TRIVIAL(info) << "starting server at: " << address_input << ":" << port_input;
  
    auto const address = net::ip::make_address(address_input.c_str());
    unsigned short port = port_input;
  
    net::io_context ioc{1};
  
    tcp::acceptor acceptor{ioc, {address, port}};
    tcp::socket socket{ioc};
    http_server(acceptor, socket);
  
    ioc.run();
    }
    catch(std::exception const& e)
    {
    std::cerr << "Error: " << e.what() << std::endl;
    return;
  }
}

