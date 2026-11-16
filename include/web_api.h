#pragma once

#include <mutex>
#include <string>

#include "http_server.h"
#include "search_engine.h"

namespace se {

class WebApi {
public:
    WebApi(SearchEngine& engine, std::string page_path) : engine_(engine), page_path_(std::move(page_path)) {}

    HttpResponse handle(const HttpRequest& request);

private:
    HttpResponse page() const;
    HttpResponse search(const HttpRequest& request);

    SearchEngine& engine_;
    std::string page_path_;
    std::mutex mutex_;
};

}
