#include "benchmark.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <sys/resource.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

namespace se {

double percentile_sorted(const std::vector<double>& sorted, double percentile) {
    if (sorted.empty()) return 0.0;
    const double rank = std::ceil(percentile / 100.0 * static_cast<double>(sorted.size()));
    const std::size_t index = static_cast<std::size_t>(std::max(1.0, rank)) - 1;
    return sorted[std::min(index, sorted.size() - 1)];
}

LatencyStats measure_latency(SearchEngine& engine, const std::vector<std::string>& queries, std::size_t k,
                             Algorithm algorithm, std::size_t warmup, std::size_t repeats) {
    if (queries.empty()) throw std::invalid_argument("no queries to measure");
    if (repeats == 0) throw std::invalid_argument("repeats must be at least 1");

    volatile std::uint64_t sink = 0;
    for (std::size_t i = 0; i < warmup; ++i) {
        const auto hits = engine.search_ids(queries[i % queries.size()], k, algorithm);
        sink = sink + hits.size();
    }

    std::vector<double> latencies;
    latencies.reserve(queries.size() * repeats);
    QueryStats stats;
    for (std::size_t r = 0; r < repeats; ++r) {
        for (const std::string& query : queries) {
            const auto start = std::chrono::steady_clock::now();
            const auto hits = engine.search_ids(query, k, algorithm, &stats);
            const auto stop = std::chrono::steady_clock::now();
            sink = sink + hits.size();
            latencies.push_back(std::chrono::duration<double, std::micro>(stop - start).count());
        }
    }

    LatencyStats out;
    out.measured_queries = latencies.size();
    out.warmup_queries = warmup;
    out.repeats = repeats;
    double total = 0.0;
    for (double v : latencies) total += v;
    out.mean_us = total / static_cast<double>(latencies.size());
    out.qps = total > 0.0 ? static_cast<double>(latencies.size()) / (total / 1e6) : 0.0;
    out.mean_docs_scored = static_cast<double>(stats.docs_scored) / static_cast<double>(latencies.size());
    std::sort(latencies.begin(), latencies.end());
    out.min_us = latencies.front();
    out.max_us = latencies.back();
    out.p50_us = percentile_sorted(latencies, 50.0);
    out.p95_us = percentile_sorted(latencies, 95.0);
    out.p99_us = percentile_sorted(latencies, 99.0);
    return out;
}

std::uint64_t peak_rss_bytes() {
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

CsvWriter::CsvWriter(const std::string& path, const std::vector<std::string>& header, bool append) {
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
    bool write_header = true;
    if (append && std::filesystem::exists(p) && std::filesystem::file_size(p) > 0) write_header = false;
    out_.open(path, append ? std::ios::app : std::ios::trunc);
    if (!out_) throw std::runtime_error("cannot open '" + path + "' for writing");
    if (write_header) row(header);
}

void CsvWriter::row(const std::vector<std::string>& fields) {
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) out_ << ',';
        const std::string& f = fields[i];
        if (f.find_first_of(",\"\n") == std::string::npos) {
            out_ << f;
        } else {
            out_ << '"';
            for (char c : f) {
                if (c == '"') out_ << '"';
                out_ << c;
            }
            out_ << '"';
        }
    }
    out_ << '\n';
    out_.flush();
}

std::string format_number(double value, int precision) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(precision) << value;
    return os.str();
}

std::string hardware_description() {
    std::ostringstream os;
#if defined(__APPLE__)
    char brand[256] = {0};
    std::size_t len = sizeof(brand);
    if (sysctlbyname("machdep.cpu.brand_string", brand, &len, nullptr, 0) == 0) os << brand << ", ";
#else
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.rfind("model name", 0) == 0) {
            const auto colon = line.find(':');
            if (colon != std::string::npos) os << line.substr(colon + 2) << ", ";
            break;
        }
    }
#endif
    os << std::thread::hardware_concurrency() << " hardware threads";
    return os.str();
}

}
