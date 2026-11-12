#pragma once

#include <cstddef>
#include <istream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "search_engine.h"

namespace se {

struct Query {
    std::string id;
    std::string text;
};

using Qrels = std::unordered_map<std::string, std::unordered_set<std::string>>;

std::vector<Query> parse_queries(std::istream& in);
std::vector<Query> load_queries(const std::string& path);

Qrels parse_qrels(std::istream& in);
Qrels load_qrels(const std::string& path);

struct EvalResult {
    std::size_t k = 0;
    std::size_t queries_evaluated = 0;
    std::size_t queries_skipped = 0;
    double mrr = 0.0;
    double recall = 0.0;
};

double reciprocal_rank(const std::vector<std::string>& ranked_ids, const std::unordered_set<std::string>& relevant,
                       std::size_t k);
double recall_at(const std::vector<std::string>& ranked_ids, const std::unordered_set<std::string>& relevant,
                 std::size_t k);

EvalResult evaluate(SearchEngine& engine, const std::vector<Query>& queries, const Qrels& qrels, std::size_t k,
                    Algorithm algorithm);

}
