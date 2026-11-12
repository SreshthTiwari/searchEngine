#include <sstream>
#include <string>

#include "evaluation.h"
#include "index_builder.h"
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

TEST(evaluation_metrics_on_small_example) {
    const std::unordered_set<std::string> relevant = {"a", "c"};
    const std::vector<std::string> ranked = {"x", "a", "y", "c"};
    CHECK_NEAR(se::reciprocal_rank(ranked, relevant, 10), 0.5, 1e-12);
    CHECK_NEAR(se::reciprocal_rank(ranked, relevant, 1), 0.0, 1e-12);
    CHECK_NEAR(se::recall_at(ranked, relevant, 10), 1.0, 1e-12);
    CHECK_NEAR(se::recall_at(ranked, relevant, 2), 0.5, 1e-12);
    CHECK_NEAR(se::recall_at(ranked, {}, 10), 0.0, 1e-12);
}

TEST(evaluation_end_to_end_with_parsed_files) {
    auto engine = se::SearchEngine::from_index(se::build_index(sample_docs(), se::BuildOptions{}));
    std::istringstream queries_in("q1\tmachine learning algorithms\nq2\ttomato soup\nq3\tunjudged query\n");
    std::istringstream qrels_in("q1 0 P1 1\nq1 0 P2 0\nq2\t0\tP3\t2\n");
    const auto queries = se::parse_queries(queries_in);
    const auto qrels = se::parse_qrels(qrels_in);
    CHECK_EQ(queries.size(), std::size_t{3});
    CHECK_EQ(qrels.at("q1").size(), std::size_t{1});
    const auto result = se::evaluate(engine, queries, qrels, 10, Algorithm::Wand);
    CHECK_EQ(result.queries_evaluated, std::size_t{2});
    CHECK_EQ(result.queries_skipped, std::size_t{1});
    CHECK_NEAR(result.mrr, 1.0, 1e-12);
    CHECK_NEAR(result.recall, 1.0, 1e-12);
}

TEST(evaluation_rejects_malformed_inputs) {
    std::istringstream bad_qrels("q1 0 P1\n");
    CHECK_THROWS(se::parse_qrels(bad_qrels), se::ParseError);
    CHECK_THROWS(se::load_queries("/nonexistent/queries.tsv"), se::ParseError);
    CHECK_THROWS(se::load_qrels("/nonexistent/qrels.tsv"), se::ParseError);
}

TEST(evaluation_queries_without_ids_get_sequential_ids) {
    std::istringstream in("first query\nsecond query\n");
    const auto queries = se::parse_queries(in);
    CHECK_EQ(queries.size(), std::size_t{2});
    CHECK_EQ(queries[1].id, std::string("1"));
    CHECK_EQ(queries[1].text, std::string("second query"));
}
