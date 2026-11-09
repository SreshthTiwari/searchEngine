#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "tokenizer.h"

namespace se {

constexpr std::uint32_t kEndDoc = std::numeric_limits<std::uint32_t>::max();

struct Posting {
    std::uint32_t doc;
    std::uint32_t tf;

    friend bool operator==(const Posting&, const Posting&) = default;
};

struct DocInfo {
    std::string external_id;
    std::string title;
    std::string snippet;
    std::uint32_t length = 0;

    bool operator==(const DocInfo&) const = default;
};

struct StringHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
};

class IndexCore {
public:
    std::vector<std::string> terms;
    std::vector<std::uint32_t> doc_freq;
    std::vector<DocInfo> docs;
    TokenizerOptions tokenizer;

    void finalize();

    std::optional<std::uint32_t> find(std::string_view term) const;

    std::uint32_t num_docs() const { return static_cast<std::uint32_t>(docs.size()); }
    std::size_t num_terms() const { return terms.size(); }
    std::uint64_t total_length() const { return total_length_; }
    std::uint64_t num_postings() const { return num_postings_; }
    double avg_doc_length() const {
        return docs.empty() ? 0.0 : static_cast<double>(total_length_) / static_cast<double>(docs.size());
    }

    bool operator==(const IndexCore& other) const {
        return terms == other.terms && doc_freq == other.doc_freq && docs == other.docs &&
               tokenizer == other.tokenizer;
    }

private:
    std::unordered_map<std::string, std::uint32_t, StringHash, std::equal_to<>> lookup_;
    std::uint64_t total_length_ = 0;
    std::uint64_t num_postings_ = 0;
};

class VectorCursor {
public:
    VectorCursor() = default;
    explicit VectorCursor(std::span<const Posting> list) : cur_(list.data()), end_(list.data() + list.size()) {}

    bool valid() const { return cur_ != end_; }
    std::uint32_t doc() const { return cur_ != end_ ? cur_->doc : kEndDoc; }
    std::uint32_t tf() const { return cur_->tf; }
    void next() { ++cur_; }

    void next_geq(std::uint32_t target) {
        if (cur_ == end_ || cur_->doc >= target) return;
        const Posting* lo = cur_;
        std::size_t step = 1;
        while (static_cast<std::size_t>(end_ - lo) > step && lo[step].doc < target) {
            lo += step;
            step <<= 1;
        }
        const Posting* hi = static_cast<std::size_t>(end_ - lo) > step ? lo + step + 1 : end_;
        cur_ = std::lower_bound(lo, hi, target, [](const Posting& p, std::uint32_t v) { return p.doc < v; });
    }

private:
    const Posting* cur_ = nullptr;
    const Posting* end_ = nullptr;
};

class InvertedIndex {
public:
    InvertedIndex() = default;
    InvertedIndex(IndexCore core, std::vector<Posting> postings);

    const IndexCore& core() const { return core_; }

    std::span<const Posting> postings(std::uint32_t term) const {
        return {postings_.data() + offsets_[term], static_cast<std::size_t>(offsets_[term + 1] - offsets_[term])};
    }

    VectorCursor cursor(std::uint32_t term) const { return VectorCursor(postings(term)); }

    std::uint64_t num_postings() const { return postings_.size(); }
    std::size_t postings_bytes() const { return postings_.size() * sizeof(Posting); }
    const std::vector<Posting>& all_postings() const { return postings_; }

    bool operator==(const InvertedIndex& other) const {
        return core_ == other.core_ && postings_ == other.postings_;
    }

private:
    IndexCore core_;
    std::vector<Posting> postings_;
    std::vector<std::uint64_t> offsets_;
};

}
