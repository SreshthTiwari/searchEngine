#include <string>
#include <vector>

#include "test_util.h"
#include "tokenizer.h"

using se::Tokenizer;
using se::TokenizerOptions;
using Tokens = std::vector<std::string>;

TEST(tokenizer_lowercases_and_splits_on_whitespace) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("Hello World  FOO") == (Tokens{"hello", "world", "foo"}));
}

TEST(tokenizer_strips_punctuation) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("Hello, world! (fast-search); done...") ==
          (Tokens{"hello", "world", "fast", "search", "done"}));
}

TEST(tokenizer_handles_empty_and_whitespace_only_input) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("").empty());
    CHECK(tokenizer.tokenize("   \t\n  ").empty());
    CHECK(tokenizer.tokenize("!!! ... ???").empty());
}

TEST(tokenizer_keeps_digits_and_mixed_alphanumerics) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("C++20 is 3.14 times RTX4090") ==
          (Tokens{"c", "20", "is", "3", "14", "times", "rtx4090"}));
}

TEST(tokenizer_joins_internal_apostrophes_only) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("don't 'quoted' dogs' rock'n'roll") ==
          (Tokens{"dont", "quoted", "dogs", "rocknroll"}));
}

TEST(tokenizer_treats_non_ascii_bytes_as_separators) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("caf\xC3\xA9 na\xC3\xAFve") == (Tokens{"caf", "na", "ve"}));
}

TEST(tokenizer_drops_overlong_tokens) {
    TokenizerOptions options;
    options.max_token_length = 5;
    Tokenizer tokenizer(options);
    CHECK(tokenizer.tokenize("short toolongtoken ok") == (Tokens{"short", "ok"}));
}

TEST(tokenizer_stopwords_are_optional) {
    Tokenizer keep;
    CHECK(keep.tokenize("the cat and the hat") == (Tokens{"the", "cat", "and", "the", "hat"}));

    TokenizerOptions options;
    options.remove_stopwords = true;
    Tokenizer remove(options);
    CHECK(remove.tokenize("The cat and THE hat") == (Tokens{"cat", "hat"}));
    CHECK(remove.tokenize("the and of").empty());
}

TEST(tokenizer_is_deterministic_between_documents_and_queries) {
    Tokenizer tokenizer;
    CHECK(tokenizer.tokenize("Machine-Learning Algorithms") ==
          tokenizer.tokenize("machine learning algorithms"));
}

TEST(tokenizer_appends_to_existing_output) {
    Tokenizer tokenizer;
    Tokens out{"existing"};
    tokenizer.tokenize("new tokens", out);
    CHECK(out == (Tokens{"existing", "new", "tokens"}));
}
