#include "evaluation.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "document.h"

namespace se {

std::vector<Query> parse_queries(std::istream& in) {
    std::vector<Query> queries;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        const std::size_t tab = line.find('\t');
        Query q;
        if (tab == std::string::npos) {
            q.id = std::to_string(queries.size());
            q.text = line;
        } else {
            q.id = line.substr(0, tab);
            q.text = line.substr(tab + 1);
            if (q.id.empty()) throw ParseError("line " + std::to_string(line_number) + ": empty query id");
        }
        queries.push_back(std::move(q));
    }
    return queries;
}

std::vector<Query> load_queries(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ParseError("cannot open queries file '" + path + "'");
    return parse_queries(in);
}

Qrels parse_qrels(std::istream& in) {
    Qrels qrels;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.find_first_not_of(" \t\r") == std::string::npos) continue;
        std::istringstream fields(line);
        std::string qid;
        std::string ignored;
        std::string doc;
        long long relevance = 0;
        if (!(fields >> qid >> ignored >> doc >> relevance)) {
            throw ParseError("line " + std::to_string(line_number) + ": expected 'qid 0 docid relevance'");
        }
        if (relevance > 0) qrels[qid].insert(doc);
    }
    return qrels;
}

Qrels load_qrels(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw ParseError("cannot open qrels file '" + path + "'");
    return parse_qrels(in);
}

double reciprocal_rank(const std::vector<std::string>& ranked_ids, const std::unordered_set<std::string>& relevant,
                       std::size_t k) {
    const std::size_t limit = std::min(k, ranked_ids.size());
    for (std::size_t i = 0; i < limit; ++i) {
        if (relevant.count(ranked_ids[i])) return 1.0 / static_cast<double>(i + 1);
    }
    return 0.0;
}

double recall_at(const std::vector<std::string>& ranked_ids, const std::unordered_set<std::string>& relevant,
                 std::size_t k) {
    if (relevant.empty()) return 0.0;
    const std::size_t limit = std::min(k, ranked_ids.size());
    std::unordered_set<std::string> found;
    for (std::size_t i = 0; i < limit; ++i) {
        if (relevant.count(ranked_ids[i])) found.insert(ranked_ids[i]);
    }
    return static_cast<double>(found.size()) / static_cast<double>(relevant.size());
}

EvalResult evaluate(SearchEngine& engine, const std::vector<Query>& queries, const Qrels& qrels, std::size_t k,
                    Algorithm algorithm) {
    EvalResult result;
    result.k = k;
    double mrr_sum = 0.0;
    double recall_sum = 0.0;
    for (const Query& query : queries) {
        const auto it = qrels.find(query.id);
        if (it == qrels.end() || it->second.empty()) {
            ++result.queries_skipped;
            continue;
        }
        const auto hits = engine.search_ids(query.text, k, algorithm);
        std::vector<std::string> ranked;
        ranked.reserve(hits.size());
        for (const SearchHit& hit : hits) ranked.push_back(engine.core().docs[hit.doc].external_id);
        mrr_sum += reciprocal_rank(ranked, it->second, k);
        recall_sum += recall_at(ranked, it->second, k);
        ++result.queries_evaluated;
    }
    if (result.queries_evaluated > 0) {
        result.mrr = mrr_sum / static_cast<double>(result.queries_evaluated);
        result.recall = recall_sum / static_cast<double>(result.queries_evaluated);
    }
    return result;
}

}
