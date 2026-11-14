#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "bench_args.h"
#include "benchmark.h"
#include "compressed_index.h"
#include "document.h"
#include "evaluation.h"
#include "index_builder.h"
#include "search_engine.h"

namespace {

const char* kUsage = R"(Usage: benchmark_search --input DOCS --queries QUERIES [options]

  --output-dir DIR     where latency.csv and topk.csv are written          (default: results)
  --label NAME         dataset label stored in the CSV                     (default: input path)
  --sizes N,N,...      collection sizes (prefixes of DOCS) to measure      (default: all documents)
  --k N                top-k used for the main comparison                  (default: 10)
  --topks N,N,...      top-k sweep run on the largest size                 (default: 1,5,10,20,50,100)
  --warmup N           unmeasured warmup queries per configuration         (default: 100)
  --repeats N          passes over the query set per configuration         (default: 3)
  --threads N          threads used to build indexes                       (default: 4)
  --max-queries N      use only the first N queries, 0 = all               (default: 0)
)";

const std::vector<std::string> kHeader = {
    "dataset", "dataset_size", "index_format", "algorithm", "k", "measured_queries", "warmup_queries", "repeats",
    "mean_us", "p50_us", "p95_us", "p99_us", "qps", "mean_docs_scored", "result_mismatches", "hardware"};

struct Engines {
    se::SearchEngine plain;
    se::SearchEngine compressed;
};

Engines make_engines(const std::vector<se::Document>& docs, std::size_t threads) {
    se::BuildOptions options;
    options.threads = threads;
    se::InvertedIndex plain = se::build_index(docs, options);
    se::CompressedIndex packed = se::CompressedIndex::compress(plain);
    return {se::SearchEngine::from_index(std::move(plain)), se::SearchEngine::from_index(std::move(packed))};
}

std::size_t count_mismatches(se::SearchEngine& engine, const std::vector<std::string>& queries, std::size_t k) {
    std::size_t mismatches = 0;
    for (const std::string& query : queries) {
        if (engine.search_ids(query, k, se::Algorithm::Wand) != engine.search_ids(query, k, se::Algorithm::Exhaustive)) {
            ++mismatches;
        }
    }
    return mismatches;
}

std::size_t measure_engine(se::SearchEngine& engine, const std::string& label, std::size_t size,
                           const std::vector<std::string>& queries, std::size_t k, std::size_t warmup,
                           std::size_t repeats, se::CsvWriter& csv) {
    const std::string format = engine.compressed() ? "compressed" : "uncompressed";
    const std::size_t mismatches = count_mismatches(engine, queries, k);
    for (const se::Algorithm algorithm : {se::Algorithm::Exhaustive, se::Algorithm::Wand}) {
        const auto stats = se::measure_latency(engine, queries, k, algorithm, warmup, repeats);
        std::printf("size=%-8zu %-12s %-10s k=%-4zu p50=%9.1fus p95=%9.1fus p99=%9.1fus mean=%9.1fus qps=%9.1f scored=%9.1f\n",
                    size, format.c_str(), se::algorithm_name(algorithm), k, stats.p50_us, stats.p95_us, stats.p99_us,
                    stats.mean_us, stats.qps, stats.mean_docs_scored);
        csv.row({label, std::to_string(size), format, se::algorithm_name(algorithm), std::to_string(k),
                 std::to_string(stats.measured_queries), std::to_string(stats.warmup_queries),
                 std::to_string(stats.repeats), se::format_number(stats.mean_us), se::format_number(stats.p50_us),
                 se::format_number(stats.p95_us), se::format_number(stats.p99_us), se::format_number(stats.qps, 1),
                 se::format_number(stats.mean_docs_scored, 1), std::to_string(mismatches),
                 se::hardware_description()});
    }
    return mismatches;
}

}

int main(int argc, char** argv) {
    try {
        const bench::Args args(argc, argv);
        if (!args.has("input") || !args.has("queries")) {
            std::cerr << kUsage;
            return 2;
        }
        const std::string input = args.require("input");
        const std::string label = args.get("label", input);
        const std::string out_dir = args.get("output-dir", "results");
        const std::size_t k = args.get_size("k", 10);
        const std::size_t warmup = args.get_size("warmup", 100);
        const std::size_t repeats = args.get_size("repeats", 3);
        const std::size_t threads = args.get_size("threads", 4);
        const std::size_t max_queries = args.get_size("max-queries", 0);
        if (k == 0 || repeats == 0) throw std::invalid_argument("--k and --repeats must be at least 1");

        const auto docs = se::load_documents(input);
        auto queries_full = se::load_queries(args.require("queries"));
        if (max_queries > 0 && queries_full.size() > max_queries) queries_full.resize(max_queries);
        std::vector<std::string> queries;
        for (const auto& q : queries_full) queries.push_back(q.text);
        if (queries.empty()) throw std::invalid_argument("no queries to run");

        const auto sizes = args.get_sizes("sizes", {docs.size()});
        const auto topks = args.get_sizes("topks", {1, 5, 10, 20, 50, 100});

        std::cout << "Hardware: " << se::hardware_description() << "\n";
        std::cout << "Documents available: " << docs.size() << ", queries: " << queries.size()
                  << ", warmup: " << warmup << ", repeats: " << repeats << "\n";

        se::CsvWriter latency(out_dir + "/latency.csv", kHeader, false);
        se::CsvWriter topk_csv(out_dir + "/topk.csv", kHeader, false);

        std::size_t total_mismatches = 0;
        for (std::size_t i = 0; i < sizes.size(); ++i) {
            const std::size_t size = sizes[i];
            if (size == 0 || size > docs.size()) throw std::invalid_argument("size out of range: " + std::to_string(size));
            const std::vector<se::Document> subset(docs.begin(), docs.begin() + static_cast<std::ptrdiff_t>(size));
            Engines engines = make_engines(subset, threads);
            total_mismatches += measure_engine(engines.plain, label, size, queries, k, warmup, repeats, latency);
            total_mismatches += measure_engine(engines.compressed, label, size, queries, k, warmup, repeats, latency);

            if (i + 1 == sizes.size()) {
                for (const std::size_t sweep_k : topks) {
                    total_mismatches += measure_engine(engines.plain, label, size, queries, sweep_k, warmup, repeats, topk_csv);
                    total_mismatches += measure_engine(engines.compressed, label, size, queries, sweep_k, warmup, repeats, topk_csv);
                }
            }
        }

        if (total_mismatches > 0) {
            std::cerr << "ERROR: WAND and exhaustive results differed for " << total_mismatches << " query runs\n";
            return 1;
        }
        std::cout << "WAND matched exhaustive retrieval exactly for every query.\n";
        return 0;
    } catch (const std::invalid_argument& e) {
        std::cerr << "error: " << e.what() << "\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
