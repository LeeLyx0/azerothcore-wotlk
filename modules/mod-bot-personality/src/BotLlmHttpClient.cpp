#include "BotLlmHttpClient.h"

#include "Timer.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace asio = boost::asio;
using tcp = asio::ip::tcp;

bool BotLlmHttpClient::ParseUrl(
    std::string const& endpoint,
    ParsedUrl& parsed) const
{
    std::size_t const schemeEnd = endpoint.find("://");
    if (schemeEnd == std::string::npos)
        return false;

    parsed.scheme = endpoint.substr(0, schemeEnd);
    std::string rest = endpoint.substr(schemeEnd + 3);

    std::size_t const pathStart = rest.find('/');
    std::string hostPort = pathStart == std::string::npos ?
        rest :
        rest.substr(0, pathStart);
    parsed.target = pathStart == std::string::npos ?
        "/" :
        rest.substr(pathStart);

    if (hostPort.empty())
        return false;

    std::size_t const at = hostPort.rfind('@');
    if (at != std::string::npos)
        hostPort = hostPort.substr(at + 1);

    std::size_t const colon = hostPort.rfind(':');
    if (colon != std::string::npos && colon + 1 < hostPort.size())
    {
        parsed.host = hostPort.substr(0, colon);
        parsed.port = hostPort.substr(colon + 1);
    }
    else
    {
        parsed.host = hostPort;
        parsed.port = parsed.scheme == "https" ? "443" : "80";
    }

    return !parsed.host.empty() && !parsed.target.empty();
}

BotLlmHttpResult BotLlmHttpClient::PostChatCompletion(
    BotLlmRequest const& request,
    std::string const& body) const
{
    BotLlmHttpResult result;
    uint32 const started = getMSTime();

    ParsedUrl url;
    if (!ParseUrl(request.endpoint, url))
    {
        result.error = "invalid endpoint";
        return result;
    }

    if (url.scheme != "http")
    {
        result.error = "only http endpoints are supported by this client";
        return result;
    }

    try
    {
        asio::io_context ioc;
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        uint32 const timeoutMs = request.requestTimeoutMs ?
            request.requestTimeoutMs :
            request.connectTimeoutMs;

        stream.expires_after(std::chrono::milliseconds(timeoutMs));
        auto const results = resolver.resolve(url.host, url.port);
        stream.connect(results);

        http::request<http::string_body> httpRequest{
            http::verb::post,
            url.target,
            11
        };
        httpRequest.set(http::field::host, url.host);
        httpRequest.set(http::field::user_agent, "mod-bot-personality");
        httpRequest.set(http::field::content_type, "application/json");
        if (!request.apiKey.empty())
        {
            httpRequest.set(
                http::field::authorization,
                "Bearer " + request.apiKey);
        }

        httpRequest.body() = body;
        httpRequest.prepare_payload();

        http::write(stream, httpRequest);

        beast::flat_buffer buffer;
        http::response<http::string_body> httpResponse;
        http::read(stream, buffer, httpResponse);

        beast::error_code ec;
        stream.socket().shutdown(tcp::socket::shutdown_both, ec);

        result.statusCode = static_cast<int32>(httpResponse.result_int());
        result.body = std::move(httpResponse.body());
        result.success = result.statusCode >= 200 && result.statusCode < 300;
        if (!result.success)
            result.error = "http failure";
    }
    catch (std::exception const& ex)
    {
        result.error = ex.what();
    }

    result.latencyMs = getMSTimeDiff(started, getMSTime());
    return result;
}
