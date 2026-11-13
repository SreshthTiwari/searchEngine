#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "benchmark.h"
#include "compressed_index.h"
#include "document.h"
#include "evaluation.h"
#include "index_builder.h"
#include "search_engine.h"
#include "storage.h"

namespace {

class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

const char* kUsage = R"(Usage: search_engine <command> [options]

Commands:
  index        Build an index from a document collection
  search       Run one query against an index
  interactive  Read queries from standard input and print results
  benchmark    Measure query latency on a query file and write a CSV
  evaluate     Compute MRR@k and Recall@k from relevance judgements
  info         Print header information of an index file

index options:
  --input PATH          documents file (TSV: id<TAB>text or id<TAB>title<TAB>text, or JSONL)
  --output PATH         index file to write
  --format F            auto | tsv | jsonl                      (default: auto)
  --threads N           worker threads, 0 = all cores           (default: 1)
  --compression C       vbyte | none                            (default: vbyte)
  --stopwords           remove English stopwords while indexing

search options:
  --index PATH          index file
  --query TEXT          query string
  --topk N              number of results                       (default: 10)
  --algorithm A         wand | exhaustive                       (default: wand)
  --k1 X --b X          BM25 parameters                         (default: 1.2, 0.75)

interactive options:  --index PATH [--topk N] [--algorithm A] [--k1 X] [--b X]

benchmark options:
  --index PATH          index file
  --queries PATH        queries file (TSV: id<TAB>query, or one query per line)
  --output PATH         CSV file to write                       (default: results/benchmark.csv)
  --topk N              number of results per query             (default: 10)
  --warmup N            unmeasured warmup queries               (default: 100)
  --repeats N           passes over the query set               (default: 1)
  --append              append to the CSV instead of overwriting

evaluate options:
  --index PATH --queries PATH --qrels PATH [--topk N] [--algorithm A]

info options:         --index PATH
)";

class Args {
public:
    Args(int argc, char** argv, int first, std::set<std::string> value_options, std::set<std::string> flag_options) {
        for (int i = first; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg.rfind("--", 0) != 0) throw UsageError("unexpected argument '" + arg + "'");
            const std::string name = arg.substr(2);
            if (flag_options.count(name)) {
                values_[name] = "1";
            } else if (value_options.count(name)) {
                if (i + 1 >= argc) throw UsageError("option --" + name + " requires a value");
                values_[name] = argv[++i];
            } else {
                throw UsageError("unknown option --" + name);
            }
        }
    }

    bool has(const std::string& name) const { return values_.count(name) > 0; }

    std::string require(const std::string& name) const {
        const auto it = values_.find(name);
        if (it == values_.end()) throw UsageError("missing required option --" + name);
        return it->second;
    }

    std::string get(const std::string& name, const std::string& fallback) const {
        const auto it = values_.find(name);
        return it == values_.end() ? fallback : it->second;
    }

    std::size_t get_size(const std::string& name, std::size_t fallback) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return fallback;
        const std::string& text = it->second;
        if (text.empty() || text[0] == '-' || text[0] == '+') throw UsageError("--" + name + " must be a non-negative integer");
        char* end = nullptr;
        errno = 0;
        const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
        if (errno != 0 || *end != '\0') throw UsageError("--" + name + " must be a non-negative integer");
        return static_cast<std::size_t>(v);
    }

    double get_double(const std::string& name, double fallback) const {
        const auto it = values_.find(name);
        if (it == values_.end()) return fallback;
        char* end = nullptr;
        errno = 0;
        const double v = std::strtod(it->second.c_str(), &end);
        if (errno != 0 || it->second.empty() || *end != '\0') throw UsageError("--" + name + " must be a number");
        return v;
    }

private:
    std::map<std::string, std::string> values_;
};

se::Bm25Params bm25_params(const Args& args) {
    se::Bm25Params params;
    params.k1 = args.get_double("k1", params.k1);
    params.b = args.get_double("b", params.b);
    return params;
}

std::size_t require_positive(const Args& args, const std::string& name, std::size_t fallback) {
    const std::size_t value = args.get_size(name, fallback);
    if (value == 0) throw UsageError("--" + name + " must be at least 1");
    return value;
}

std::string describe(const se::SearchResult& r) {
    if (!r.title.empty()) return r.title;
    constexpr std::size_t kMax = 100;
    if (r.snippet.size() <= kMax) return r.snippet;
    return r.snippet.substr(0, kMax) + "...";
}

void print_results(const std::string& query, const std::vector<se::SearchResult>& results, double millis,
                   const se::SearchEngine& engine, se::Algorithm algorithm) {
    std::cout << "Query: " << query << "\n\n";
    if (results.empty()) {
        std::cout << "No matching documents.\n";
    }
    for (std::size_t i = 0; i < results.size(); ++i) {
        std::printf("%zu. Doc %s | Score: %.4f | %s\n", i + 1, results[i].external_id.c_str(), results[i].score,
                    describe(results[i]).c_str());
    }
    std::printf("\n(%zu result(s) in %.3f ms, %s, %s index)\n", results.size(), millis, se::algorithm_name(algorithm),
                engine.compressed() ? "compressed" : "uncompressed");
}

int run_index(const Args& args) {
    const std::string input = args.require("input");
    const std::string output = args.require("output");
    const std::string compression = args.get("compression", "vbyte");
    if (compression != "vbyte" && compression != "none") {
        throw UsageError("--compression must be 'vbyte' or 'none'");
    }

    se::BuildOptions options;
    options.threads = args.get_size("threads", 1);
    options.tokenizer.remove_stopwords = args.has("stopwords");

    se::Stopwatch total;
    se::Stopwatch load_timer;
    const auto docs = se::load_documents(input, se::parse_input_format(args.get("format", "auto")));
    const double load_seconds = load_timer.seconds();
    std::cout << "Loaded " << docs.size() << " documents in " << se::format_number(load_seconds, 2) << " s\n";

    se::Stopwatch build_timer;
    const se::InvertedIndex index = se::build_index(docs, options);
    const double build_seconds = build_timer.seconds();
    std::cout << "Indexed with " << se::resolve_thread_count(options.threads) << " thread(s) in "
              << se::format_number(build_seconds, 2) << " s ("
              << se::format_number(static_cast<double>(docs.size()) / std::max(build_seconds, 1e-9), 0)
              << " docs/s)\n";

    se::Stopwatch save_timer;
    if (compression == "vbyte") {
        const se::CompressedIndex compressed = se::CompressedIndex::compress(index);
        se::save_index(compressed, output);
    } else {
        se::save_index(index, output);
    }
    const auto info = se::read_index_info(output);

    std::cout << "Wrote " << output << " (" << (info.compressed ? "compressed" : "uncompressed") << ", "
              << se::format_number(static_cast<double>(info.file_bytes) / (1024.0 * 1024.0), 2) << " MB) in "
              << se::format_number(save_timer.seconds(), 2) << " s\n";
    std::cout << "  documents: " << info.num_docs << "\n  terms:     " << info.num_terms
              << "\n  postings:  " << info.num_postings << "\n  avg doc length: "
              << se::format_number(index.core().avg_doc_length(), 2) << " tokens\n";
    std::cout << "Total time: " << se::format_number(total.seconds(), 2) << " s\n";
    return 0;
}

int run_search(const Args& args) {
    auto engine = se::SearchEngine::load(args.require("index"), bm25_params(args));
    const std::string query = args.require("query");
    const std::size_t k = args.get_size("topk", 10);
    const se::Algorithm algorithm = se::parse_algorithm(args.get("algorithm", "wand"));

    se::Stopwatch timer;
    const auto results = engine.search(query, k, algorithm);
    print_results(query, results, timer.seconds() * 1000.0, engine, algorithm);
    return 0;
}

int run_interactive(const Args& args) {
    auto engine = se::SearchEngine::load(args.require("index"), bm25_params(args));
    const std::size_t k = args.get_size("topk", 10);
    const se::Algorithm algorithm = se::parse_algorithm(args.get("algorithm", "wand"));
    const auto& core = engine.core();
    std::cout << "Loaded " << core.num_docs() << " documents, " << core.num_terms()
              << " terms. Enter a query (empty line or :quit to exit).\n";
    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) break;
        if (line == ":quit" || line == ":q") break;
        if (line.find_first_not_of(" \t\r") == std::string::npos) break;
        se::Stopwatch timer;
        const auto results = engine.search(line, k, algorithm);
        print_results(line, results, timer.seconds() * 1000.0, engine, algorithm);
    }
    return 0;
}

int run_benchmark(const Args& args) {
    const std::string index_path = args.require("index");
    const auto queries = se::load_queries(args.require("queries"));
    if (queries.empty()) throw UsageError("queries file contains no queries");
    const std::size_t k = require_positive(args, "topk", 10);
    const std::size_t warmup = args.get_size("warmup", 100);
    const std::size_t repeats = require_positive(args, "repeats", 1);
    const std::string output = args.get("output", "results/benchmark.csv");

    std::vector<std::string> texts;
    texts.reserve(queries.size());
    for (const auto& q : queries) texts.push_back(q.text);

    auto engine = se::SearchEngine::load(index_path, bm25_params(args));
    const std::string format = engine.compressed() ? "compressed" : "uncompressed";

    se::CsvWriter csv(output,
                      {"index_file", "index_format", "algorithm", "k", "measured_queries", "warmup_queries",
                       "repeats", "mean_us", "p50_us", "p95_us", "p99_us", "qps", "mean_docs_scored", "hardware"},
                      args.has("append"));

    std::cout << "Hardware: " << se::hardware_description() << "\n";
    std::cout << "Index: " << index_path << " (" << format << ", " << engine.core().num_docs() << " docs)\n";
    std::printf("%-11s %8s %10s %10s %10s %10s %12s\n", "algorithm", "queries", "p50 (us)", "p95 (us)", "p99 (us)",
                "mean (us)", "QPS");

    for (const se::Algorithm algorithm : {se::Algorithm::Exhaustive, se::Algorithm::Wand}) {
        const auto stats = se::measure_latency(engine, texts, k, algorithm, warmup, repeats);
        std::printf("%-11s %8zu %10.1f %10.1f %10.1f %10.1f %12.1f\n", se::algorithm_name(algorithm),
                    stats.measured_queries, stats.p50_us, stats.p95_us, stats.p99_us, stats.mean_us, stats.qps);
        csv.row({index_path, format, se::algorithm_name(algorithm), std::to_string(k),
                 std::to_string(stats.measured_queries), std::to_string(stats.warmup_queries),
                 std::to_string(stats.repeats), se::format_number(stats.mean_us), se::format_number(stats.p50_us),
                 se::format_number(stats.p95_us), se::format_number(stats.p99_us), se::format_number(stats.qps, 1),
                 se::format_number(stats.mean_docs_scored, 1), se::hardware_description()});
    }
    std::cout << "Wrote " << output << "\n";
    return 0;
}

int run_evaluate(const Args& args) {
    auto engine = se::SearchEngine::load(args.require("index"), bm25_params(args));
    const auto queries = se::load_queries(args.require("queries"));
    const auto qrels = se::load_qrels(args.require("qrels"));
    const std::size_t k = require_positive(args, "topk", 10);
    const se::Algorithm algorithm = se::parse_algorithm(args.get("algorithm", "wand"));
    const auto result = se::evaluate(engine, queries, qrels, k, algorithm);
    std::cout << "Algorithm: " << se::algorithm_name(algorithm) << "\n"
              << "Queries evaluated: " << result.queries_evaluated << " (skipped without judgements: "
              << result.queries_skipped << ")\n"
              << "MRR@" << k << ":    " << se::format_number(result.mrr, 4) << "\n"
              << "Recall@" << k << ": " << se::format_number(result.recall, 4) << "\n";
    return 0;
}

int run_info(const Args& args) {
    const auto info = se::read_index_info(args.require("index"));
    std::cout << "format version: " << info.version << "\n"
              << "postings:       " << (info.compressed ? "compressed (delta + variable-byte)" : "uncompressed") << "\n"
              << "documents:      " << info.num_docs << "\n"
              << "terms:          " << info.num_terms << "\n"
              << "postings count: " << info.num_postings << "\n"
              << "total tokens:   " << info.total_length << "\n"
              << "file size:      " << info.file_bytes << " bytes\n";
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h" ||
        std::string(argv[1]) == "help") {
        std::cout << kUsage;
        return argc < 2 ? 2 : 0;
    }
    const std::string command = argv[1];
    try {
        if (command == "index") {
            return run_index(Args(argc, argv, 2, {"input", "output", "format", "threads", "compression"}, {"stopwords"}));
        }
        if (command == "search") {
            return run_search(Args(argc, argv, 2, {"index", "query", "topk", "algorithm", "k1", "b"}, {}));
        }
        if (command == "interactive") {
            return run_interactive(Args(argc, argv, 2, {"index", "topk", "algorithm", "k1", "b"}, {}));
        }
        if (command == "benchmark") {
            return run_benchmark(Args(argc, argv, 2,
                                      {"index", "queries", "output", "topk", "warmup", "repeats", "k1", "b"},
                                      {"append"}));
        }
        if (command == "evaluate") {
            return run_evaluate(Args(argc, argv, 2, {"index", "queries", "qrels", "topk", "algorithm", "k1", "b"}, {}));
        }
        if (command == "info") {
            return run_info(Args(argc, argv, 2, {"index"}, {}));
        }
        std::cerr << "error: unknown command '" << command << "'\n\n" << kUsage;
        return 2;
    } catch (const UsageError& e) {
        std::cerr << "error: " << e.what() << "\n\nRun 'search_engine --help' for usage.\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
