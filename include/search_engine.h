#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "bm25.h"
#include "compressed_index.h"
#include "inverted_index.h"
#include "storage.h"
#include "tokenizer.h"

namespace se {

enum class Algorithm { Exhaustive, Wand };

const char* algorithm_name(Algorithm algorithm);
Algorithm parse_algorithm(const std::string& name);

struct SearchResult {
    std::uint32_t doc;
    double score;
    std::string external_id;
    std::string title;
    std::string snippet;
};

std::vector<std::uint32_t> resolve_query(const IndexCore& core, std::string_view query);

class SearchEngine {
public:
    static SearchEngine load(const std::string& path, Bm25Params params = {});
    static SearchEngine from_index(InvertedIndex index, Bm25Params params = {});
    static SearchEngine from_index(CompressedIndex index, Bm25Params params = {});
    static SearchEngine from_any(AnyIndex index, Bm25Params params = {});

    const IndexCore& core() const;
    bool compressed() const { return std::holds_alternative<CompressedIndex>(index_); }
    const Bm25Params& params() const { return model_.params(); }

    std::vector<SearchHit> search_ids(std::string_view query, std::size_t k, Algorithm algorithm,
                                      QueryStats* stats = nullptr);

    std::vector<SearchResult> search(std::string_view query, std::size_t k, Algorithm algorithm = Algorithm::Wand);

private:
    SearchEngine(AnyIndex index, Bm25Params params);

    AnyIndex index_;
    Bm25Model model_;
    std::vector<double> bounds_;
    ExhaustiveScratch scratch_;
};

}
