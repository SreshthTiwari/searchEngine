#include <algorithm>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

#include "bench_args.h"
#include "benchmark.h"
#include "compressed_index.h"
#include "document.h"
#include "index_builder.h"
#include "storage.h"

namespace {

constexpr double kMegabyte = 1024.0 * 1024.0;

const char* kUsage = R"(Usage: benchmark_index <mode> [options]

modes:
  build        time index construction for one thread count (run once per process so peak RSS is meaningful)
    --input PATH --threads N [--run ID] [--label NAME] [--output results/build.csv]
  compression  index size, compression ratio and codec throughput
    --input PATH [--sizes N,N,...] [--threads N] [--repeats R] [--label NAME] [--output results/compression.csv]
)";

double median(std::vector<double> values) {
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

int run_build(const bench::Args& args) {
    const std::string input = args.require("input");
    const std::size_t threads = args.get_size("threads", 1);
    const std::string label = args.get("label", input);
    const std::string output = args.get("output", "results/build.csv");

    se::Stopwatch load_timer;
    const auto docs = se::load_documents(input);
    const double load_seconds = load_timer.seconds();
    const double rss_after_load = static_cast<double>(se::peak_rss_bytes()) / kMegabyte;

    se::BuildOptions options;
    options.threads = threads;
    se::Stopwatch build_timer;
    const se::InvertedIndex index = se::build_index(docs, options);
    const double build_seconds = build_timer.seconds();
    const double rss_after_build = static_cast<double>(se::peak_rss_bytes()) / kMegabyte;

    const double docs_per_second = static_cast<double>(docs.size()) / build_seconds;
    std::printf("threads=%zu docs=%zu terms=%zu postings=%llu build=%.3fs (%.0f docs/s) peak_rss=%.1f MB\n",
                se::resolve_thread_count(threads), docs.size(), index.core().num_terms(),
                static_cast<unsigned long long>(index.num_postings()), build_seconds, docs_per_second,
                rss_after_build);

    se::CsvWriter csv(output,
                      {"dataset", "documents", "terms", "postings", "threads", "run", "load_seconds",
                       "build_seconds", "docs_per_second", "peak_rss_after_load_mb", "peak_rss_after_build_mb",
                       "hardware"},
                      true);
    csv.row({label, std::to_string(docs.size()), std::to_string(index.core().num_terms()),
             std::to_string(index.num_postings()), std::to_string(se::resolve_thread_count(threads)),
             args.get("run", "1"), se::format_number(load_seconds), se::format_number(build_seconds, 4),
             se::format_number(docs_per_second, 1), se::format_number(rss_after_load, 1),
             se::format_number(rss_after_build, 1), se::hardware_description()});
    return 0;
}

int run_compression(const bench::Args& args) {
    const std::string input = args.require("input");
    const std::string label = args.get("label", input);
    const std::string output = args.get("output", "results/compression.csv");
    const std::size_t threads = args.get_size("threads", 4);
    const std::size_t repeats = std::max<std::size_t>(1, args.get_size("repeats", 5));

    const auto all_docs = se::load_documents(input);
    const auto sizes = args.get_sizes("sizes", {all_docs.size()});

    se::CsvWriter csv(output,
                      {"dataset", "documents", "terms", "postings", "uncompressed_file_mb", "compressed_file_mb",
                       "uncompressed_postings_mb", "compressed_postings_mb", "uncompressed_bytes_per_posting",
                       "compressed_bytes_per_posting", "compression_ratio", "encode_seconds", "decode_seconds",
                       "encode_mpostings_per_s", "decode_mpostings_per_s", "encode_mb_per_s", "decode_mb_per_s",
                       "roundtrip_verified", "repeats", "hardware"},
                      false);

    for (const std::size_t size : sizes) {
        if (size == 0 || size > all_docs.size()) throw std::invalid_argument("size out of range: " + std::to_string(size));
        const std::vector<se::Document> docs(all_docs.begin(), all_docs.begin() + static_cast<std::ptrdiff_t>(size));
        se::BuildOptions options;
        options.threads = threads;
        const se::InvertedIndex plain = se::build_index(docs, options);

        std::vector<double> encode_times;
        std::vector<double> decode_times;
        se::CompressedIndex compressed;
        for (std::size_t r = 0; r < repeats; ++r) {
            se::Stopwatch encode_timer;
            compressed = se::CompressedIndex::compress(plain);
            encode_times.push_back(encode_timer.seconds());

            se::Stopwatch decode_timer;
            std::uint64_t checksum = 0;
            for (std::uint32_t t = 0; t < compressed.core().num_terms(); ++t) {
                for (auto cursor = compressed.cursor(t); cursor.valid(); cursor.next()) {
                    checksum += cursor.doc() + cursor.tf();
                }
            }
            decode_times.push_back(decode_timer.seconds());
            if (checksum == 0 && plain.num_postings() > 0) throw std::runtime_error("decode produced no data");
        }
        const bool verified = compressed.decompress() == plain;

        const double postings = static_cast<double>(plain.num_postings());
        const double raw_bytes = static_cast<double>(plain.postings_bytes());
        const double packed_bytes = static_cast<double>(compressed.postings_bytes());
        const double encode_s = median(encode_times);
        const double decode_s = median(decode_times);
        const double plain_file = static_cast<double>(se::serialize_index(plain).size());
        const double packed_file = static_cast<double>(se::serialize_index(compressed).size());

        std::printf("docs=%zu postings=%.0f raw=%.2fMB packed=%.2fMB ratio=%.2fx %.2f->%.2f B/posting encode=%.1f decode=%.1f Mpostings/s %s\n",
                    size, postings, raw_bytes / kMegabyte, packed_bytes / kMegabyte, raw_bytes / packed_bytes,
                    raw_bytes / postings, packed_bytes / postings, postings / encode_s / 1e6,
                    postings / decode_s / 1e6, verified ? "verified" : "MISMATCH");

        csv.row({label, std::to_string(size), std::to_string(plain.core().num_terms()),
                 std::to_string(plain.num_postings()), se::format_number(plain_file / kMegabyte, 3),
                 se::format_number(packed_file / kMegabyte, 3), se::format_number(raw_bytes / kMegabyte, 3),
                 se::format_number(packed_bytes / kMegabyte, 3), se::format_number(raw_bytes / postings, 3),
                 se::format_number(packed_bytes / postings, 3), se::format_number(raw_bytes / packed_bytes, 3),
                 se::format_number(encode_s, 4), se::format_number(decode_s, 4),
                 se::format_number(postings / encode_s / 1e6, 2), se::format_number(postings / decode_s / 1e6, 2),
                 se::format_number(raw_bytes / encode_s / kMegabyte, 1),
                 se::format_number(raw_bytes / decode_s / kMegabyte, 1), verified ? "yes" : "no",
                 std::to_string(repeats), se::hardware_description()});
        if (!verified) return 1;
    }
    std::cout << "Wrote " << output << "\n";
    return 0;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << kUsage;
        return 2;
    }
    const std::string mode = argv[1];
    try {
        std::vector<char*> rest = {argv[0]};
        for (int i = 2; i < argc; ++i) rest.push_back(argv[i]);
        const bench::Args args(static_cast<int>(rest.size()), rest.data());
        if (mode == "build") return run_build(args);
        if (mode == "compression") return run_compression(args);
        std::cerr << "unknown mode '" << mode << "'\n" << kUsage;
        return 2;
    } catch (const std::invalid_argument& e) {
        std::cerr << "error: " << e.what() << "\n" << kUsage;
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
