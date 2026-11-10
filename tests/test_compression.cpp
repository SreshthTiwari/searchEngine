#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "bm25.h"
#include "compressed_index.h"
#include "compression.h"
#include "index_builder.h"
#include "test_corpus.h"
#include "test_util.h"

using se::Posting;

namespace {

std::vector<Posting> random_list(std::mt19937& rng, std::size_t n, std::uint32_t max_gap, std::uint32_t max_tf) {
    std::uniform_int_distribution<std::uint32_t> gap(1, max_gap);
    std::uniform_int_distribution<std::uint32_t> tf(1, max_tf);
    std::vector<Posting> list;
    std::uint32_t doc = gap(rng) - 1;
    for (std::size_t i = 0; i < n; ++i) {
        list.push_back({doc, tf(rng)});
        doc += gap(rng);
    }
    return list;
}

}

TEST(compression_vbyte_roundtrip_at_boundaries) {
    const std::vector<std::uint32_t> values = {0,        1,        127,       128,        129,        16383,
                                               16384,    2097151,  2097152,   268435455,  268435456,   0xFFFFFFFEu,
                                               std::numeric_limits<std::uint32_t>::max()};
    for (std::uint32_t v : values) {
        std::vector<std::uint8_t> buf;
        se::vbyte_encode(v, buf);
        const std::uint8_t* p = buf.data();
        CHECK_EQ(se::vbyte_decode(p), v);
        CHECK(p == buf.data() + buf.size());
        const std::uint8_t* q = buf.data();
        std::uint32_t out = 0;
        CHECK(se::vbyte_decode_checked(q, buf.data() + buf.size(), out));
        CHECK_EQ(out, v);
    }
}

TEST(compression_vbyte_uses_expected_byte_counts) {
    auto size_of = [](std::uint32_t v) {
        std::vector<std::uint8_t> buf;
        se::vbyte_encode(v, buf);
        return buf.size();
    };
    CHECK_EQ(size_of(0), std::size_t{1});
    CHECK_EQ(size_of(127), std::size_t{1});
    CHECK_EQ(size_of(128), std::size_t{2});
    CHECK_EQ(size_of(16383), std::size_t{2});
    CHECK_EQ(size_of(16384), std::size_t{3});
    CHECK_EQ(size_of(std::numeric_limits<std::uint32_t>::max()), std::size_t{5});
}

TEST(compression_vbyte_checked_decode_rejects_bad_input) {
    std::uint32_t out = 0;
    {
        const std::uint8_t data[] = {0x80};
        const std::uint8_t* p = data;
        CHECK(!se::vbyte_decode_checked(p, data + 1, out));
    }
    {
        const std::uint8_t data[] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x01};
        const std::uint8_t* p = data;
        CHECK(!se::vbyte_decode_checked(p, data + 6, out));
    }
    {
        const std::uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x7F};
        const std::uint8_t* p = data;
        CHECK(!se::vbyte_decode_checked(p, data + 5, out));
    }
    {
        const std::uint8_t* p = nullptr;
        CHECK(!se::vbyte_decode_checked(p, nullptr, out));
    }
}

TEST(compression_delta_roundtrip) {
    const std::vector<std::uint32_t> values = {0, 0, 3, 4, 10, 1000, 1000, 4000000};
    const auto gaps = se::delta_encode(values);
    CHECK((gaps == std::vector<std::uint32_t>{0, 0, 3, 1, 6, 990, 0, 3999000}));
    CHECK(se::delta_decode(gaps) == values);
    CHECK(se::delta_encode({}).empty());
    CHECK_THROWS(se::delta_encode(std::vector<std::uint32_t>{5, 4}), std::invalid_argument);
}

TEST(compression_posting_list_roundtrip_across_block_boundaries) {
    std::mt19937 rng(1);
    for (std::size_t n : {0, 1, 2, 127, 128, 129, 255, 256, 257, 1000, 5000}) {
        for (std::uint32_t max_gap : {1u, 5u, 300u, 100000u}) {
            const auto list = random_list(rng, n, max_gap, 70000);
            const auto encoded = se::encode_postings(list);
            CHECK_EQ(encoded.count, static_cast<std::uint32_t>(n));
            CHECK_EQ(encoded.blocks.size(), se::block_count_for(n));
            CHECK(se::decode_postings(encoded) == list);
            const std::uint32_t num_docs = n == 0 ? 1 : list.back().doc + 1;
            CHECK(se::validate_encoded_list(encoded.bytes.data(), encoded.bytes.size(), encoded.blocks,
                                            encoded.count, num_docs));
        }
    }
}

TEST(compression_handles_extreme_values_exactly) {
    const std::vector<Posting> list = {{0, 1}, {1, std::numeric_limits<std::uint32_t>::max()},
                                       {2, 128}, {4000000000u, 16384}, {4000000001u, 1}};
    CHECK(se::decode_postings(se::encode_postings(list)) == list);
}

TEST(compression_encoder_rejects_unsorted_or_invalid_postings) {
    CHECK_THROWS(se::encode_postings(std::vector<Posting>{{5, 1}, {3, 1}}), std::invalid_argument);
    CHECK_THROWS(se::encode_postings(std::vector<Posting>{{5, 1}, {5, 1}}), std::invalid_argument);
    CHECK_THROWS(se::encode_postings(std::vector<Posting>{{5, 0}}), std::invalid_argument);
}

TEST(compression_cursor_matches_vector_cursor_under_random_skips) {
    std::mt19937 rng(42);
    for (int round = 0; round < 60; ++round) {
        const auto list = random_list(rng, 1 + rng() % 2000, 1 + rng() % 40, 9);
        const auto encoded = se::encode_postings(list);
        se::VectorCursor expected{std::span<const Posting>(list)};
        se::CompressedCursor actual(encoded.bytes.data(), encoded.blocks.data(),
                                    static_cast<std::uint32_t>(encoded.blocks.size()), encoded.count);
        std::uint32_t target = 0;
        while (true) {
            CHECK_EQ(actual.valid(), expected.valid());
            CHECK_EQ(actual.doc(), expected.doc());
            if (!expected.valid()) break;
            CHECK_EQ(actual.tf(), expected.tf());
            const auto action = rng() % 3;
            if (action == 0) {
                expected.next();
                actual.next();
            } else {
                target = expected.doc() + static_cast<std::uint32_t>(rng() % (action == 1 ? 5 : 600));
                expected.next_geq(target);
                actual.next_geq(target);
            }
        }
    }
}

TEST(compression_cursor_next_geq_never_moves_backwards) {
    std::vector<Posting> list;
    for (std::uint32_t d = 10; d < 2000; d += 7) list.push_back({d, 2});
    const auto encoded = se::encode_postings(list);
    se::CompressedCursor cursor(encoded.bytes.data(), encoded.blocks.data(),
                                static_cast<std::uint32_t>(encoded.blocks.size()), encoded.count);
    cursor.next_geq(500);
    const std::uint32_t at = cursor.doc();
    cursor.next_geq(100);
    CHECK_EQ(cursor.doc(), at);
    cursor.next_geq(0);
    CHECK_EQ(cursor.doc(), at);
    cursor.next_geq(std::numeric_limits<std::uint32_t>::max());
    CHECK(!cursor.valid());
}

TEST(compression_validation_detects_corruption) {
    std::mt19937 rng(7);
    const auto list = random_list(rng, 300, 50, 20);
    auto encoded = se::encode_postings(list);
    const std::uint32_t num_docs = list.back().doc + 1;
    CHECK(se::validate_encoded_list(encoded.bytes.data(), encoded.bytes.size(), encoded.blocks, encoded.count, num_docs));

    CHECK(!se::validate_encoded_list(encoded.bytes.data(), encoded.bytes.size(), encoded.blocks, encoded.count,
                                     list.back().doc));
    CHECK(!se::validate_encoded_list(encoded.bytes.data(), encoded.bytes.size() - 1, encoded.blocks, encoded.count,
                                     num_docs));
    CHECK(!se::validate_encoded_list(encoded.bytes.data(), encoded.bytes.size(), encoded.blocks, encoded.count + 1,
                                     num_docs));

    auto tampered = encoded;
    tampered.blocks[1].last_doc += 1;
    CHECK(!se::validate_encoded_list(tampered.bytes.data(), tampered.bytes.size(), tampered.blocks, tampered.count,
                                     num_docs + 10));
}

TEST(compression_index_roundtrip_preserves_postings_exactly) {
    const auto docs = testing::random_documents(31, 1500, 300, 50);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);
    CHECK(compressed.decompress() == plain);
    for (std::uint32_t t = 0; t < plain.core().num_terms(); t += 17) {
        const auto span = plain.postings(t);
        CHECK(compressed.decode(t) == std::vector<Posting>(span.begin(), span.end()));
    }
}

TEST(compression_reduces_postings_size) {
    const auto docs = testing::random_documents(32, 3000, 500, 60);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);
    CHECK(compressed.postings_bytes() < plain.postings_bytes());
}

TEST(compression_exhaustive_results_match_uncompressed) {
    const auto docs = testing::random_documents(33, 1200, 150, 40);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);
    const se::Bm25Model model(plain.core(), {});
    se::ExhaustiveScratch s1;
    se::ExhaustiveScratch s2;
    std::mt19937 rng(9);
    se::Tokenizer tokenizer;
    for (int q = 0; q < 60; ++q) {
        std::vector<std::uint32_t> terms;
        for (const auto& token : tokenizer.tokenize(testing::random_query(rng, 150, 5))) {
            if (const auto id = plain.core().find(token)) terms.push_back(*id);
        }
        std::sort(terms.begin(), terms.end());
        terms.erase(std::unique(terms.begin(), terms.end()), terms.end());
        CHECK(se::exhaustive_search(plain, model, terms, 10, s1) == se::exhaustive_search(compressed, model, terms, 10, s2));
    }
}

TEST(compression_index_constructor_rejects_inconsistent_data) {
    const auto docs = testing::random_documents(34, 200, 40, 20);
    const auto plain = se::build_index(docs, se::BuildOptions{});
    const auto compressed = se::CompressedIndex::compress(plain);

    auto bytes = compressed.bytes();
    bytes.pop_back();
    CHECK_THROWS(se::CompressedIndex(plain.core(), bytes, compressed.byte_offsets(), compressed.blocks()),
                 std::invalid_argument);

    auto offsets = compressed.byte_offsets();
    offsets.pop_back();
    CHECK_THROWS(se::CompressedIndex(plain.core(), compressed.bytes(), offsets, compressed.blocks()),
                 std::invalid_argument);

    auto flipped = compressed.bytes();
    flipped[0] = 0xFF;
    flipped[1] = 0xFF;
    flipped[2] = 0xFF;
    flipped[3] = 0xFF;
    flipped[4] = 0xFF;
    CHECK_THROWS(se::CompressedIndex(plain.core(), flipped, compressed.byte_offsets(), compressed.blocks()),
                 std::invalid_argument);
}
