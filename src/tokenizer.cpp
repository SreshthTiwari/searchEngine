#include "tokenizer.h"

#include <algorithm>
#include <array>
#include <utility>

namespace se {

namespace {

constexpr std::array<std::string_view, 33> kStopwords = {
    "a",    "an",   "and",  "are",   "as",   "at",   "be",   "but",  "by",
    "for",  "if",   "in",   "into",  "is",   "it",   "no",   "not",  "of",
    "on",   "or",   "such", "that",  "the",  "their", "then", "there", "these",
    "they", "this", "to",   "was",   "will", "with"};

static_assert(std::is_sorted(kStopwords.begin(), kStopwords.end()));

constexpr bool is_ascii_digit(unsigned char c) { return c >= '0' && c <= '9'; }
constexpr bool is_ascii_upper(unsigned char c) { return c >= 'A' && c <= 'Z'; }
constexpr bool is_ascii_lower(unsigned char c) { return c >= 'a' && c <= 'z'; }
constexpr bool is_ascii_alnum(unsigned char c) {
    return is_ascii_digit(c) || is_ascii_upper(c) || is_ascii_lower(c);
}

}

Tokenizer::Tokenizer(TokenizerOptions options) : options_(options) {}

bool Tokenizer::is_stopword(std::string_view token) {
    return std::binary_search(kStopwords.begin(), kStopwords.end(), token);
}

void Tokenizer::tokenize(std::string_view text, std::vector<std::string>& out) const {
    std::string current;

    auto flush = [&] {
        if (current.empty()) return;
        bool keep = current.size() <= options_.max_token_length;
        if (keep && options_.remove_stopwords && is_stopword(current)) keep = false;
        if (keep) out.push_back(std::move(current));
        current.clear();
    };

    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (is_ascii_upper(c)) {
            current.push_back(static_cast<char>(c + ('a' - 'A')));
        } else if (is_ascii_lower(c) || is_ascii_digit(c)) {
            current.push_back(static_cast<char>(c));
        } else if (c == '\'' && !current.empty() && i + 1 < text.size() &&
                   is_ascii_alnum(static_cast<unsigned char>(text[i + 1]))) {
            continue;
        } else {
            flush();
        }
    }
    flush();
}

std::vector<std::string> Tokenizer::tokenize(std::string_view text) const {
    std::vector<std::string> tokens;
    tokenize(text, tokens);
    return tokens;
}

}
