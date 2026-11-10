#include "bm25.h"

#include <stdexcept>

#include "compressed_index.h"

namespace se {

Bm25Model::Bm25Model(const IndexCore& core, Bm25Params params) : params_(params) {
    if (!(params.k1 >= 0.0) || !std::isfinite(params.k1)) throw std::invalid_argument("k1 must be a non-negative number");
    if (!(params.b >= 0.0 && params.b <= 1.0)) throw std::invalid_argument("b must lie in [0, 1]");

    const std::uint64_t n = core.num_docs();
    idf_.resize(core.num_terms());
    for (std::size_t t = 0; t < core.num_terms(); ++t) idf_[t] = idf_from_counts(n, core.doc_freq[t]);

    const double avg = core.avg_doc_length() > 0.0 ? core.avg_doc_length() : 1.0;
    norm_.resize(core.docs.size());
    for (std::size_t d = 0; d < core.docs.size(); ++d) {
        norm_[d] = params.k1 * (1.0 - params.b + params.b * static_cast<double>(core.docs[d].length) / avg);
    }
}

template <class Index>
std::vector<SearchHit> exhaustive_search(const Index& index, const Bm25Model& model,
                                         std::span<const std::uint32_t> terms, std::size_t k,
                                         ExhaustiveScratch& scratch, QueryStats* stats) {
    if (k == 0 || terms.empty()) return {};

    const std::uint32_t n = index.core().num_docs();
    if (scratch.accumulators.size() < n) scratch.accumulators.assign(n, 0.0);
    scratch.touched.clear();

    for (const std::uint32_t term : terms) {
        const double idf = model.idf(term);
        for (auto cursor = index.cursor(term); cursor.valid(); cursor.next()) {
            const std::uint32_t doc = cursor.doc();
            double& acc = scratch.accumulators[doc];
            if (acc == 0.0) scratch.touched.push_back(doc);
            acc += model.contribution(idf, cursor.tf(), doc);
        }
    }

    TopK top(k);
    for (const std::uint32_t doc : scratch.touched) {
        top.push({doc, scratch.accumulators[doc]});
        scratch.accumulators[doc] = 0.0;
    }
    if (stats) stats->docs_scored += scratch.touched.size();
    return top.take_sorted();
}

template <class Index>
std::vector<double> compute_upper_bounds(const Index& index, const Bm25Model& model) {
    const IndexCore& core = index.core();
    std::vector<double> bounds(core.num_terms(), 0.0);
    for (std::uint32_t term = 0; term < core.num_terms(); ++term) {
        const double idf = model.idf(term);
        double best = 0.0;
        for (auto cursor = index.cursor(term); cursor.valid(); cursor.next()) {
            best = std::max(best, model.contribution(idf, cursor.tf(), cursor.doc()));
        }
        bounds[term] = best * (1.0 + 1e-9);
    }
    return bounds;
}

template std::vector<SearchHit> exhaustive_search<InvertedIndex>(const InvertedIndex&, const Bm25Model&,
                                                                 std::span<const std::uint32_t>, std::size_t,
                                                                 ExhaustiveScratch&, QueryStats*);
template std::vector<double> compute_upper_bounds<InvertedIndex>(const InvertedIndex&, const Bm25Model&);

template std::vector<SearchHit> exhaustive_search<CompressedIndex>(const CompressedIndex&, const Bm25Model&,
                                                                   std::span<const std::uint32_t>, std::size_t,
                                                                   ExhaustiveScratch&, QueryStats*);
template std::vector<double> compute_upper_bounds<CompressedIndex>(const CompressedIndex&, const Bm25Model&);

}
