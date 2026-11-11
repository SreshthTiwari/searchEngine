#include "wand.h"

#include <utility>

#include "compressed_index.h"

namespace se {

template <class Index>
std::vector<SearchHit> wand_search(const Index& index, const Bm25Model& model,
                                   std::span<const double> upper_bounds,
                                   std::span<const std::uint32_t> terms, std::size_t k, QueryStats* stats) {
    using Cursor = decltype(index.cursor(std::uint32_t{0}));

    if (k == 0 || terms.empty()) return {};

    struct Entry {
        Cursor cursor;
        double idf;
        double bound;
    };

    std::vector<Entry> entries;
    entries.reserve(terms.size());
    for (const std::uint32_t term : terms) {
        entries.push_back({index.cursor(term), model.idf(term), upper_bounds[term]});
    }

    std::vector<std::uint32_t> order;
    order.reserve(entries.size());
    for (std::uint32_t i = 0; i < entries.size(); ++i) {
        if (entries[i].cursor.valid()) order.push_back(i);
    }

    TopK top(k);
    std::uint64_t scored = 0;

    auto doc_of = [&](std::uint32_t i) { return entries[i].cursor.doc(); };

    while (true) {
        for (std::size_t i = 1; i < order.size(); ++i) {
            const std::uint32_t current = order[i];
            std::size_t j = i;
            while (j > 0 && (doc_of(order[j - 1]) > doc_of(current) ||
                             (doc_of(order[j - 1]) == doc_of(current) && order[j - 1] > current))) {
                order[j] = order[j - 1];
                --j;
            }
            order[j] = current;
        }
        while (!order.empty() && !entries[order.back()].cursor.valid()) order.pop_back();
        if (order.empty()) break;

        const double threshold = top.full() ? top.threshold() : -1.0;
        double bound_sum = 0.0;
        std::size_t pivot = order.size();
        for (std::size_t i = 0; i < order.size(); ++i) {
            bound_sum += entries[order[i]].bound;
            if (bound_sum > threshold) {
                pivot = i;
                break;
            }
        }
        if (pivot == order.size()) break;

        const std::uint32_t pivot_doc = doc_of(order[pivot]);
        if (doc_of(order[0]) == pivot_doc) {
            double score = 0.0;
            for (Entry& e : entries) {
                if (e.cursor.valid() && e.cursor.doc() == pivot_doc) {
                    score += model.contribution(e.idf, e.cursor.tf(), pivot_doc);
                    e.cursor.next();
                }
            }
            ++scored;
            top.push({pivot_doc, score});
        } else {
            std::uint32_t best = order[0];
            for (std::size_t i = 1; i < pivot; ++i) {
                if (doc_of(order[i]) < pivot_doc && entries[order[i]].bound > entries[best].bound) best = order[i];
            }
            entries[best].cursor.next_geq(pivot_doc);
        }
    }

    if (stats) stats->docs_scored += scored;
    return top.take_sorted();
}

template std::vector<SearchHit> wand_search<InvertedIndex>(const InvertedIndex&, const Bm25Model&,
                                                           std::span<const double>,
                                                           std::span<const std::uint32_t>, std::size_t,
                                                           QueryStats*);
template std::vector<SearchHit> wand_search<CompressedIndex>(const CompressedIndex&, const Bm25Model&,
                                                             std::span<const double>,
                                                             std::span<const std::uint32_t>, std::size_t,
                                                             QueryStats*);

}
