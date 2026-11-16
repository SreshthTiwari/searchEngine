#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "index_builder.h"
#include "test_util.h"
#include "web_api.h"

using se::HttpRequest;

namespace {

se::SearchEngine make_engine() {
    return se::SearchEngine::from_index(se::build_index(
        {{"P1", "Intro", "Machine learning algorithms learn patterns from data"},
         {"P2", "", "Tomato soup with \"fresh\" basil <script>alert(1)</script>"}},
        se::BuildOptions{}));
}

HttpRequest get(const std::string& path, std::map<std::string, std::string> query = {}) {
    HttpRequest request;
    request.method = "GET";
    request.path = path;
    request.query = std::move(query);
    return request;
}

}

TEST(web_search_returns_json_results) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    const auto response = api.handle(get("/api/search", {{"q", "machine learning"}}));
    CHECK_EQ(response.status, 200);
    CHECK(response.content_type.find("application/json") == 0);
    CHECK(response.body.find("\"id\":\"P1\"") != std::string::npos);
    CHECK(response.body.find("\"title\":\"Intro\"") != std::string::npos);
    CHECK(response.body.find("\"took_us\":") != std::string::npos);
}

TEST(web_search_escapes_text_in_json) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    const auto response = api.handle(get("/api/search", {{"q", "tomato \"quoted\""}}));
    CHECK_EQ(response.status, 200);
    CHECK(response.body.find("\"query\":\"tomato \\\"quoted\\\"\"") != std::string::npos);
    CHECK(response.body.find("\\\"fresh\\\"") != std::string::npos);
}

TEST(web_search_unknown_terms_return_empty_results) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    const auto response = api.handle(get("/api/search", {{"q", "zzzunknown"}}));
    CHECK_EQ(response.status, 200);
    CHECK(response.body.find("\"results\":[]") != std::string::npos);
    CHECK_EQ(api.handle(get("/api/search", {{"q", ""}})).status, 200);
}

TEST(web_search_respects_k) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    const auto response = api.handle(get("/api/search", {{"q", "machine tomato"}, {"k", "1"}}));
    CHECK_EQ(response.status, 200);
    std::size_t hits = 0;
    for (std::size_t pos = response.body.find("\"id\":"); pos != std::string::npos;
         pos = response.body.find("\"id\":", pos + 1)) {
        ++hits;
    }
    CHECK_EQ(hits, std::size_t{1});
}

TEST(web_search_rejects_bad_parameters) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    CHECK_EQ(api.handle(get("/api/search")).status, 400);
    CHECK_EQ(api.handle(get("/api/search", {{"q", "x"}, {"k", "0"}})).status, 400);
    CHECK_EQ(api.handle(get("/api/search", {{"q", "x"}, {"k", "101"}})).status, 400);
    CHECK_EQ(api.handle(get("/api/search", {{"q", "x"}, {"k", "abc"}})).status, 400);
    CHECK_EQ(api.handle(get("/api/search", {{"q", "x"}, {"k", "-1"}})).status, 400);
    CHECK_EQ(api.handle(get("/api/search", {{"q", std::string(513, 'a')}})).status, 400);
}

TEST(web_unknown_paths_return_404) {
    auto engine = make_engine();
    se::WebApi api(engine, "unused.html");
    CHECK_EQ(api.handle(get("/nope")).status, 404);
    CHECK_EQ(api.handle(get("/../etc/passwd")).status, 404);
}

TEST(web_serves_page_from_disk) {
    const auto path = (std::filesystem::temp_directory_path() / "se_test_page.html").string();
    {
        std::ofstream out(path);
        out << "<!doctype html><title>t</title>";
    }
    auto engine = make_engine();
    se::WebApi api(engine, path);
    const auto response = api.handle(get("/"));
    CHECK_EQ(response.status, 200);
    CHECK(response.content_type.find("text/html") == 0);
    CHECK_EQ(response.body, std::string("<!doctype html><title>t</title>"));
    std::remove(path.c_str());

    se::WebApi missing(engine, "/nonexistent/page.html");
    CHECK_EQ(missing.handle(get("/")).status, 404);
}
