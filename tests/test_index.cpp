#include <stdexcept>
#include <string>
#include <vector>

#include "index_builder.h"
#include "inverted_index.h"
#include "test_corpus.h"
#include "test_util.h"

using se::BuildOptions;
using se::Posting;
using testing::make_documents;

namespace {

se::InvertedIndex small_index() {
    return se::build_index(make_documents({
                               "machine learning machine",
                               "deep learning",
                               "machine vision systems",
                               "",
                           }),
                           BuildOptions{});
}

std::vector<Posting> list_of(const se::InvertedIndex& index, const std::string& term) {
    const auto id = index.core().find(term);
    CHECK(id.has_value());
    if (!id) return {};
    const auto span = index.postings(*id);
    return {span.begin(), span.end()};
}

}

TEST(index_posting_lists_and_term_frequencies) {
    const auto index = small_index();
    CHECK((list_of(index, "machine") == std::vector<Posting>{{0, 2}, {2, 1}}));
    CHECK((list_of(index, "learning") == std::vector<Posting>{{0, 1}, {1, 1}}));
    CHECK((list_of(index, "vision") == std::vector<Posting>{{2, 1}}));
    CHECK((list_of(index, "deep") == std::vector<Posting>{{1, 1}}));
}

TEST(index_document_frequency_and_statistics) {
    const auto index = small_index();
    const auto& core = index.core();
    CHECK_EQ(core.num_docs(), std::uint32_t{4});
    CHECK_EQ(core.num_terms(), std::size_t{5});
    CHECK_EQ(core.doc_freq[*core.find("machine")], std::uint32_t{2});
    CHECK_EQ(core.doc_freq[*core.find("systems")], std::uint32_t{1});
    CHECK_EQ(core.total_length(), std::uint64_t{8});
    CHECK_NEAR(core.avg_doc_length(), 2.0, 1e-12);
    CHECK_EQ(core.num_postings(), std::uint64_t{8 - 1});
    CHECK_EQ(index.num_postings(), std::uint64_t{7});
}

TEST(index_records_document_lengths_and_metadata) {
    const auto index = small_index();
    CHECK_EQ(index.core().docs[0].length, std::uint32_t{3});
    CHECK_EQ(index.core().docs[3].length, std::uint32_t{0});
    CHECK_EQ(index.core().docs[1].external_id, std::string("d1"));
    CHECK_EQ(index.core().docs[0].snippet, std::string("machine learning machine"));
}

TEST(index_terms_are_sorted_and_lookup_is_exact) {
    const auto index = small_index();
    const auto& terms = index.core().terms;
    for (std::size_t i = 1; i < terms.size(); ++i) CHECK(terms[i - 1] < terms[i]);
    CHECK(!index.core().find("unknown").has_value());
    CHECK(!index.core().find("").has_value());
    CHECK(!index.core().find("machin").has_value());
}

TEST(index_posting_lists_are_sorted_by_document_id) {
    const auto docs = testing::random_documents(11, 300, 50, 40);
    const auto index = se::build_index(docs, BuildOptions{});
    for (std::uint32_t t = 0; t < index.core().num_terms(); ++t) {
        const auto list = index.postings(t);
        for (std::size_t i = 1; i < list.size(); ++i) CHECK(list[i - 1].doc < list[i].doc);
        CHECK_EQ(list.size(), std::size_t{index.core().doc_freq[t]});
    }
}

TEST(index_empty_collection) {
    const auto index = se::build_index({}, BuildOptions{});
    CHECK_EQ(index.core().num_docs(), std::uint32_t{0});
    CHECK_EQ(index.core().num_terms(), std::size_t{0});
    CHECK_NEAR(index.core().avg_doc_length(), 0.0, 0.0);
}

TEST(index_all_empty_documents) {
    const auto index = se::build_index(make_documents({"", "!!!", "  "}), BuildOptions{});
    CHECK_EQ(index.core().num_docs(), std::uint32_t{3});
    CHECK_EQ(index.core().num_terms(), std::size_t{0});
}

TEST(index_stopword_option_changes_vocabulary_and_lengths) {
    BuildOptions options;
    options.tokenizer.remove_stopwords = true;
    const auto index = se::build_index(make_documents({"the cat and the dog"}), options);
    CHECK(!index.core().find("the").has_value());
    CHECK(index.core().find("cat").has_value());
    CHECK_EQ(index.core().docs[0].length, std::uint32_t{2});
}

TEST(index_rejects_inconsistent_construction) {
    se::IndexCore core;
    core.terms = {"a"};
    core.doc_freq = {1};
    core.docs = {se::DocInfo{"x", "", "", 1}};
    CHECK_THROWS(se::InvertedIndex(core, {}), std::invalid_argument);
    CHECK_THROWS(se::InvertedIndex(core, {Posting{5, 1}}), std::invalid_argument);
    CHECK_THROWS(se::InvertedIndex(core, {Posting{0, 0}}), std::invalid_argument);

    se::IndexCore unsorted;
    unsorted.terms = {"b", "a"};
    unsorted.doc_freq = {1, 1};
    unsorted.docs = {se::DocInfo{"x", "", "", 2}};
    CHECK_THROWS(se::InvertedIndex(unsorted, {Posting{0, 1}, Posting{0, 1}}), std::invalid_argument);
}

TEST(index_vector_cursor_next_geq) {
    std::vector<Posting> list;
    for (std::uint32_t d = 0; d < 1000; d += 3) list.push_back({d, 1});
    se::VectorCursor cursor{std::span<const Posting>(list)};
    CHECK_EQ(cursor.doc(), std::uint32_t{0});
    cursor.next_geq(0);
    CHECK_EQ(cursor.doc(), std::uint32_t{0});
    cursor.next_geq(1);
    CHECK_EQ(cursor.doc(), std::uint32_t{3});
    cursor.next_geq(500);
    CHECK_EQ(cursor.doc(), std::uint32_t{501});
    cursor.next_geq(501);
    CHECK_EQ(cursor.doc(), std::uint32_t{501});
    cursor.next_geq(998);
    CHECK_EQ(cursor.doc(), std::uint32_t{999});
    cursor.next_geq(1000);
    CHECK(!cursor.valid());
    CHECK_EQ(cursor.doc(), se::kEndDoc);
}
