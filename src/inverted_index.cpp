#include "inverted_index.h"

#include <stdexcept>
#include <utility>

namespace se {

void IndexCore::finalize() {
    if (terms.size() != doc_freq.size()) {
        throw std::invalid_argument("term and document-frequency counts differ");
    }
    if (docs.size() >= kEndDoc) throw std::invalid_argument("too many documents");

    const std::uint64_t n = docs.size();
    num_postings_ = 0;
    for (std::size_t t = 0; t < terms.size(); ++t) {
        if (t > 0 && !(terms[t - 1] < terms[t])) {
            throw std::invalid_argument("terms must be strictly increasing");
        }
        if (doc_freq[t] == 0 || doc_freq[t] > n) {
            throw std::invalid_argument("document frequency out of range");
        }
        num_postings_ += doc_freq[t];
    }

    total_length_ = 0;
    for (const DocInfo& doc : docs) total_length_ += doc.length;

    lookup_.clear();
    lookup_.reserve(terms.size());
    for (std::size_t t = 0; t < terms.size(); ++t) {
        lookup_.emplace(terms[t], static_cast<std::uint32_t>(t));
    }
}

std::optional<std::uint32_t> IndexCore::find(std::string_view term) const {
    const auto it = lookup_.find(term);
    if (it == lookup_.end()) return std::nullopt;
    return it->second;
}

InvertedIndex::InvertedIndex(IndexCore core, std::vector<Posting> postings)
    : core_(std::move(core)), postings_(std::move(postings)) {
    core_.finalize();
    if (postings_.size() != core_.num_postings()) {
        throw std::invalid_argument("posting count does not match document frequencies");
    }

    offsets_.assign(core_.num_terms() + 1, 0);
    for (std::size_t t = 0; t < core_.num_terms(); ++t) {
        offsets_[t + 1] = offsets_[t] + core_.doc_freq[t];
    }

    const std::uint32_t n = core_.num_docs();
    for (std::size_t t = 0; t < core_.num_terms(); ++t) {
        for (std::uint64_t i = offsets_[t]; i < offsets_[t + 1]; ++i) {
            const Posting& p = postings_[i];
            if (p.doc >= n) throw std::invalid_argument("posting references unknown document");
            if (p.tf == 0) throw std::invalid_argument("posting has zero term frequency");
            if (i > offsets_[t] && postings_[i - 1].doc >= p.doc) {
                throw std::invalid_argument("posting list is not strictly sorted by document id");
            }
        }
    }
}

}
