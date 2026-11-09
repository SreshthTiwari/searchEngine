#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace se {

struct TokenizerOptions {
    bool remove_stopwords = false;
    std::size_t max_token_length = 64;

    bool operator==(const TokenizerOptions&) const = default;
};

class Tokenizer {
public:
    explicit Tokenizer(TokenizerOptions options = {});

    void tokenize(std::string_view text, std::vector<std::string>& out) const;
    std::vector<std::string> tokenize(std::string_view text) const;

    const TokenizerOptions& options() const { return options_; }

    static bool is_stopword(std::string_view token);

private:
    TokenizerOptions options_;
};

}
