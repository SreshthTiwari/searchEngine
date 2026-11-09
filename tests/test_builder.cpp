#include <string>
#include <vector>

#include "index_builder.h"
#include "test_corpus.h"
#include "test_util.h"

using se::BuildOptions;

namespace {

se::InvertedIndex build_with(const std::vector<se::Document>& docs, std::size_t threads) {
    BuildOptions options;
    options.threads = threads;
    return se::build_index(docs, options);
}

}

TEST(builder_multithreaded_matches_single_threaded) {
    const auto docs = testing::random_documents(3, 2000, 400, 60);
    const auto reference = build_with(docs, 1);
    for (std::size_t threads : {2, 3, 4, 7, 8}) {
        const auto parallel = build_with(docs, threads);
        CHECK(parallel == reference);
    }
}

TEST(builder_more_threads_than_documents) {
    const auto docs = testing::random_documents(5, 3, 20, 10);
    const auto reference = build_with(docs, 1);
    CHECK(build_with(docs, 16) == reference);
}

TEST(builder_empty_input_with_many_threads) {
    const auto index = build_with({}, 8);
    CHECK_EQ(index.core().num_docs(), std::uint32_t{0});
    CHECK_EQ(index.core().num_terms(), std::size_t{0});
}

TEST(builder_documents_with_no_tokens_are_kept) {
    auto docs = testing::random_documents(9, 50, 30, 10);
    docs[10].text.clear();
    docs[11].text = "!!! ???";
    const auto reference = build_with(docs, 1);
    const auto parallel = build_with(docs, 4);
    CHECK(parallel == reference);
    CHECK_EQ(parallel.core().docs[10].length, std::uint32_t{0});
    CHECK_EQ(parallel.core().docs[11].length, std::uint32_t{0});
}

TEST(builder_posting_lists_stay_sorted_across_partitions) {
    const auto docs = testing::random_documents(21, 1500, 100, 30);
    const auto index = build_with(docs, 6);
    for (std::uint32_t t = 0; t < index.core().num_terms(); ++t) {
        const auto list = index.postings(t);
        for (std::size_t i = 1; i < list.size(); ++i) CHECK(list[i - 1].doc < list[i].doc);
    }
}

TEST(builder_zero_threads_means_hardware_concurrency) {
    CHECK(se::resolve_thread_count(0) >= 1);
    CHECK_EQ(se::resolve_thread_count(5), std::size_t{5});
    const auto docs = testing::random_documents(2, 100, 30, 10);
    CHECK(build_with(docs, 0) == build_with(docs, 1));
}
