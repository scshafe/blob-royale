#include "boost-beast.hpp"
#include "game_state.hpp"


class http_connection : public std::enable_shared_from_this<http_connection>
{
public:
    http_connection(tcp::socket socket, void (*start_sim_callback)());
    http_connection(tcp::socket socket);
    // Initiate the asynchronous operations associated with the connection.
    void start();
    
private:

  void (*start_sim)();

  // The socket for the currently connected client.
  tcp::socket socket_;

  // The buffer for performing reads.
  beast::flat_buffer buffer_{8192};

  // The request message.
  http::request<http::dynamic_body> request_;

  // The response message.
  http::response<http::dynamic_body> response_;

  // The timer for putting a deadline on connection processing.
  net::steady_timer deadline_{
      socket_.get_executor(), std::chrono::seconds(60)};

  // Asynchronously receive a complete request message.
  void read_request();
  
  // Determine what needs to be done with the request message.
  void process_request();
  
  // Construct a response message based on the program state.
  void create_response();
  
  // Asynchronously transmit the response message.
  void write_response();
  
  // Check whether we have spent enough time on this connection.
  void check_deadline();
};

// "Loop" forever accepting new connections.
void
http_server(tcp::acceptor& acceptor, tcp::socket& socket);


void start_server(std::string address_input, unsigned int port_input, void (*start_sim_callback)());

