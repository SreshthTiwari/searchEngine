#pragma once

#include <cstddef>
#include <vector>

#include "document.h"
#include "inverted_index.h"
#include "tokenizer.h"

namespace se {

struct BuildOptions {
    std::size_t threads = 1;
    TokenizerOptions tokenizer;
    std::size_t snippet_bytes = 160;
};

std::size_t resolve_thread_count(std::size_t requested);

InvertedIndex build_index(const std::vector<Document>& docs, const BuildOptions& options = {});

}
