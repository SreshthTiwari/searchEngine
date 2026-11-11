#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "compressed_index.h"
#include "inverted_index.h"

namespace se {

class IndexError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

constexpr std::uint32_t kIndexFormatVersion = 1;
constexpr std::size_t kIndexHeaderSize = 64;

using AnyIndex = std::variant<InvertedIndex, CompressedIndex>;

struct IndexFileInfo {
    std::uint32_t version = 0;
    bool compressed = false;
    std::uint64_t num_docs = 0;
    std::uint64_t num_terms = 0;
    std::uint64_t num_postings = 0;
    std::uint64_t total_length = 0;
    std::uint64_t file_bytes = 0;
};

std::vector<std::uint8_t> serialize_index(const InvertedIndex& index);
std::vector<std::uint8_t> serialize_index(const CompressedIndex& index);

AnyIndex deserialize_index(const std::vector<std::uint8_t>& data);

void save_index(const InvertedIndex& index, const std::string& path);
void save_index(const CompressedIndex& index, const std::string& path);

AnyIndex load_index(const std::string& path);

IndexFileInfo read_index_info(const std::string& path);

}
