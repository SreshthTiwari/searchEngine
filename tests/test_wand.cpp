#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "bm25.h"
#include "compressed_index.h"
#include "index_builder.h"
#include "test_corpus.h"
#include "test_util.h"
#include "wand.h"

using se::SearchHit;

namespace {

std::vector<std::uint32_t> resolve(const se::IndexCore& core, const std::string& query) {
    se::Tokenizer tokenizer(core.tokenizer);
    std::vector<std::uint32_t> ids;
    for (const auto& token : tokenizer.tokenize(query)) {
        if (const auto id = core.find(token)) ids.push_back(*id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

template <class Index>
void compare_algorithms(const Index& index, const se::Bm25Model& model, const std::vector<double>& bounds,
                        const std::string& query, std::size_t k) {
    se::ExhaustiveScratch scratch;
    const auto terms = resolve(index.core(), query);
    const auto expected = se::exhaustive_search(index, model, terms, k, scratch);
    const auto actual = se::wand_search(index, model, bounds, terms, k);
    if (!(expected == actual)) {
        testing::report(__FILE__, __LINE__, "WAND differs from exhaustive for query '" + query + "' k=" + std::to_string(k));
    }
}

}

TEST(wand_matches_exhaustive_on_random_collections) {
    std::mt19937 rng(2024);
    for (int round = 0; round < 6; ++round) {
        const std::size_t vocab = 20 + round * 60;
        const auto docs = testing::random_documents(100 + round, 400 + round * 300, vocab, 10 + round * 15);
        const auto index = se::build_index(docs, se::BuildOptions{});
        const se::Bm25Model model(index.core(), {});
        const auto bounds = se::compute_upper_bounds(index, model);
        for (int q = 0; q < 80; ++q) {
            const std::string query = testing::random_query(rng, vocab, 6);
            for (std::size_t k : {1, 3, 10, 50, 1000}) compare_algorithms(index, model, bounds, query, k);
        }
    }
}

TEST(wand_matches_exhaustive_on_compressed_index) {
    std::mt19937 rng(99);
    const auto docs = testing::random_documents(55, 2500, 200, 50);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);
    const se::Bm25Model model(compressed.core(), {});
    const auto bounds = se::compute_upper_bounds(compressed, model);
    for (int q = 0; q < 150; ++q) {
        const std::string query = testing::random_query(rng, 200, 6);
        for (std::size_t k : {1, 10, 100}) compare_algorithms(compressed, model, bounds, query, k);
    }
}

TEST(wand_compressed_and_uncompressed_agree) {
    std::mt19937 rng(3);
    const auto docs = testing::random_documents(56, 1800, 120, 40);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);
    const se::Bm25Model model(plain.core(), {});
    const auto b1 = se::compute_upper_bounds(plain, model);
    const auto b2 = se::compute_upper_bounds(compressed, model);
    CHECK(b1 == b2);
    for (int q = 0; q < 100; ++q) {
        const auto terms = resolve(plain.core(), testing::random_query(rng, 120, 5));
        CHECK(se::wand_search(plain, model, b1, terms, 10) == se::wand_search(compressed, model, b2, terms, 10));
    }
}

TEST(wand_tie_handling_matches_exhaustive_on_duplicate_documents) {
    std::vector<std::string> texts;
    for (int i = 0; i < 300; ++i) texts.push_back(i % 3 == 0 ? "alpha beta" : (i % 3 == 1 ? "alpha gamma" : "beta gamma delta"));
    const auto index = se::build_index(testing::make_documents(texts), se::BuildOptions{});
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    for (const char* query : {"alpha", "beta", "gamma", "alpha beta", "alpha beta gamma", "delta alpha"}) {
        for (std::size_t k : {1, 2, 7, 100, 500}) compare_algorithms(index, model, bounds, query, k);
    }
}

TEST(wand_tie_break_prefers_lower_document_ids) {
    const auto index = se::build_index(testing::make_documents({"x y", "x y", "x y", "x y", "x y"}), se::BuildOptions{});
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    const auto hits = se::wand_search(index, model, bounds, resolve(index.core(), "x y"), 2);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits[0].doc, std::uint32_t{0});
    CHECK_EQ(hits[1].doc, std::uint32_t{1});
}

TEST(wand_handles_empty_unknown_and_zero_k) {
    const auto index = se::build_index(testing::make_documents({"a b c", "b c d"}), se::BuildOptions{});
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    CHECK(se::wand_search(index, model, bounds, resolve(index.core(), ""), 10).empty());
    CHECK(se::wand_search(index, model, bounds, resolve(index.core(), "zzz"), 10).empty());
    CHECK(se::wand_search(index, model, bounds, resolve(index.core(), "b"), 0).empty());
}

TEST(wand_with_non_default_bm25_parameters) {
    std::mt19937 rng(17);
    const auto docs = testing::random_documents(61, 1000, 100, 40);
    const auto index = se::build_index(docs, se::BuildOptions{});
    for (const se::Bm25Params params : {se::Bm25Params{0.0, 0.0}, se::Bm25Params{2.0, 0.3}, se::Bm25Params{1.2, 1.0},
                                        se::Bm25Params{3.0, 0.9}}) {
        const se::Bm25Model model(index.core(), params);
        const auto bounds = se::compute_upper_bounds(index, model);
        for (int q = 0; q < 60; ++q) {
            compare_algorithms(index, model, bounds, testing::random_query(rng, 100, 5), 10);
        }
    }
}

TEST(wand_upper_bounds_dominate_every_actual_contribution) {
    const auto docs = testing::random_documents(62, 800, 80, 40);
    const auto index = se::build_index(docs, se::BuildOptions{});
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    for (std::uint32_t t = 0; t < index.core().num_terms(); ++t) {
        for (const auto& p : index.postings(t)) {
            CHECK(model.contribution(model.idf(t), p.tf, p.doc) <= bounds[t]);
        }
    }
}

TEST(wand_scores_fewer_documents_than_exhaustive_on_skewed_queries) {
    const auto docs = testing::random_documents(63, 6000, 300, 60);
    const auto index = se::build_index(docs, se::BuildOptions{});
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    std::mt19937 rng(8);
    std::uint64_t wand_scored = 0;
    std::uint64_t exhaustive_scored = 0;
    se::ExhaustiveScratch scratch;
    for (int q = 0; q < 100; ++q) {
        const auto terms = resolve(index.core(), testing::random_query(rng, 300, 5));
        se::QueryStats w;
        se::QueryStats e;
        const auto a = se::wand_search(index, model, bounds, terms, 10, &w);
        const auto b = se::exhaustive_search(index, model, terms, 10, scratch, &e);
        CHECK(a == b);
        wand_scored += w.docs_scored;
        exhaustive_scored += e.docs_scored;
    }
    CHECK(wand_scored <= exhaustive_scored);
}
