#ifndef _HELPERS_HPP_
#define _HELPERS_HPP_


#include "boost-log.hpp"

#include "game_engine_parameters.hpp"
#include "game_engine.hpp"




#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/signal_set.hpp>
#include <boost/asio/strand.hpp>
#include <boost/make_unique.hpp>
#include <boost/optional.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;                 // from <boost/beast.hpp>
namespace http = beast::http;                   // from <boost/beast/http.hpp>
namespace websocket = beast::websocket;         // from <boost/beast/websocket.hpp>
namespace net = boost::asio;                    // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp;               // from <boost/asio/ip/tcp.hpp>






// Return a reasonable mime type based on the extension of a file.
beast::string_view
mime_type(beast::string_view path);

// Append an HTTP rel-path to a local filesystem path.
// The returned path is normalized for the platform.
std::string
path_cat(
    beast::string_view base,
    beast::string_view path);

// Return a response for the given request.
//
// The concrete type of the response message (which depends on the
// request), is type-erased in message_generator.
template <class Body, class Allocator>
http::message_generator
handle_request(
    beast::string_view doc_root,
    http::request<Body, http::basic_fields<Allocator>>&& req)
{
    // Returns a bad request response
//    auto const bad_request =
//    [&req](beast::string_view why)
//    {
//        http::response<http::string_body> res{http::status::bad_request, req.version()};
//        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        res.set(http::field::content_type, "text/html");
//        res.keep_alive(req.keep_alive());
//        res.body() = std::string(why);
//        res.prepare_payload();
//        return res;
//    };
//
//    // Returns a not found response
//    auto const not_found =
//    [&req](beast::string_view target)
//    {
//        http::response<http::string_body> res{http::status::not_found, req.version()};
//        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        res.set(http::field::content_type, "text/html");
//        res.keep_alive(req.keep_alive());
//        res.body() = "The resource '" + std::string(target) + "' was not found.";
//        res.prepare_payload();
//        return res;
//    };
//
//    // Returns a server error response
//    auto const server_error =
//    [&req](beast::string_view what)
//    {
//        http::response<http::string_body> res{http::status::internal_server_error, req.version()};
//        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        res.set(http::field::content_type, "text/html");
//        res.keep_alive(req.keep_alive());
//        res.body() = "An error occurred: '" + std::string(what) + "'";
//        res.prepare_payload();
//        return res;
//    };
//
//    // Make sure we can handle the method
//    if( req.method() != http::verb::get &&
//        req.method() != http::verb::head)
//        return bad_request("Unknown HTTP-method");
//if(req.method() == http::verb::head)
//    {
//        http::response<http::empty_body> res{http::status::ok, req.version()};
//        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        res.set(http::field::content_type, mime_type(path));
//        res.content_length(size);
//        res.keep_alive(req.keep_alive());
//        return res;
//    }
  http::response<http::dynamic_body> res{http::status::ok, req.version()};

  if (req.target() == "/game-config")
    {
      BOOST_LOG_TRIVIAL(info) << "game-config";
      res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(http::field::content_type, "application/json");
      res.set(http::field::access_control_allow_origin, "*");
      res.set(http::field::access_control_allow_headers, "*");
      beast::ostream(res.body())
      << GameEngine::get_instance()->game_config();
 //res.set(http::field::content_type, mime_type(path));
        //res.content_length(size);
        res.keep_alive(req.keep_alive());
    }
     else
     {
        res.result(http::status::not_found);
        res.set(http::field::content_type, "text/plain");
        beast::ostream(res.body()) << "File not found\r\n";
     }      
        res.content_length(res.body().size());
        return res;
    // Request path must be absolute and not contain "..".
//    else if( req.target().empty() ||
//        req.target()[0] != '/' ||
//        req.target().find("..") != beast::string_view::npos )
//        return bad_request("Illegal request-target");
//
//    // Build the path to the requested file
//    std::string path = path_cat(doc_root, req.target());
//    if(req.target().back() == '/')
//        path.append("index.html");
//
//    // Attempt to open the file
//    beast::error_code ec;
//    http::file_body::value_type body;
//    body.open(path.c_str(), beast::file_mode::scan, ec);
//
//    // Handle the case where the file doesn't exist
//    if(ec == beast::errc::no_such_file_or_directory)
//        return not_found(req.target());
//
//    // Handle an unknown error
//    if(ec)
//        return server_error(ec.message());
//
//    // Cache the size since we need it after the move
//    auto const size = body.size();
//
//    // Respond to HEAD request
//    if(req.method() == http::verb::head)
//    {
//        http::response<http::empty_body> res{http::status::ok, req.version()};
//        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//        res.set(http::field::content_type, mime_type(path));
//        res.content_length(size);
//        res.keep_alive(req.keep_alive());
//        return res;
//    }
//
//    // Respond to GET request
//    http::response<http::file_body> res{
//        std::piecewise_construct,
//        std::make_tuple(std::move(body)),
//        std::make_tuple(http::status::ok, req.version())};
//    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
//    res.set(http::field::content_type, mime_type(path));
//    res.content_length(size);
//    res.keep_alive(req.keep_alive());
//
//    if (req.target() == "/game-config")
//    {
//      BOOST_LOG_TRIVIAL(info) << "game-config endpoint received";
//      res.set(http::field::content_type, "application/json");
//      res.set(http::field::access_control_allow_origin, "*");
//      res.set(http::field::access_control_allow_headers, "*");
//      beast::ostream(res.body())
//      << GameEngine::get_instance()->game_config();
//    }
//    else if (req.target() == "/game-state")
//    {
//      BOOST_LOG_TRIVIAL(info) << "game-state endpoint received";
//      res.set(http::field::content_type, "application/json");
//      res.set(http::field::access_control_allow_origin, "*");
//      res.set(http::field::access_control_allow_headers, "*");
//      beast::ostream(res.body())
//      << GameEngine::get_instance()->game_info();
//    }
//    else if (req.target() == "/start-sim")
//    {
//      BOOST_LOG_TRIVIAL(info) << "game-state endpoint received";
//      res.set(http::field::content_type, "test/plain");
//      res.set(http::field::access_control_allow_origin, "*");
//      res.set(http::field::access_control_allow_headers, "*");
//      beast::ostream(res.body())
//      << "Starting sim\r\n";
//      GameEngine::get_instance()->start_sim();
//    }
//    else if (req.target() == "/pause-sim")
//    {
//      BOOST_LOG_TRIVIAL(info) << "game-state endpoint received";
//      res.set(http::field::content_type, "test/plain");
//      res.set(http::field::access_control_allow_origin, "*");
//      res.set(http::field::access_control_allow_headers, "*");
//      beast::ostream(res.body())
//      << "Stopping sim\r\n";
//      GameEngine::get_instance()->pause_sim();
//    }
    res.content_length(res.body().size());


    return res;
}

//------------------------------------------------------------------------------

// Report a failure
void
fail(beast::error_code ec, char const* what);


#endif
