#pragma once

#include <cstdint>
#include <vector>

#include "compression.h"
#include "inverted_index.h"

namespace se {

class CompressedIndex {
public:
    CompressedIndex() = default;
    CompressedIndex(IndexCore core, std::vector<std::uint8_t> bytes, std::vector<std::uint64_t> byte_offsets,
                    std::vector<BlockMeta> blocks, bool validate = true);

    static CompressedIndex compress(const InvertedIndex& index);

    const IndexCore& core() const { return core_; }

    CompressedCursor cursor(std::uint32_t term) const {
        return CompressedCursor(bytes_.data() + byte_offsets_[term], blocks_.data() + block_offsets_[term],
                                static_cast<std::uint32_t>(block_offsets_[term + 1] - block_offsets_[term]),
                                core_.doc_freq[term]);
    }

    std::vector<Posting> decode(std::uint32_t term) const;
    InvertedIndex decompress() const;

    std::uint64_t num_postings() const { return core_.num_postings(); }
    std::size_t postings_bytes() const { return bytes_.size() + blocks_.size() * sizeof(BlockMeta); }

    const std::vector<std::uint8_t>& bytes() const { return bytes_; }
    const std::vector<std::uint64_t>& byte_offsets() const { return byte_offsets_; }
    const std::vector<BlockMeta>& blocks() const { return blocks_; }

private:
    IndexCore core_;
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint64_t> byte_offsets_;
    std::vector<BlockMeta> blocks_;
    std::vector<std::uint64_t> block_offsets_;
};

}
