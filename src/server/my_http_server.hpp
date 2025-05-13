#ifndef _MY_HTTP_SERVER_HPP_
#define _MY_HTTP_SERVER_HPP_

#include "helpers.hpp"


// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session>
{
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<std::string const> doc_root_;

    static constexpr std::size_t queue_limit = 8; // max responses
    std::queue<http::message_generator> response_queue_;

    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::dynamic_body>> parser_;

public:
    // Take ownership of the socket
    http_session(tcp::socket&& socket, std::shared_ptr<std::string const> const& doc_root);
        
    
    // Start the session
    void
    run();
    
private:
    void
    do_read();
    
    void
    on_read(beast::error_code ec, std::size_t bytes_transferred);
    
    void 
    queue_write_data(http::response<http::dynamic_body> res);
    
    void
    queue_write(http::message_generator response);
    
    // Called to start/continue the write-loop. Should not be called when
    // write_loop is already active.
    void
    do_write();
    
    void
    on_write(
        bool keep_alive,
        beast::error_code ec,
        std::size_t bytes_transferred);
    
    void
    do_close();
    
};



#endif
