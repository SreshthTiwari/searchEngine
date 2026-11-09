#include <sstream>
#include <string>

#include "document.h"
#include "test_util.h"

using se::Document;
using se::InputFormat;
using se::ParseError;

namespace {

std::vector<Document> parse(const std::string& text, InputFormat format) {
    std::istringstream in(text);
    return se::parse_documents(in, format);
}

}

TEST(documents_tsv_two_columns) {
    const auto docs = parse("10\tfirst passage\n20\tsecond passage\n", InputFormat::Tsv);
    CHECK_EQ(docs.size(), std::size_t{2});
    CHECK_EQ(docs[0].external_id, std::string("10"));
    CHECK_EQ(docs[0].title, std::string(""));
    CHECK_EQ(docs[0].text, std::string("first passage"));
    CHECK_EQ(docs[1].external_id, std::string("20"));
}

TEST(documents_tsv_three_columns_with_titles) {
    const auto docs = parse("a\tTitle A\tbody a\nb\tTitle B\tbody b\n", InputFormat::Tsv);
    CHECK_EQ(docs.size(), std::size_t{2});
    CHECK_EQ(docs[0].title, std::string("Title A"));
    CHECK_EQ(docs[1].text, std::string("body b"));
}

TEST(documents_tsv_skips_blank_lines_and_handles_crlf) {
    const auto docs = parse("1\tone\r\n\r\n   \n2\ttwo\r\n", InputFormat::Tsv);
    CHECK_EQ(docs.size(), std::size_t{2});
    CHECK_EQ(docs[0].text, std::string("one"));
    CHECK_EQ(docs[1].text, std::string("two"));
}

TEST(documents_tsv_allows_empty_text) {
    const auto docs = parse("1\t\n", InputFormat::Tsv);
    CHECK_EQ(docs.size(), std::size_t{1});
    CHECK(docs[0].text.empty());
}

TEST(documents_tsv_rejects_malformed_lines) {
    CHECK_THROWS(parse("only-one-field\n", InputFormat::Tsv), ParseError);
    CHECK_THROWS(parse("1\ta\tb\tc\n", InputFormat::Tsv), ParseError);
    CHECK_THROWS(parse("\ttext without id\n", InputFormat::Tsv), ParseError);
}

TEST(documents_tsv_error_reports_line_number) {
    try {
        parse("1\tok\nbroken\n", InputFormat::Tsv);
        CHECK(false);
    } catch (const ParseError& e) {
        CHECK(std::string(e.what()).find("line 2") != std::string::npos);
    }
}

TEST(documents_jsonl_basic) {
    const auto docs = parse(
        "{\"id\": 7, \"title\": \"T\", \"text\": \"hello world\"}\n"
        "{\"id\": \"x9\", \"text\": \"second\", \"extra\": {\"nested\": [1, 2, {\"a\": null}]}}\n",
        InputFormat::Jsonl);
    CHECK_EQ(docs.size(), std::size_t{2});
    CHECK_EQ(docs[0].external_id, std::string("7"));
    CHECK_EQ(docs[0].title, std::string("T"));
    CHECK_EQ(docs[0].text, std::string("hello world"));
    CHECK_EQ(docs[1].external_id, std::string("x9"));
    CHECK_EQ(docs[1].text, std::string("second"));
}

TEST(documents_jsonl_decodes_escapes_and_unicode) {
    const auto docs = parse("{\"id\":\"1\",\"text\":\"a\\n\\t\\\"q\\\" \\u00e9 \\ud83d\\ude00\"}\n", InputFormat::Jsonl);
    CHECK_EQ(docs.size(), std::size_t{1});
    CHECK_EQ(docs[0].text, std::string("a\n\t\"q\" \xC3\xA9 \xF0\x9F\x98\x80"));
}

TEST(documents_jsonl_rejects_malformed_input) {
    CHECK_THROWS(parse("{\"id\": \"1\"}\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("{\"text\": \"no id\"}\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("not json\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("{\"id\": \"1\", \"text\": \"unterminated}\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("{\"id\": \"1\", \"text\": 5}\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("{\"id\": \"1\", \"text\": \"x\"} trailing\n", InputFormat::Jsonl), ParseError);
    CHECK_THROWS(parse("{\"id\": \"1\", \"text\": \"\\ud83d\"}\n", InputFormat::Jsonl), ParseError);
}

TEST(documents_load_missing_file_fails_gracefully) {
    CHECK_THROWS(se::load_documents("/nonexistent/path/to/docs.tsv"), ParseError);
}

TEST(documents_input_format_names) {
    CHECK(se::parse_input_format("tsv") == InputFormat::Tsv);
    CHECK(se::parse_input_format("jsonl") == InputFormat::Jsonl);
    CHECK(se::parse_input_format("auto") == InputFormat::Auto);
    CHECK_THROWS(se::parse_input_format("xml"), ParseError);
}

TEST(documents_snippet_collapses_whitespace_and_truncates) {
    CHECK_EQ(se::make_snippet("a  b\n\tc", 100), std::string("a b c"));
    CHECK_EQ(se::make_snippet("abcdefghij", 5), std::string("abcde"));
    CHECK_EQ(se::make_snippet("", 5), std::string(""));
}

TEST(documents_snippet_does_not_split_utf8_sequences) {
    const std::string text = "ab\xC3\xA9\xC3\xA9";
    CHECK_EQ(se::make_snippet(text, 3), std::string("ab"));
    CHECK_EQ(se::make_snippet(text, 4), std::string("ab\xC3\xA9"));
    CHECK_EQ(se::make_snippet(text, 5), std::string("ab\xC3\xA9"));
}
