#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "inverted_index.h"

namespace se {

constexpr std::uint32_t kBlockSize = 128;

struct BlockMeta {
    std::uint32_t last_doc;
    std::uint32_t byte_offset;

    friend bool operator==(const BlockMeta&, const BlockMeta&) = default;
};

void vbyte_encode(std::uint32_t value, std::vector<std::uint8_t>& out);

inline std::uint32_t vbyte_decode(const std::uint8_t*& p) noexcept {
    std::uint32_t result = 0;
    unsigned shift = 0;
    while (true) {
        const std::uint8_t byte = *p++;
        result |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80)) return result;
        shift += 7;
    }
}

bool vbyte_decode_checked(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& out) noexcept;

std::vector<std::uint32_t> delta_encode(std::span<const std::uint32_t> sorted);
std::vector<std::uint32_t> delta_decode(std::span<const std::uint32_t> gaps);

void append_encoded_list(std::span<const Posting> list, std::vector<std::uint8_t>& bytes,
                         std::vector<BlockMeta>& blocks);

bool validate_encoded_list(const std::uint8_t* bytes, std::size_t size, std::span<const BlockMeta> blocks,
                           std::uint32_t count, std::uint32_t num_docs) noexcept;

inline std::size_t block_count_for(std::uint64_t postings) { return (postings + kBlockSize - 1) / kBlockSize; }

class CompressedCursor {
public:
    CompressedCursor() = default;

    CompressedCursor(const std::uint8_t* bytes, const BlockMeta* blocks, std::uint32_t block_count,
                     std::uint32_t count)
        : bytes_(bytes), blocks_(blocks), block_count_(block_count), count_(count) {
        if (count_ == 0 || block_count_ == 0) {
            block_ = block_count_;
        } else {
            load_block(0);
        }
    }

    bool valid() const { return block_ < block_count_; }
    std::uint32_t doc() const { return valid() ? docs_[pos_] : kEndDoc; }

    std::uint32_t tf() {
        if (!tf_ready_) decode_tfs();
        return tfs_[pos_];
    }

    void next() {
        if (++pos_ < block_n_) return;
        if (block_ + 1 < block_count_) {
            load_block(block_ + 1);
        } else {
            block_ = block_count_;
        }
    }

    void next_geq(std::uint32_t target) {
        if (!valid() || docs_[pos_] >= target) return;
        if (target > blocks_[block_].last_doc) {
            const BlockMeta* first = blocks_ + block_ + 1;
            const BlockMeta* last = blocks_ + block_count_;
            const BlockMeta* it = std::lower_bound(first, last, target, [](const BlockMeta& m, std::uint32_t v) {
                return m.last_doc < v;
            });
            if (it == last) {
                block_ = block_count_;
                return;
            }
            load_block(static_cast<std::uint32_t>(it - blocks_));
        }
        pos_ = static_cast<std::uint32_t>(std::lower_bound(docs_ + pos_, docs_ + block_n_, target) - docs_);
    }

private:
    void load_block(std::uint32_t block) {
        block_ = block;
        block_n_ = block + 1 == block_count_ ? count_ - block * kBlockSize : kBlockSize;
        const std::uint8_t* p = bytes_ + blocks_[block].byte_offset;
        std::uint32_t doc = block == 0 ? 0 : blocks_[block - 1].last_doc;
        for (std::uint32_t i = 0; i < block_n_; ++i) {
            doc += vbyte_decode(p);
            docs_[i] = doc;
        }
        tf_ptr_ = p;
        tf_ready_ = false;
        pos_ = 0;
    }

    void decode_tfs() {
        const std::uint8_t* p = tf_ptr_;
        for (std::uint32_t i = 0; i < block_n_; ++i) tfs_[i] = vbyte_decode(p);
        tf_ready_ = true;
    }

    const std::uint8_t* bytes_ = nullptr;
    const BlockMeta* blocks_ = nullptr;
    std::uint32_t block_count_ = 0;
    std::uint32_t count_ = 0;
    std::uint32_t block_ = 0;
    std::uint32_t block_n_ = 0;
    std::uint32_t pos_ = 0;
    const std::uint8_t* tf_ptr_ = nullptr;
    bool tf_ready_ = false;
    std::uint32_t docs_[kBlockSize];
    std::uint32_t tfs_[kBlockSize];
};

struct EncodedList {
    std::vector<std::uint8_t> bytes;
    std::vector<BlockMeta> blocks;
    std::uint32_t count = 0;
};

EncodedList encode_postings(std::span<const Posting> list);
std::vector<Posting> decode_postings(const EncodedList& encoded);

}
