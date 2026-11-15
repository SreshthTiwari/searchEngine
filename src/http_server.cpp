#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <thread>

namespace se {

namespace {

constexpr std::size_t kMaxHeaderBytes = 8192;
constexpr int kMaxActiveConnections = 64;
constexpr int kSocketTimeoutSeconds = 5;

int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

const char* status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

bool write_all(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, 0);
        if (n <= 0) return false;
        sent += static_cast<std::size_t>(n);
    }
    return true;
}

void send_response(int fd, const HttpResponse& response) {
    std::string out = "HTTP/1.1 " + std::to_string(response.status) + " " + status_text(response.status) + "\r\n";
    out += "Content-Type: " + response.content_type + "\r\n";
    out += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    out += "Connection: close\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    if (response.content_type.rfind("text/html", 0) == 0) {
        out += "Content-Security-Policy: default-src 'self'; style-src 'unsafe-inline'; script-src 'unsafe-inline'; "
               "connect-src 'self'; frame-ancestors 'none'\r\n";
    }
    out += "\r\n";
    out += response.body;
    write_all(fd, out);
}

HttpResponse plain_error(int status, const std::string& message) {
    HttpResponse response;
    response.status = status;
    response.body = message + "\n";
    return response;
}

}

std::string url_decode(std::string_view encoded) {
    std::string out;
    out.reserve(encoded.size());
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        const char c = encoded[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < encoded.size() && hex_value(encoded[i + 1]) >= 0 &&
                   hex_value(encoded[i + 2]) >= 0) {
            out.push_back(static_cast<char>(hex_value(encoded[i + 1]) * 16 + hex_value(encoded[i + 2])));
            i += 2;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

bool parse_request(std::string_view head, HttpRequest& out) {
    const std::size_t line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end);
    const std::size_t first_space = line.find(' ');
    if (first_space == std::string_view::npos) return false;
    const std::size_t second_space = line.find(' ', first_space + 1);
    if (second_space == std::string_view::npos) return false;

    const std::string_view method = line.substr(0, first_space);
    const std::string_view target = line.substr(first_space + 1, second_space - first_space - 1);
    const std::string_view version = line.substr(second_space + 1);
    if (method.empty() || target.empty() || target[0] != '/') return false;
    if (version.rfind("HTTP/1.", 0) != 0) return false;
    for (char c : target) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return false;
    }

    out = HttpRequest{};
    out.method = std::string(method);
    const std::size_t qmark = target.find('?');
    out.path = std::string(target.substr(0, qmark));
    if (qmark != std::string_view::npos) {
        std::string_view rest = target.substr(qmark + 1);
        while (!rest.empty()) {
            const std::size_t amp = rest.find('&');
            const std::string_view pair = rest.substr(0, amp);
            rest = amp == std::string_view::npos ? std::string_view{} : rest.substr(amp + 1);
            if (pair.empty()) continue;
            const std::size_t eq = pair.find('=');
            const std::string key = url_decode(pair.substr(0, eq));
            const std::string value = eq == std::string_view::npos ? std::string() : url_decode(pair.substr(eq + 1));
            out.query[key] = value;
        }
    }
    return true;
}

std::string json_escape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buf;
                } else {
                    out.push_back(c);
                }
        }
    }
    return out;
}

HttpServer::HttpServer(HttpHandler handler) : handler_(std::move(handler)) {}

HttpServer::~HttpServer() {
    stop();
    while (active_.load() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    if (listen_fd_ >= 0) ::close(listen_fd_);
}

std::uint16_t HttpServer::listen(const std::string& host, std::uint16_t port) {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) throw std::runtime_error("cannot create socket");
    const int enable = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));
#ifdef SO_NOSIGPIPE
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address '" + host + "'");
    }
    if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        throw std::runtime_error("cannot bind to " + host + ":" + std::to_string(port) +
                                 " (is the port already in use?)");
    }
    if (::listen(listen_fd_, 64) != 0) throw std::runtime_error("cannot listen on socket");

    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    return ntohs(addr.sin_port);
}

void HttpServer::serve() {
    while (!stopping_.load()) {
        pollfd p{listen_fd_, POLLIN, 0};
        const int ready = ::poll(&p, 1, 200);
        if (ready <= 0) continue;
        const int fd = ::accept(listen_fd_, nullptr, nullptr);
        if (fd < 0) continue;

        timeval timeout{kSocketTimeoutSeconds, 0};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
        const int enable = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enable, sizeof(enable));
#endif

        if (active_.load() >= kMaxActiveConnections) {
            send_response(fd, plain_error(503, "server busy"));
            ::close(fd);
            continue;
        }
        ++active_;
        try {
            std::thread([this, fd] {
                handle_connection(fd);
                ::close(fd);
                --active_;
            }).detach();
        } catch (...) {
            ::close(fd);
            --active_;
        }
    }
}

void HttpServer::handle_connection(int fd) {
    std::string head;
    char buffer[2048];
    while (head.find("\r\n\r\n") == std::string::npos) {
        if (head.size() > kMaxHeaderBytes) {
            send_response(fd, plain_error(431, "request headers too large"));
            return;
        }
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), 0);
        if (n == 0) return;
        if (n < 0) {
            send_response(fd, plain_error(408, "request timed out"));
            return;
        }
        head.append(buffer, static_cast<std::size_t>(n));
    }

    HttpRequest request;
    if (!parse_request(head, request)) {
        send_response(fd, plain_error(400, "malformed request"));
        return;
    }
    if (request.method != "GET") {
        send_response(fd, plain_error(405, "only GET is supported"));
        return;
    }
    try {
        send_response(fd, handler_(request));
    } catch (const std::exception&) {
        send_response(fd, plain_error(500, "internal error"));
    }
}

}
