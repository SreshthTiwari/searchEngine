#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include "search_engine.h"

namespace se {

struct LatencyStats {
    std::size_t measured_queries = 0;
    std::size_t warmup_queries = 0;
    std::size_t repeats = 0;
    double mean_us = 0.0;
    double min_us = 0.0;
    double p50_us = 0.0;
    double p95_us = 0.0;
    double p99_us = 0.0;
    double max_us = 0.0;
    double qps = 0.0;
    double mean_docs_scored = 0.0;
};

double percentile_sorted(const std::vector<double>& sorted, double percentile);

LatencyStats measure_latency(SearchEngine& engine, const std::vector<std::string>& queries, std::size_t k,
                             Algorithm algorithm, std::size_t warmup, std::size_t repeats = 1);

std::uint64_t peak_rss_bytes();

class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::steady_clock::now()) {}
    double seconds() const {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    std::chrono::steady_clock::time_point start_;
};

class CsvWriter {
public:
    CsvWriter(const std::string& path, const std::vector<std::string>& header, bool append);

    void row(const std::vector<std::string>& fields);

private:
    std::ofstream out_;
};

std::string format_number(double value, int precision = 3);
std::string hardware_description();

}
