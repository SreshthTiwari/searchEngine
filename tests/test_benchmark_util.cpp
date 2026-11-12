#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark.h"
#include "index_builder.h"
#include "test_corpus.h"
#include "test_util.h"

TEST(benchmark_percentiles_use_nearest_rank) {
    std::vector<double> values;
    for (int i = 1; i <= 100; ++i) values.push_back(i);
    CHECK_NEAR(se::percentile_sorted(values, 50), 50.0, 0.0);
    CHECK_NEAR(se::percentile_sorted(values, 95), 95.0, 0.0);
    CHECK_NEAR(se::percentile_sorted(values, 99), 99.0, 0.0);
    CHECK_NEAR(se::percentile_sorted(values, 100), 100.0, 0.0);
    CHECK_NEAR(se::percentile_sorted(values, 0), 1.0, 0.0);
    CHECK_NEAR(se::percentile_sorted({}, 50), 0.0, 0.0);
    CHECK_NEAR(se::percentile_sorted({7.0}, 99), 7.0, 0.0);
}

TEST(benchmark_latency_reports_requested_counts) {
    const auto docs = testing::random_documents(81, 500, 60, 30);
    auto engine = se::SearchEngine::from_index(se::build_index(docs, se::BuildOptions{}));
    const std::vector<std::string> queries = {"w1 w2", "w3", "w0 w5 w9", "w40"};
    const auto stats = se::measure_latency(engine, queries, 10, se::Algorithm::Wand, 5, 3);
    CHECK_EQ(stats.measured_queries, std::size_t{12});
    CHECK_EQ(stats.warmup_queries, std::size_t{5});
    CHECK(stats.min_us <= stats.p50_us);
    CHECK(stats.p50_us <= stats.p95_us);
    CHECK(stats.p95_us <= stats.p99_us);
    CHECK(stats.p99_us <= stats.max_us);
    CHECK(stats.qps > 0.0);
}

TEST(benchmark_latency_rejects_bad_arguments) {
    auto engine = se::SearchEngine::from_index(se::build_index(testing::make_documents({"a b"}), se::BuildOptions{}));
    CHECK_THROWS(se::measure_latency(engine, {}, 10, se::Algorithm::Wand, 0), std::invalid_argument);
    CHECK_THROWS(se::measure_latency(engine, {"a"}, 10, se::Algorithm::Wand, 0, 0), std::invalid_argument);
}

TEST(benchmark_csv_writer_escapes_and_appends) {
    const auto path = (std::filesystem::temp_directory_path() / "se_test_results.csv").string();
    std::remove(path.c_str());
    {
        se::CsvWriter writer(path, {"a", "b"}, false);
        writer.row({"1", "two,words"});
    }
    {
        se::CsvWriter writer(path, {"a", "b"}, true);
        writer.row({"3", "say \"hi\""});
    }
    std::ifstream in(path);
    std::stringstream buffer;
    buffer << in.rdbuf();
    CHECK_EQ(buffer.str(), std::string("a,b\n1,\"two,words\"\n3,\"say \"\"hi\"\"\"\n"));
    std::remove(path.c_str());
}

TEST(benchmark_peak_rss_is_reported) {
    CHECK(se::peak_rss_bytes() > 0);
    CHECK(!se::hardware_description().empty());
    CHECK_EQ(se::format_number(3.14159, 2), std::string("3.14"));
}
