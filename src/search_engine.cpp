#include "search_engine.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "wand.h"

namespace se {

const char* algorithm_name(Algorithm algorithm) {
    return algorithm == Algorithm::Wand ? "wand" : "exhaustive";
}

Algorithm parse_algorithm(const std::string& name) {
    if (name == "wand") return Algorithm::Wand;
    if (name == "exhaustive") return Algorithm::Exhaustive;
    throw std::invalid_argument("unknown algorithm '" + name + "' (expected wand or exhaustive)");
}

std::vector<std::uint32_t> resolve_query(const IndexCore& core, std::string_view query) {
    const Tokenizer tokenizer(core.tokenizer);
    std::vector<std::uint32_t> ids;
    for (const std::string& token : tokenizer.tokenize(query)) {
        if (const auto id = core.find(token)) ids.push_back(*id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

SearchEngine::SearchEngine(AnyIndex index, Bm25Params params) : index_(std::move(index)) {
    std::visit(
        [&](const auto& idx) {
            model_ = Bm25Model(idx.core(), params);
            bounds_ = compute_upper_bounds(idx, model_);
        },
        index_);
}

SearchEngine SearchEngine::load(const std::string& path, Bm25Params params) {
    return SearchEngine(load_index(path), params);
}

SearchEngine SearchEngine::from_index(InvertedIndex index, Bm25Params params) {
    return SearchEngine(AnyIndex(std::move(index)), params);
}

SearchEngine SearchEngine::from_index(CompressedIndex index, Bm25Params params) {
    return SearchEngine(AnyIndex(std::move(index)), params);
}

SearchEngine SearchEngine::from_any(AnyIndex index, Bm25Params params) {
    return SearchEngine(std::move(index), params);
}

const IndexCore& SearchEngine::core() const {
    return std::visit([](const auto& idx) -> const IndexCore& { return idx.core(); }, index_);
}

std::vector<SearchHit> SearchEngine::search_ids(std::string_view query, std::size_t k, Algorithm algorithm,
                                                QueryStats* stats) {
    const std::vector<std::uint32_t> terms = resolve_query(core(), query);
    return std::visit(
        [&](const auto& idx) {
            if (algorithm == Algorithm::Wand) return wand_search(idx, model_, bounds_, terms, k, stats);
            return exhaustive_search(idx, model_, terms, k, scratch_, stats);
        },
        index_);
}

std::vector<SearchResult> SearchEngine::search(std::string_view query, std::size_t k, Algorithm algorithm) {
    const std::vector<SearchHit> hits = search_ids(query, k, algorithm);
    const IndexCore& c = core();
    std::vector<SearchResult> results;
    results.reserve(hits.size());
    for (const SearchHit& hit : hits) {
        const DocInfo& info = c.docs[hit.doc];
        results.push_back({hit.doc, hit.score, info.external_id, info.title, info.snippet});
    }
    return results;
}

}
