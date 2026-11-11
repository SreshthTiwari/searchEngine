#include <filesystem>
#include <random>
#include <string>

#include "index_builder.h"
#include "search_engine.h"
#include "test_corpus.h"
#include "test_util.h"

using se::Algorithm;

namespace {

std::vector<se::Document> sample_docs() {
    return {{"P1", "Intro", "Machine learning algorithms learn patterns from data"},
            {"P2", "", "Deep learning is a subset of machine learning"},
            {"P3", "Cooking", "A recipe for tomato soup with basil"},
            {"P4", "", "Efficient algorithms for sorting and searching data"}};
}

}

TEST(engine_returns_ranked_results_with_metadata) {
    auto engine = se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}));
    const auto results = engine.search("machine learning algorithms", 3);
    CHECK(!results.empty());
    CHECK_EQ(results[0].external_id, std::string("P1"));
    CHECK_EQ(results[0].title, std::string("Intro"));
    CHECK(results[0].snippet.find("Machine learning") == 0);
    for (std::size_t i = 1; i < results.size(); ++i) CHECK(results[i - 1].score >= results[i].score);
}

TEST(engine_queries_are_normalised_like_documents) {
    auto engine = se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}));
    CHECK(engine.search_ids("MACHINE, Learning!!", 10, Algorithm::Wand) ==
          engine.search_ids("machine learning", 10, Algorithm::Wand));
}

TEST(engine_unknown_and_empty_queries_return_no_results) {
    auto engine = se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}));
    CHECK(engine.search("", 10).empty());
    CHECK(engine.search("???", 10).empty());
    CHECK(engine.search("xyzzy plugh", 10).empty());
}

TEST(engine_duplicate_query_terms_are_deduplicated) {
    auto engine = se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}));
    CHECK(engine.search_ids("data data data", 10, Algorithm::Wand) == engine.search_ids("data", 10, Algorithm::Wand));
}

TEST(engine_algorithms_and_backends_agree) {
    const auto docs = testing::random_documents(71, 1500, 150, 40);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    auto a = se::SearchEngine::from_index(plain);
    auto b = se::SearchEngine::from_index(se::CompressedIndex::compress(plain));
    CHECK(!a.compressed());
    CHECK(b.compressed());
    std::mt19937 rng(14);
    for (int q = 0; q < 80; ++q) {
        const std::string query = testing::random_query(rng, 150, 5);
        const auto reference = a.search_ids(query, 10, Algorithm::Exhaustive);
        CHECK(a.search_ids(query, 10, Algorithm::Wand) == reference);
        CHECK(b.search_ids(query, 10, Algorithm::Exhaustive) == reference);
        CHECK(b.search_ids(query, 10, Algorithm::Wand) == reference);
    }
}

TEST(engine_save_load_preserves_results) {
    const auto docs = testing::random_documents(72, 800, 100, 30);
    const auto path = (std::filesystem::temp_directory_path() / "se_test_engine.bin").string();
    const auto plain = se::build_index(docs, se::BuildOptions{});
    se::save_index(se::CompressedIndex::compress(plain), path);
    auto original = se::SearchEngine::from_index(plain);
    auto loaded = se::SearchEngine::load(path);
    std::remove(path.c_str());
    CHECK(loaded.compressed());
    std::mt19937 rng(2);
    for (int q = 0; q < 50; ++q) {
        const std::string query = testing::random_query(rng, 100, 4);
        CHECK(original.search_ids(query, 10, Algorithm::Wand) == loaded.search_ids(query, 10, Algorithm::Wand));
    }
}

TEST(engine_parses_algorithm_names) {
    CHECK(se::parse_algorithm("wand") == Algorithm::Wand);
    CHECK(se::parse_algorithm("exhaustive") == Algorithm::Exhaustive);
    CHECK_THROWS(se::parse_algorithm("bm25"), std::invalid_argument);
}

TEST(engine_rejects_invalid_bm25_parameters) {
    CHECK_THROWS(se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}), {-1.0, 0.5}),
                 std::invalid_argument);
}
