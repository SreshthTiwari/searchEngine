#include "web_api.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace se {

namespace {

constexpr std::size_t kMaxQueryBytes = 512;
constexpr std::size_t kMaxResults = 100;

HttpResponse json_response(int status, std::string body) {
    HttpResponse response;
    response.status = status;
    response.content_type = "application/json; charset=utf-8";
    response.body = std::move(body);
    return response;
}

HttpResponse json_error(int status, const std::string& message) {
    return json_response(status, "{\"error\":\"" + json_escape(message) + "\"}");
}

std::string number(double value, const char* format) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

bool parse_k(const std::string& text, std::size_t& k) {
    if (text.empty() || text.size() > 4) return false;
    std::size_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + static_cast<std::size_t>(c - '0');
    }
    if (value == 0 || value > kMaxResults) return false;
    k = value;
    return true;
}

}

HttpResponse WebApi::handle(const HttpRequest& request) {
    if (request.path == "/" || request.path == "/index.html") return page();
    if (request.path == "/api/search") return search(request);
    return json_error(404, "not found");
}

HttpResponse WebApi::page() const {
    std::ifstream in(page_path_, std::ios::binary);
    if (!in) return json_error(404, "web page not found at '" + page_path_ + "'");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    HttpResponse response;
    response.content_type = "text/html; charset=utf-8";
    response.body = buffer.str();
    return response;
}

HttpResponse WebApi::search(const HttpRequest& request) {
    const auto q = request.query.find("q");
    if (q == request.query.end()) return json_error(400, "missing query parameter 'q'");
    if (q->second.size() > kMaxQueryBytes) {
        return json_error(400, "query is longer than " + std::to_string(kMaxQueryBytes) + " bytes");
    }

    std::size_t k = 10;
    if (const auto it = request.query.find("k"); it != request.query.end()) {
        if (!parse_k(it->second, k)) {
            return json_error(400, "k must be an integer between 1 and " + std::to_string(kMaxResults));
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto start = std::chrono::steady_clock::now();
    const std::vector<SearchHit> hits = engine_.search_ids(q->second, k, Algorithm::Wand);
    const double micros =
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();

    const IndexCore& core = engine_.core();
    std::string body = "{\"query\":\"" + json_escape(q->second) + "\"";
    body += ",\"took_us\":" + number(micros, "%.1f");
    body += ",\"total_documents\":" + std::to_string(core.num_docs());
    body += ",\"results\":[";
    for (std::size_t i = 0; i < hits.size(); ++i) {
        const DocInfo& doc = core.docs[hits[i].doc];
        if (i > 0) body += ",";
        body += "{\"id\":\"" + json_escape(doc.external_id) + "\"";
        body += ",\"score\":" + number(hits[i].score, "%.4f");
        body += ",\"title\":\"" + json_escape(doc.title) + "\"";
        body += ",\"snippet\":\"" + json_escape(doc.snippet) + "\"}";
    }
    body += "]}";
    return json_response(200, body);
}

}
