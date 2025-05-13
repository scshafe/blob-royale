// -----------------------------------------------------

//
// Copyright (c) 2016-2019 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Official repository: https://github.com/boostorg/beast
//

//------------------------------------------------------------------------------
//
// Example: Advanced server
//
//------------------------------------------------------------------------------

// This is adapted from the advanced_server example of Boost Beast





#include "my_http_server.hpp"
#include "my_websocket.hpp"



    // Take ownership of the socket
http_session::http_session(
    tcp::socket&& socket,
    std::shared_ptr<std::string const> const& doc_root)
    : stream_(std::move(socket))
    , doc_root_(doc_root)
{
    static_assert(queue_limit > 0,
                  "queue limit must be positive");
}

// Start the session
void http_session::run()
{
    // We need to be executing within a strand to perform async operations
    // on the I/O objects in this session. Although not strictly necessary
    // for single-threaded contexts, this example code is written to be
    // thread-safe by default.
    net::dispatch(
        stream_.get_executor(),
        beast::bind_front_handler(
            &http_session::do_read,
            this->shared_from_this()));
}


void http_session::do_read()
{
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse.
    parser_->body_limit(10000);

    // Set the timeout.
    stream_.expires_after(std::chrono::seconds(30));

    // Read a request using the parser-oriented interface
    http::async_read(
        stream_,
        buffer_,
        *parser_,
        beast::bind_front_handler(
            &http_session::on_read,
            shared_from_this()));
}

void http_session::on_read(beast::error_code ec, std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This means they closed the connection
    if(ec == http::error::end_of_stream)
        return do_close();

    if(ec)
        return fail(ec, "read");

    // See if it is a WebSocket Upgrade
    if(websocket::is_upgrade(parser_->get()))
    {
        // Create a websocket session, transferring ownership
        // of both the socket and the HTTP request.
        std::make_shared<websocket_session>(
            stream_.release_socket())->do_accept(parser_->release());
        return;
    }

    // Send the response
    queue_write(handle_request(*doc_root_, parser_->release()));

    // If we aren't at the queue limit, try to pipeline another request
    if (response_queue_.size() < queue_limit)
        do_read();
}

void  http_session::queue_write_data(http::response<http::dynamic_body> res)
{

}

void http_session::queue_write(http::message_generator response)
{
    // Allocate and store the work
    response_queue_.push(std::move(response));

    // If there was no previous work, start the write loop
    if (response_queue_.size() == 1)
        do_write();
}

// Called to start/continue the write-loop. Should not be called when
// write_loop is already active.
void http_session::do_write()
{
    if(! response_queue_.empty())
    {
        bool keep_alive = response_queue_.front().keep_alive();

        beast::async_write(
            stream_,
            std::move(response_queue_.front()),
            beast::bind_front_handler(
                &http_session::on_write,
                shared_from_this(),
                keep_alive));
    }
}

void http_session::on_write(
    bool keep_alive,
    beast::error_code ec,
    std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        return fail(ec, "write");

    if(! keep_alive)
    {
        // This means we should close the connection, usually because
        // the response indicated the "Connection: close" semantic.
        return do_close();
    }

    // Resume the read if it has been paused
    if(response_queue_.size() == queue_limit)
        do_read();

    response_queue_.pop();

    do_write();
}

void http_session::do_close()
{
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);

    // At this point the connection is closed gracefully
}

