#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "bm25.h"
#include "index_builder.h"
#include "test_corpus.h"
#include "test_util.h"
#include "tokenizer.h"

using se::Bm25Model;
using se::SearchHit;
using testing::make_documents;

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

std::vector<SearchHit> run(const se::InvertedIndex& index, const Bm25Model& model, const std::string& query,
                           std::size_t k) {
    se::ExhaustiveScratch scratch;
    const auto terms = resolve(index.core(), query);
    return se::exhaustive_search(index, model, terms, k, scratch);
}

struct Reference {
    std::vector<std::vector<std::string>> docs;
    double k1 = 1.2;
    double b = 0.75;

    double idf(const std::string& term) const {
        double df = 0;
        for (const auto& d : docs) df += std::count(d.begin(), d.end(), term) > 0 ? 1 : 0;
        const double n = static_cast<double>(docs.size());
        return std::log(1.0 + (n - df + 0.5) / (df + 0.5));
    }

    double score(std::size_t doc, std::vector<std::string> query) const {
        double total_len = 0;
        for (const auto& d : docs) total_len += static_cast<double>(d.size());
        const double avg = total_len / static_cast<double>(docs.size());
        std::sort(query.begin(), query.end());
        query.erase(std::unique(query.begin(), query.end()), query.end());
        double s = 0;
        for (const auto& term : query) {
            const double tf = static_cast<double>(std::count(docs[doc].begin(), docs[doc].end(), term));
            if (tf == 0) continue;
            const double len = static_cast<double>(docs[doc].size());
            s += idf(term) * tf * (k1 + 1.0) / (tf + k1 * (1.0 - b + b * len / avg));
        }
        return s;
    }
};

se::InvertedIndex fruit_index() {
    return se::build_index(make_documents({"apple banana apple", "banana cherry", "apple cherry date elderberry"}),
                           se::BuildOptions{});
}

}

TEST(bm25_matches_hand_computed_scores) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    const auto hits = run(index, model, "apple cherry", 10);
    CHECK_EQ(hits.size(), std::size_t{3});
    CHECK_EQ(hits[0].doc, std::uint32_t{2});
    CHECK_NEAR(hits[0].score, 0.8272063874724948, 1e-12);
    CHECK_EQ(hits[1].doc, std::uint32_t{0});
    CHECK_NEAR(hits[1].score, 0.6462549902128865, 1e-12);
    CHECK_EQ(hits[2].doc, std::uint32_t{1});
    CHECK_NEAR(hits[2].score, 0.5442147286003255, 1e-12);
}

TEST(bm25_single_term_query) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    const auto hits = run(index, model, "apple", 10);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_EQ(hits[0].doc, std::uint32_t{0});
    CHECK_NEAR(hits[0].score, 0.6462549902128865, 1e-12);
    CHECK_EQ(hits[1].doc, std::uint32_t{2});
    CHECK_NEAR(hits[1].score, 0.4136031937362474, 1e-12);
}

TEST(bm25_matches_independent_reference_on_random_data) {
    const auto docs = testing::random_documents(77, 200, 60, 30);
    const auto index = se::build_index(docs, se::BuildOptions{});
    se::Tokenizer tokenizer;
    Reference ref;
    for (const auto& d : docs) ref.docs.push_back(tokenizer.tokenize(d.text));

    std::mt19937 rng(5);
    for (int q = 0; q < 40; ++q) {
        const std::string query = testing::random_query(rng, 60, 4);
        const Bm25Model model(index.core(), {});
        const auto hits = run(index, model, query, 1000);
        const auto q_tokens = tokenizer.tokenize(query);
        std::size_t expected_matches = 0;
        for (std::size_t d = 0; d < docs.size(); ++d) {
            const double expected = ref.score(d, q_tokens);
            if (expected == 0.0) continue;
            ++expected_matches;
            const auto it = std::find_if(hits.begin(), hits.end(), [&](const SearchHit& h) { return h.doc == d; });
            CHECK(it != hits.end());
            if (it != hits.end()) CHECK_NEAR(it->score, expected, 1e-9);
        }
        CHECK_EQ(hits.size(), expected_matches);
    }
}

TEST(bm25_parameters_are_configurable) {
    const auto index = fruit_index();
    const Bm25Model flat(index.core(), {0.0, 0.0});
    const auto hits = run(index, flat, "apple", 10);
    CHECK_EQ(hits.size(), std::size_t{2});
    CHECK_NEAR(hits[0].score, hits[1].score, 1e-12);
    CHECK_EQ(hits[0].doc, std::uint32_t{0});
    CHECK_EQ(hits[1].doc, std::uint32_t{2});

    const Bm25Model long_penalised(index.core(), {1.2, 1.0});
    Reference ref;
    ref.b = 1.0;
    se::Tokenizer tokenizer;
    for (const char* text : {"apple banana apple", "banana cherry", "apple cherry date elderberry"}) {
        ref.docs.push_back(tokenizer.tokenize(text));
    }
    const auto b1 = run(index, long_penalised, "apple", 10);
    CHECK_NEAR(b1[0].score, ref.score(b1[0].doc, {"apple"}), 1e-12);
    CHECK_NEAR(b1[1].score, ref.score(b1[1].doc, {"apple"}), 1e-12);
}

TEST(bm25_rejects_invalid_parameters) {
    const auto index = fruit_index();
    CHECK_THROWS(Bm25Model(index.core(), {-1.0, 0.5}), std::invalid_argument);
    CHECK_THROWS(Bm25Model(index.core(), {1.2, 1.5}), std::invalid_argument);
    CHECK_THROWS(Bm25Model(index.core(), {1.2, -0.1}), std::invalid_argument);
}

TEST(bm25_ties_break_by_ascending_document_id) {
    const auto index = se::build_index(make_documents({"same text", "same text", "same text", "same text"}),
                                       se::BuildOptions{});
    const Bm25Model model(index.core(), {});
    const auto hits = run(index, model, "same", 3);
    CHECK_EQ(hits.size(), std::size_t{3});
    CHECK_EQ(hits[0].doc, std::uint32_t{0});
    CHECK_EQ(hits[1].doc, std::uint32_t{1});
    CHECK_EQ(hits[2].doc, std::uint32_t{2});
}

TEST(bm25_results_are_sorted_descending_and_unique) {
    const auto docs = testing::random_documents(8, 500, 40, 25);
    const auto index = se::build_index(docs, se::BuildOptions{});
    const Bm25Model model(index.core(), {});
    std::mt19937 rng(12);
    for (int q = 0; q < 50; ++q) {
        const auto hits = run(index, model, testing::random_query(rng, 40, 5), 20);
        for (std::size_t i = 1; i < hits.size(); ++i) {
            CHECK(hits[i - 1].score > hits[i].score || (hits[i - 1].score == hits[i].score && hits[i - 1].doc < hits[i].doc));
        }
        std::vector<std::uint32_t> ids;
        for (const auto& h : hits) ids.push_back(h.doc);
        std::sort(ids.begin(), ids.end());
        CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
        CHECK(hits.size() <= 20);
    }
}

TEST(bm25_unknown_and_empty_queries_return_nothing) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    CHECK(run(index, model, "", 10).empty());
    CHECK(run(index, model, "   !!! ", 10).empty());
    CHECK(run(index, model, "zzzunknown", 10).empty());
    CHECK(run(index, model, "apple", 0).empty());
}

TEST(bm25_unknown_terms_are_ignored_when_mixed_with_known_terms) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    CHECK(run(index, model, "apple zzzunknown", 10) == run(index, model, "apple", 10));
}

TEST(bm25_duplicate_query_terms_are_counted_once) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    CHECK(run(index, model, "apple apple APPLE", 10) == run(index, model, "apple", 10));
}

TEST(bm25_k_larger_than_match_count) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    CHECK_EQ(run(index, model, "date", 100).size(), std::size_t{1});
    CHECK_EQ(run(index, model, "apple", 1).size(), std::size_t{1});
}

TEST(bm25_idf_is_positive_even_for_ubiquitous_terms) {
    const auto index = se::build_index(make_documents({"common a", "common b", "common c"}), se::BuildOptions{});
    const Bm25Model model(index.core(), {});
    const auto id = *index.core().find("common");
    CHECK(model.idf(id) > 0.0);
    CHECK_NEAR(model.idf(id), std::log(1.0 + 0.5 / 3.5), 1e-15);
}

TEST(bm25_scratch_is_reusable_across_queries) {
    const auto index = fruit_index();
    const Bm25Model model(index.core(), {});
    se::ExhaustiveScratch scratch;
    const auto a1 = se::exhaustive_search(index, model, resolve(index.core(), "apple cherry"), 10, scratch);
    const auto b = se::exhaustive_search(index, model, resolve(index.core(), "banana"), 10, scratch);
    const auto a2 = se::exhaustive_search(index, model, resolve(index.core(), "apple cherry"), 10, scratch);
    CHECK(a1 == a2);
    CHECK_EQ(b.size(), std::size_t{2});
}

TEST(bm25_topk_heap_keeps_best_with_deterministic_ties) {
    se::TopK top(3);
    top.push({5, 1.0});
    top.push({1, 2.0});
    top.push({9, 1.0});
    CHECK(top.full());
    CHECK(!top.accepts({20, 1.0}));
    CHECK(top.accepts({2, 1.0}));
    top.push({2, 1.0});
    top.push({0, 0.5});
    const auto hits = top.take_sorted();
    CHECK_EQ(hits.size(), std::size_t{3});
    CHECK((hits[0] == SearchHit{1, 2.0}));
    CHECK((hits[1] == SearchHit{2, 1.0}));
    CHECK((hits[2] == SearchHit{5, 1.0}));
}
