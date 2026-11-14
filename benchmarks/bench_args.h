#pragma once

#include <cerrno>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace bench {

class Args {
public:
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.rfind("--", 0) != 0) throw std::invalid_argument("unexpected argument '" + arg + "'");
            if (i + 1 >= argc) throw std::invalid_argument("option " + arg + " requires a value");
            values_[arg.substr(2)] = argv[++i];
        }
    }

    bool has(const std::string& name) const { return values_.count(name) > 0; }

    std::string require(const std::string& name) const {
        const auto it = values_.find(name);
        if (it == values_.end()) throw std::invalid_argument("missing required option --" + name);
        return it->second;
    }

    std::string get(const std::string& name, const std::string& fallback) const {
        const auto it = values_.find(name);
        return it == values_.end() ? fallback : it->second;
    }

    std::size_t get_size(const std::string& name, std::size_t fallback) const {
        const auto it = values_.find(name);
        return it == values_.end() ? fallback : parse_size(name, it->second);
    }

    std::vector<std::size_t> get_sizes(const std::string& name, const std::vector<std::size_t>& fallback) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return fallback;
        std::vector<std::size_t> out;
        std::stringstream ss(it->second);
        std::string item;
        while (std::getline(ss, item, ',')) out.push_back(parse_size(name, item));
        if (out.empty()) throw std::invalid_argument("--" + name + " must not be empty");
        return out;
    }

private:
    static std::size_t parse_size(const std::string& name, const std::string& text) {
        char* end = nullptr;
        errno = 0;
        const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
        if (text.empty() || text[0] == '-' || errno != 0 || *end != '\0') {
            throw std::invalid_argument("--" + name + " expects non-negative integers, got '" + text + "'");
        }
        return static_cast<std::size_t>(v);
    }

    std::map<std::string, std::string> values_;
};

}
