#include "compressed_index.h"

#include <stdexcept>
#include <utility>

namespace se {

CompressedIndex::CompressedIndex(IndexCore core, std::vector<std::uint8_t> bytes,
                                 std::vector<std::uint64_t> byte_offsets, std::vector<BlockMeta> blocks,
                                 bool validate)
    : core_(std::move(core)),
      bytes_(std::move(bytes)),
      byte_offsets_(std::move(byte_offsets)),
      blocks_(std::move(blocks)) {
    core_.finalize();
    const std::size_t terms = core_.num_terms();

    if (byte_offsets_.size() != terms + 1 || byte_offsets_.front() != 0 || byte_offsets_.back() != bytes_.size()) {
        throw std::invalid_argument("inconsistent compressed term offsets");
    }
    for (std::size_t t = 0; t < terms; ++t) {
        if (byte_offsets_[t] > byte_offsets_[t + 1]) throw std::invalid_argument("compressed offsets decrease");
    }

    block_offsets_.assign(terms + 1, 0);
    for (std::size_t t = 0; t < terms; ++t) {
        block_offsets_[t + 1] = block_offsets_[t] + block_count_for(core_.doc_freq[t]);
    }
    if (block_offsets_.back() != blocks_.size()) throw std::invalid_argument("inconsistent compressed block count");

    if (validate) {
        for (std::size_t t = 0; t < terms; ++t) {
            const std::span<const BlockMeta> list_blocks(blocks_.data() + block_offsets_[t],
                                                         block_offsets_[t + 1] - block_offsets_[t]);
            if (!validate_encoded_list(bytes_.data() + byte_offsets_[t], byte_offsets_[t + 1] - byte_offsets_[t],
                                       list_blocks, core_.doc_freq[t], core_.num_docs())) {
                throw std::invalid_argument("corrupt compressed posting list");
            }
        }
    }
}

CompressedIndex CompressedIndex::compress(const InvertedIndex& index) {
    const IndexCore& core = index.core();
    std::vector<std::uint8_t> bytes;
    bytes.reserve(index.num_postings() * 2);
    std::vector<std::uint64_t> byte_offsets(core.num_terms() + 1, 0);
    std::vector<BlockMeta> blocks;
    blocks.reserve(block_count_for(index.num_postings()) + core.num_terms());

    for (std::uint32_t t = 0; t < core.num_terms(); ++t) {
        append_encoded_list(index.postings(t), bytes, blocks);
        byte_offsets[t + 1] = bytes.size();
    }
    bytes.shrink_to_fit();
    return CompressedIndex(core, std::move(bytes), std::move(byte_offsets), std::move(blocks), false);
}

std::vector<Posting> CompressedIndex::decode(std::uint32_t term) const {
    std::vector<Posting> out;
    out.reserve(core_.doc_freq[term]);
    for (auto cursor = this->cursor(term); cursor.valid(); cursor.next()) out.push_back({cursor.doc(), cursor.tf()});
    return out;
}

InvertedIndex CompressedIndex::decompress() const {
    std::vector<Posting> postings;
    postings.reserve(core_.num_postings());
    for (std::uint32_t t = 0; t < core_.num_terms(); ++t) {
        for (auto cursor = this->cursor(t); cursor.valid(); cursor.next()) postings.push_back({cursor.doc(), cursor.tf()});
    }
    return InvertedIndex(core_, std::move(postings));
}

}
