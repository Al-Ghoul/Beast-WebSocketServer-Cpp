
#include "boost/asio.hpp"
#include "boost/beast.hpp"
#include "boost/json.hpp"
#include "boost/log/trivial.hpp"
#include <jwt-cpp/jwt.h>

#include <iostream>
#include <vector>
#include <algorithm>
#include <ranges>
#include <unordered_map>
//postgresSQL library
//#include <pqxx/pqxx>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

using stream = websocket::stream<
	typename beast::tcp_stream::rebind_executor<
	typename net::use_awaitable_t<>::executor_with_default<net::any_io_executor>>::other>;

std::unordered_map<std::string, stream*> clients;


std::string VerifyUser(std::string const& token)
{
	constexpr auto ISSUER = "localhost";
	constexpr auto SECRET = "VkhD1O7+gQKrOiJo0qnruj7DLHeunESj7RK7IvOZAGs=";

	try
	{
		auto decoded = jwt::decode(token);
		auto verifier = jwt::verify()
			.allow_algorithm(jwt::algorithm::hs256{ SECRET })
			.with_issuer(ISSUER);

		std::error_code verify_ec;
		verifier.verify(decoded, verify_ec);
		if (verify_ec)
			BOOST_LOG_TRIVIAL(error) << verify_ec.message() << '\n';		

		return decoded.get_payload();
	}
	catch (...)
	{
		BOOST_LOG_TRIVIAL(error) << "An error occurred decoding the token\n";
		throw;
	}

}

net::awaitable<void> handle_session(stream ws)
{
	ws.set_option(
		websocket::stream_base::timeout::suggested(
			beast::role_type::server));
	ws.set_option(websocket::stream_base::decorator(
		[](websocket::response_type& res)
		{
			res.set(http::field::server,
			std::string(BOOST_BEAST_VERSION_STRING) +
			" websocket-server-coro");
		}));

	co_await ws.async_accept();

	BOOST_LOG_TRIVIAL(info) << "~Accepted a new connection~\n";

	beast::flat_buffer buffer;

	co_await ws.async_read(buffer);
	ws.text(ws.got_text());
	BOOST_LOG_TRIVIAL(info) << "~Received a User's JWT~\n";
	std::string userInfo;

	try
	{
		BOOST_LOG_TRIVIAL(info) << "~Verifying User's JWT~\n";
		userInfo = VerifyUser(beast::buffers_to_string(buffer.data()));
		BOOST_LOG_TRIVIAL(info) << userInfo << '\n';
	}
	catch (...)
	{
		BOOST_LOG_TRIVIAL(info) << "~Forced a Disconnection on a user due to JWT verfication failure~\n";
		std::erase_if(clients, [&](auto client) {return client.second == &ws; });
		ws.async_close(websocket::close_code::bad_payload);
	}

	BOOST_LOG_TRIVIAL(info) << "~User's JWT verified succuessfully~\n";

	boost::json::object actionObject;
	beast::multi_buffer write_buffer;
	boost::json::error_code parse_ec;
	boost::json::value parsedJSON = boost::json::parse(userInfo, parse_ec);

	if (parse_ec)
	{
		BOOST_LOG_TRIVIAL(error) << parse_ec.what();
		co_await ws.async_close(websocket::close_code::bad_payload);
	}

	std::string user_name = parsedJSON.as_object()["name"].as_string().data();
	actionObject["type"] = "USER_JOIN";
	actionObject["userData"].emplace_object()["name"] = user_name;

	boost::beast::ostream(write_buffer) << boost::json::serialize(actionObject);
	BOOST_LOG_TRIVIAL(info) << "~Notifying other users with the new user's presence~\n";

	for (auto const& client : clients)
		co_await client.second->async_write(write_buffer.data());

	for (auto const& client : clients) {
		write_buffer.clear();
		actionObject["userData"].emplace_object()["name"] = client.first;
		boost::beast::ostream(write_buffer) << boost::json::serialize(actionObject);
		co_await ws.async_write(write_buffer.data());
	}

	clients[user_name] = &ws;

	BOOST_LOG_TRIVIAL(info) << "~Waiting for incoming messages~\n";

	for (;;)
		try
	{
		buffer.clear();
		co_await ws.async_read(buffer);
		ws.text(ws.got_text());

		parsedJSON = boost::json::parse(beast::buffers_to_string(buffer.data()), parse_ec);

		if (parse_ec)
		{
			std::cout << "Err here\n";
			BOOST_LOG_TRIVIAL(error) << parse_ec.message();
			co_await ws.async_close(websocket::close_code::bad_payload);
		}

		boost::json::object messageObject = parsedJSON.as_object();
		auto const action = messageObject["action"].as_string();
	
		if (action == "SEND_MESSAGE") {
			auto const userMessage = messageObject["message"].as_string();
			actionObject.clear();
			actionObject["type"] = "USER_MESSAGE";
			actionObject["body"].emplace_object()["message"] = userMessage;

			BOOST_LOG_TRIVIAL(info) << "~A user sent a SEND_MESSAGE with message: " << userMessage << "\n~";

			write_buffer.clear();
			boost::beast::ostream(write_buffer) << boost::json::serialize(actionObject);

			BOOST_LOG_TRIVIAL(info) << "~Broadcasting the last message for everyone connected\n~";
			for (auto const& client : clients) {
				if (client.second == &ws) continue;
				co_await client.second->async_write(write_buffer.data());
			}
		}	
	}
	catch (boost::system::system_error& se)
	{
		BOOST_LOG_TRIVIAL(error) << "Error Code: " << se.code() << '\n';

		if (se.code() != websocket::error::closed)
		{
			BOOST_LOG_TRIVIAL(error) << se.what() << '\n';
			std::erase_if(clients, [&](auto client) {return client.second == &ws; });
			throw;
		}
	}
}


net::awaitable<void> do_listen(tcp::endpoint endpoint)
{
	auto acceptor = net::use_awaitable.as_default_on(tcp::acceptor(co_await net::this_coro::executor));
	acceptor.open(endpoint.protocol());
	acceptor.set_option(net::socket_base::reuse_address(true));
	acceptor.bind(endpoint);
	acceptor.listen(net::socket_base::max_listen_connections);

	auto sessionCallback = [](std::exception_ptr e)
	{
		try
		{
			std::rethrow_exception(e);
		}
		catch (std::exception& e) {
			BOOST_LOG_TRIVIAL(error) << "Error in session: " << e.what() << '\n';

		}
	};

	for (;;)
		boost::asio::co_spawn(
			acceptor.get_executor(),
			handle_session(stream(co_await acceptor.async_accept())),
			sessionCallback);
}


int main()
{
	try
	{
		net::io_context io_ctx(1);
		net::signal_set signals(io_ctx, SIGINT, SIGTERM);
		signals.async_wait([&](auto, auto) { io_ctx.stop(); });
		tcp::endpoint endpoint({ tcp::v4(), 1973 });
		net::co_spawn(io_ctx, do_listen(endpoint), net::detached);

		io_ctx.run();
	}
	catch (std::exception& e)
	{
		std::printf("Exception: %s\n", e.what());
	}

	//try
	//{
	//	pqxx::connection connection{ "postgresql://postgres:somePassword@localhost/SinagramDB" };
	//	pqxx::work txn{ connection };
	//	txn.exec0(
	//		"UPDATE  public.\"User\" "
	//		"SET username = \'AlGhoul\' "
	//		"WHERE id = 1");

	//	txn.commit();
	//}

	//catch (std::exception const& e)
	//{
	//	std::cout << e.what() << '\n';
	//}
}