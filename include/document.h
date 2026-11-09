#pragma once

#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

namespace se {

struct Document {
    std::string external_id;
    std::string title;
    std::string text;
};

class ParseError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class InputFormat { Auto, Tsv, Jsonl };

InputFormat parse_input_format(const std::string& name);

std::vector<Document> parse_documents(std::istream& in, InputFormat format);

std::vector<Document> load_documents(const std::string& path, InputFormat format = InputFormat::Auto);

std::string make_snippet(const std::string& text, std::size_t max_bytes);

}
