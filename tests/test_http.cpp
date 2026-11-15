#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <string>
#include <thread>

#include "http_server.h"
#include "test_util.h"

using se::HttpRequest;
using se::HttpResponse;

namespace {

std::string round_trip(std::uint16_t port, const std::string& request) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return "";
    }
    ::send(fd, request.data(), request.size(), 0);
    std::string response;
    char buffer[4096];
    while (true) {
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        response.append(buffer, static_cast<std::size_t>(n));
    }
    ::close(fd);
    return response;
}

}

TEST(http_url_decode) {
    CHECK_EQ(se::url_decode("hello+world"), std::string("hello world"));
    CHECK_EQ(se::url_decode("a%20b%2Fc"), std::string("a b/c"));
    CHECK_EQ(se::url_decode("100%"), std::string("100%"));
    CHECK_EQ(se::url_decode("%zz%4"), std::string("%zz%4"));
    CHECK_EQ(se::url_decode("%41%42"), std::string("AB"));
    CHECK_EQ(se::url_decode(""), std::string(""));
}

TEST(http_parse_request_extracts_path_and_query) {
    HttpRequest req;
    CHECK(se::parse_request("GET /api/search?q=machine+learning&k=5&flag HTTP/1.1\r\nHost: x\r\n\r\n", req));
    CHECK_EQ(req.method, std::string("GET"));
    CHECK_EQ(req.path, std::string("/api/search"));
    CHECK_EQ(req.query.at("q"), std::string("machine learning"));
    CHECK_EQ(req.query.at("k"), std::string("5"));
    CHECK_EQ(req.query.at("flag"), std::string(""));
}

TEST(http_parse_request_rejects_malformed_lines) {
    HttpRequest req;
    CHECK(!se::parse_request("", req));
    CHECK(!se::parse_request("GET\r\n\r\n", req));
    CHECK(!se::parse_request("GET /x\r\n\r\n", req));
    CHECK(!se::parse_request("GET x HTTP/1.1\r\n\r\n", req));
    CHECK(!se::parse_request("GET /x HTTP/2.0\r\n\r\n", req));
    CHECK(!se::parse_request("GET /a b HTTP/1.1\r\n\r\n", req));
}

TEST(http_json_escape) {
    CHECK_EQ(se::json_escape("plain"), std::string("plain"));
    CHECK_EQ(se::json_escape("a\"b\\c\n\t"), std::string("a\\\"b\\\\c\\n\\t"));
    CHECK_EQ(se::json_escape(std::string("\x01", 1)), std::string("\\u0001"));
    CHECK_EQ(se::json_escape("</script>"), std::string("</script>"));
}

TEST(http_server_serves_requests_over_loopback) {
    se::HttpServer server([](const HttpRequest& req) {
        HttpResponse response;
        if (req.path == "/boom") throw std::runtime_error("boom");
        response.body = "path=" + req.path + " q=" + (req.query.count("q") ? req.query.at("q") : "");
        return response;
    });
    const std::uint16_t port = server.listen("127.0.0.1", 0);
    CHECK(port != 0);
    std::thread worker([&] { server.serve(); });

    const std::string ok = round_trip(port, "GET /hello?q=a%20b HTTP/1.1\r\nHost: localhost\r\n\r\n");
    CHECK(ok.find("HTTP/1.1 200 OK") == 0);
    CHECK(ok.find("path=/hello q=a b") != std::string::npos);
    CHECK(ok.find("Content-Length: ") != std::string::npos);

    CHECK(round_trip(port, "POST /x HTTP/1.1\r\n\r\n").find("405") != std::string::npos);
    CHECK(round_trip(port, "garbage\r\n\r\n").find("400") != std::string::npos);
    CHECK(round_trip(port, "GET /boom HTTP/1.1\r\n\r\n").find("500") != std::string::npos);
    CHECK(round_trip(port, "GET /x HTTP/1.1\r\nX-Pad: " + std::string(20000, 'a') + "\r\n\r\n").find("431") !=
          std::string::npos);

    server.stop();
    worker.join();
}

TEST(http_listen_rejects_bad_address_and_port_in_use) {
    se::HttpServer first([](const HttpRequest&) { return HttpResponse{}; });
    CHECK_THROWS(first.listen("not-an-ip", 0), std::runtime_error);
    se::HttpServer second([](const HttpRequest&) { return HttpResponse{}; });
    const std::uint16_t port = second.listen("127.0.0.1", 0);
    se::HttpServer third([](const HttpRequest&) { return HttpResponse{}; });
    CHECK_THROWS(third.listen("127.0.0.1", port), std::runtime_error);
}
