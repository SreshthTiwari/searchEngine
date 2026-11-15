#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace se {

struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
};

struct HttpResponse {
    int status = 200;
    std::string content_type = "text/plain; charset=utf-8";
    std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

std::string url_decode(std::string_view encoded);
bool parse_request(std::string_view head, HttpRequest& out);
std::string json_escape(std::string_view text);

class HttpServer {
public:
    explicit HttpServer(HttpHandler handler);
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;
    ~HttpServer();

    std::uint16_t listen(const std::string& host, std::uint16_t port);
    void serve();
    void stop() { stopping_.store(true); }

private:
    void handle_connection(int fd);

    HttpHandler handler_;
    int listen_fd_ = -1;
    std::atomic<bool> stopping_{false};
    std::atomic<int> active_{0};
};

}
