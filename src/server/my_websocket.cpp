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



#include "my_websocket.hpp"

#include "game_engine.hpp"


//void websocket_session::send_game_state(std::string game_state)
//{
//
//  write_buffer.clear();
//  std::string raw_data = GameEngine::get_instance()->game_info_serialized();
//
//  boost::beast::ostream(write_buffer) << raw_data;
//  ws_.async_write(
//      write_buffer.data(),
//      beast::bind_front_handler(
//          &websocket_session::on_write,
//          shared_from_this()));
//
//}

void websocket_session::on_accept(beast::error_code ec)
{
    if(ec)
        return fail(ec, "accept");

    // Read a message
    do_read();
}

void websocket_session::do_read()
{
    // Read a message into our buffer
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &websocket_session::on_read,
            shared_from_this()));
}

void websocket_session::on_read(
    beast::error_code ec,
    std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    // This indicates that the websocket_session was closed
    if(ec == websocket::error::closed)
        return;

    TRACE << "attempting websocket read";
    if(ec)
        fail(ec, "read");

    // Echo the message
    ws_.text(ws_.got_text());

    write_buffer.clear();
    std::string raw_data = GameEngine::get_instance()->game_info_serialized();

    boost::beast::ostream(write_buffer) << raw_data;
    ws_.async_write(
        //buffer_.data(),
        write_buffer.data(),
        beast::bind_front_handler(
            &websocket_session::on_write,
            shared_from_this()));
}

void websocket_session::on_write(
    beast::error_code ec,
    std::size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        return fail(ec, "write");

    // Clear the buffer
    buffer_.consume(buffer_.size());

    // Do another read
    do_read();
}













