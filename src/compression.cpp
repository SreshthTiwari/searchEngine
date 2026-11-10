#include "compression.h"

#include <limits>
#include <stdexcept>

namespace se {

void vbyte_encode(std::uint32_t value, std::vector<std::uint8_t>& out) {
    while (value >= 0x80) {
        out.push_back(static_cast<std::uint8_t>(value | 0x80));
        value >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(value));
}

bool vbyte_decode_checked(const std::uint8_t*& p, const std::uint8_t* end, std::uint32_t& out) noexcept {
    std::uint32_t result = 0;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        if (p == end) return false;
        const std::uint8_t byte = *p++;
        if (shift == 28 && (byte & 0xF0)) return false;
        result |= static_cast<std::uint32_t>(byte & 0x7F) << shift;
        if (!(byte & 0x80)) {
            out = result;
            return true;
        }
    }
    return false;
}

std::vector<std::uint32_t> delta_encode(std::span<const std::uint32_t> sorted) {
    std::vector<std::uint32_t> gaps(sorted.size());
    std::uint32_t previous = 0;
    for (std::size_t i = 0; i < sorted.size(); ++i) {
        if (sorted[i] < previous) throw std::invalid_argument("delta_encode requires non-decreasing input");
        gaps[i] = sorted[i] - previous;
        previous = sorted[i];
    }
    return gaps;
}

std::vector<std::uint32_t> delta_decode(std::span<const std::uint32_t> gaps) {
    std::vector<std::uint32_t> values(gaps.size());
    std::uint32_t current = 0;
    for (std::size_t i = 0; i < gaps.size(); ++i) {
        current += gaps[i];
        values[i] = current;
    }
    return values;
}

void append_encoded_list(std::span<const Posting> list, std::vector<std::uint8_t>& bytes,
                         std::vector<BlockMeta>& blocks) {
    const std::size_t start = bytes.size();
    std::uint32_t previous = 0;
    for (std::size_t base = 0; base < list.size(); base += kBlockSize) {
        const std::size_t n = std::min<std::size_t>(kBlockSize, list.size() - base);
        const std::size_t offset = bytes.size() - start;
        if (offset > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("posting list too large to encode");
        }
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint32_t doc = list[base + i].doc;
            if (base + i > 0 && doc <= previous) {
                throw std::invalid_argument("posting list must be strictly increasing");
            }
            vbyte_encode(doc - previous, bytes);
            previous = doc;
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (list[base + i].tf == 0) throw std::invalid_argument("term frequency must be positive");
            vbyte_encode(list[base + i].tf, bytes);
        }
        blocks.push_back({list[base + n - 1].doc, static_cast<std::uint32_t>(offset)});
    }
}

bool validate_encoded_list(const std::uint8_t* bytes, std::size_t size, std::span<const BlockMeta> blocks,
                           std::uint32_t count, std::uint32_t num_docs) noexcept {
    if (blocks.size() != block_count_for(count)) return false;
    if (blocks.empty()) return size == 0;
    if (blocks[0].byte_offset != 0) return false;

    std::uint32_t previous_doc = 0;
    for (std::size_t b = 0; b < blocks.size(); ++b) {
        const std::size_t begin = blocks[b].byte_offset;
        const std::size_t end = b + 1 < blocks.size() ? blocks[b + 1].byte_offset : size;
        if (begin > end || end > size) return false;

        const std::uint32_t n = b + 1 == blocks.size() ? count - static_cast<std::uint32_t>(b) * kBlockSize : kBlockSize;
        const std::uint8_t* p = bytes + begin;
        const std::uint8_t* limit = bytes + end;
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t gap = 0;
            if (!vbyte_decode_checked(p, limit, gap)) return false;
            const bool first_of_list = b == 0 && i == 0;
            if (!first_of_list && gap == 0) return false;
            if (gap > std::numeric_limits<std::uint32_t>::max() - previous_doc) return false;
            previous_doc += gap;
            if (previous_doc >= num_docs) return false;
        }
        if (previous_doc != blocks[b].last_doc) return false;
        for (std::uint32_t i = 0; i < n; ++i) {
            std::uint32_t tf = 0;
            if (!vbyte_decode_checked(p, limit, tf) || tf == 0) return false;
        }
        if (p != limit) return false;
    }
    return true;
}

EncodedList encode_postings(std::span<const Posting> list) {
    EncodedList encoded;
    encoded.count = static_cast<std::uint32_t>(list.size());
    append_encoded_list(list, encoded.bytes, encoded.blocks);
    return encoded;
}

std::vector<Posting> decode_postings(const EncodedList& encoded) {
    std::vector<Posting> out;
    out.reserve(encoded.count);
    CompressedCursor cursor(encoded.bytes.data(), encoded.blocks.data(),
                            static_cast<std::uint32_t>(encoded.blocks.size()), encoded.count);
    for (; cursor.valid(); cursor.next()) out.push_back({cursor.doc(), cursor.tf()});
    return out;
}

}
