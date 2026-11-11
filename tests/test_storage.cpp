#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "bm25.h"
#include "index_builder.h"
#include "storage.h"
#include "test_corpus.h"
#include "test_util.h"
#include "wand.h"

namespace {

std::string temp_path(const std::string& name) {
    return (std::filesystem::temp_directory_path() / ("se_test_" + name + ".bin")).string();
}

struct TempFile {
    explicit TempFile(const std::string& name) : path(temp_path(name)) { std::remove(path.c_str()); }
    ~TempFile() { std::remove(path.c_str()); }
    std::string path;
};

void write_bytes(const std::string& path, const std::vector<std::uint8_t>& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

std::vector<std::uint32_t> resolve(const se::IndexCore& core, const std::string& query) {
    se::Tokenizer tokenizer(core.tokenizer);
    std::vector<std::uint32_t> ids;
    for (const auto& token : tokenizer.tokenize(query)) {
        if (const auto id = core.find(token)) ids.push_back(*id);
    }
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

se::InvertedIndex sample_index() {
    return se::build_index(testing::random_documents(404, 600, 120, 40), se::BuildOptions{});
}

template <class Index>
std::vector<std::vector<se::SearchHit>> run_queries(const Index& index, std::uint32_t seed) {
    const se::Bm25Model model(index.core(), {});
    const auto bounds = se::compute_upper_bounds(index, model);
    std::mt19937 rng(seed);
    std::vector<std::vector<se::SearchHit>> results;
    for (int q = 0; q < 40; ++q) {
        const auto terms = resolve(index.core(), testing::random_query(rng, 120, 5));
        results.push_back(se::wand_search(index, model, bounds, terms, 10));
    }
    return results;
}

}

TEST(storage_uncompressed_roundtrip_preserves_index_and_results) {
    const auto original = sample_index();
    TempFile file("plain");
    se::save_index(original, file.path);
    const auto loaded = se::load_index(file.path);
    const auto* plain = std::get_if<se::InvertedIndex>(&loaded);
    CHECK(plain != nullptr);
    if (!plain) return;
    CHECK(*plain == original);
    CHECK(run_queries(*plain, 1) == run_queries(original, 1));
}

TEST(storage_compressed_roundtrip_preserves_index_and_results) {
    const auto original = sample_index();
    const auto compressed = se::CompressedIndex::compress(original);
    TempFile file("compressed");
    se::save_index(compressed, file.path);
    const auto loaded = se::load_index(file.path);
    const auto* reloaded = std::get_if<se::CompressedIndex>(&loaded);
    CHECK(reloaded != nullptr);
    if (!reloaded) return;
    CHECK(reloaded->decompress() == original);
    CHECK(run_queries(*reloaded, 2) == run_queries(original, 2));
}

TEST(storage_preserves_metadata_and_tokenizer_options) {
    se::BuildOptions options;
    options.tokenizer.remove_stopwords = true;
    std::vector<se::Document> docs = {{"MS1", "A Title", "The quick brown fox jumps over the lazy dog"},
                                      {"MS2", "", "Another passage about dogs"}};
    const auto original = se::build_index(docs, options);
    const auto bytes = se::serialize_index(original);
    const auto loaded = se::deserialize_index(bytes);
    const auto& index = std::get<se::InvertedIndex>(loaded);
    CHECK(index.core().tokenizer.remove_stopwords);
    CHECK_EQ(index.core().docs[0].external_id, std::string("MS1"));
    CHECK_EQ(index.core().docs[0].title, std::string("A Title"));
    CHECK_EQ(index.core().docs[0].snippet, std::string("The quick brown fox jumps over the lazy dog"));
    CHECK_EQ(index.core().docs[1].length, std::uint32_t{4});
}

TEST(storage_empty_index_roundtrip) {
    const auto empty = se::build_index({}, se::BuildOptions{});
    CHECK(std::get<se::InvertedIndex>(se::deserialize_index(se::serialize_index(empty))) == empty);
    const auto compressed = se::CompressedIndex::compress(empty);
    CHECK_EQ(std::get<se::CompressedIndex>(se::deserialize_index(se::serialize_index(compressed))).core().num_docs(),
             std::uint32_t{0});
}

TEST(storage_read_info_reports_header_fields) {
    const auto original = sample_index();
    TempFile file("info");
    se::save_index(se::CompressedIndex::compress(original), file.path);
    const auto info = se::read_index_info(file.path);
    CHECK(info.compressed);
    CHECK_EQ(info.num_docs, std::uint64_t{600});
    CHECK_EQ(info.num_terms, std::uint64_t{original.core().num_terms()});
    CHECK_EQ(info.num_postings, original.num_postings());
    CHECK_EQ(info.version, se::kIndexFormatVersion);
    CHECK(info.file_bytes > se::kIndexHeaderSize);
}

TEST(storage_rejects_missing_file) {
    CHECK_THROWS(se::load_index("/nonexistent/dir/index.bin"), se::IndexError);
    CHECK_THROWS(se::read_index_info("/nonexistent/dir/index.bin"), se::IndexError);
}

TEST(storage_rejects_empty_and_tiny_files) {
    CHECK_THROWS(se::deserialize_index({}), se::IndexError);
    CHECK_THROWS(se::deserialize_index(std::vector<std::uint8_t>(10, 0)), se::IndexError);
    CHECK_THROWS(se::deserialize_index(std::vector<std::uint8_t>(64, 0)), se::IndexError);
}

TEST(storage_rejects_wrong_magic_and_version) {
    auto bytes = se::serialize_index(sample_index());
    auto bad_magic = bytes;
    bad_magic[0] = 'X';
    CHECK_THROWS(se::deserialize_index(bad_magic), se::IndexError);

    auto bad_version = bytes;
    bad_version[8] = 99;
    CHECK_THROWS(se::deserialize_index(bad_version), se::IndexError);

    auto bad_flags = bytes;
    bad_flags[12] = 0x80;
    CHECK_THROWS(se::deserialize_index(bad_flags), se::IndexError);
}

TEST(storage_rejects_truncated_files_at_every_scale) {
    const auto bytes = se::serialize_index(se::CompressedIndex::compress(sample_index()));
    for (std::size_t keep : {std::size_t{1}, std::size_t{63}, std::size_t{64}, std::size_t{65}, bytes.size() / 2, bytes.size() - 1}) {
        std::vector<std::uint8_t> cut(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(keep));
        CHECK_THROWS(se::deserialize_index(cut), se::IndexError);
    }
}

TEST(storage_rejects_trailing_garbage) {
    auto bytes = se::serialize_index(sample_index());
    bytes.push_back(0);
    CHECK_THROWS(se::deserialize_index(bytes), se::IndexError);
}

TEST(storage_detects_body_corruption_via_checksum) {
    for (bool compressed : {false, true}) {
        const auto base = sample_index();
        auto bytes = compressed ? se::serialize_index(se::CompressedIndex::compress(base)) : se::serialize_index(base);
        std::mt19937 rng(5);
        for (int i = 0; i < 200; ++i) {
            auto damaged = bytes;
            const std::size_t pos = se::kIndexHeaderSize + rng() % (bytes.size() - se::kIndexHeaderSize);
            damaged[pos] ^= static_cast<std::uint8_t>(1u << (rng() % 8));
            CHECK_THROWS(se::deserialize_index(damaged), se::IndexError);
        }
    }
}

TEST(storage_detects_header_corruption) {
    const auto bytes = se::serialize_index(sample_index());
    for (std::size_t pos = 0; pos < se::kIndexHeaderSize; ++pos) {
        auto damaged = bytes;
        damaged[pos] ^= 0x01;
        CHECK_THROWS(se::deserialize_index(damaged), se::IndexError);
    }
}

TEST(storage_rejects_structurally_invalid_content_with_valid_checksum) {
    const auto base = sample_index();
    const auto compressed = se::CompressedIndex::compress(base);
    auto bytes = se::serialize_index(compressed);

    const std::size_t postings_blob_end = bytes.size();
    const std::size_t victim = postings_blob_end - 1;
    bytes[victim] = 0x80;
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = se::kIndexHeaderSize; i < bytes.size(); ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    for (int i = 0; i < 8; ++i) bytes[56 + i] = static_cast<std::uint8_t>(hash >> (8 * i));
    CHECK_THROWS(se::deserialize_index(bytes), se::IndexError);
}

TEST(storage_rejects_garbage_file_on_disk) {
    TempFile file("garbage");
    write_bytes(file.path, std::vector<std::uint8_t>(500, 0xAB));
    CHECK_THROWS(se::load_index(file.path), se::IndexError);
}

TEST(storage_save_replaces_existing_file_atomically) {
    TempFile file("replace");
    const auto first = se::build_index(testing::make_documents({"one two"}), se::BuildOptions{});
    const auto second = se::build_index(testing::make_documents({"three four", "five"}), se::BuildOptions{});
    se::save_index(first, file.path);
    se::save_index(second, file.path);
    CHECK(std::get<se::InvertedIndex>(se::load_index(file.path)) == second);
    CHECK(!std::filesystem::exists(file.path + ".tmp"));
}
