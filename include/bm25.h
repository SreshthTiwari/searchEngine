#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "inverted_index.h"

namespace se {

struct Bm25Params {
    double k1 = 1.2;
    double b = 0.75;
};

class Bm25Model {
public:
    Bm25Model() = default;
    Bm25Model(const IndexCore& core, Bm25Params params);

    const Bm25Params& params() const { return params_; }

    double idf(std::uint32_t term) const { return idf_[term]; }

    double contribution(double idf, std::uint32_t tf, std::uint32_t doc) const {
        const double t = static_cast<double>(tf);
        return idf * (t * (params_.k1 + 1.0)) / (t + norm_[doc]);
    }

    static double idf_from_counts(std::uint64_t num_docs, std::uint64_t doc_freq) {
        const double n = static_cast<double>(num_docs);
        const double df = static_cast<double>(doc_freq);
        return std::log(1.0 + (n - df + 0.5) / (df + 0.5));
    }

private:
    Bm25Params params_;
    std::vector<double> idf_;
    std::vector<double> norm_;
};

struct SearchHit {
    std::uint32_t doc;
    double score;

    friend bool operator==(const SearchHit&, const SearchHit&) = default;
};

struct QueryStats {
    std::uint64_t docs_scored = 0;
};

class TopK {
public:
    explicit TopK(std::size_t k) : k_(k) { heap_.reserve(std::min<std::size_t>(k, 1024) + 1); }

    static bool better(const SearchHit& a, const SearchHit& b) {
        return a.score > b.score || (a.score == b.score && a.doc < b.doc);
    }

    bool full() const { return heap_.size() >= k_; }

    double threshold() const { return heap_.front().score; }

    bool accepts(const SearchHit& hit) const {
        return k_ > 0 && (!full() || better(hit, heap_.front()));
    }

    void push(const SearchHit& hit) {
        if (k_ == 0) return;
        if (heap_.size() < k_) {
            heap_.push_back(hit);
            std::push_heap(heap_.begin(), heap_.end(), better);
        } else if (better(hit, heap_.front())) {
            std::pop_heap(heap_.begin(), heap_.end(), better);
            heap_.back() = hit;
            std::push_heap(heap_.begin(), heap_.end(), better);
        }
    }

    std::vector<SearchHit> take_sorted() {
        std::sort_heap(heap_.begin(), heap_.end(), better);
        return std::move(heap_);
    }

private:
    std::size_t k_;
    std::vector<SearchHit> heap_;
};

struct ExhaustiveScratch {
    std::vector<double> accumulators;
    std::vector<std::uint32_t> touched;
};

template <class Index>
std::vector<SearchHit> exhaustive_search(const Index& index, const Bm25Model& model,
                                         std::span<const std::uint32_t> terms, std::size_t k,
                                         ExhaustiveScratch& scratch, QueryStats* stats = nullptr);

template <class Index>
std::vector<double> compute_upper_bounds(const Index& index, const Bm25Model& model);

}
