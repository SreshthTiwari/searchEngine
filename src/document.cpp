#include "document.h"

#include <cstdint>
#include <fstream>
#include <string_view>

namespace se {

namespace {

constexpr int kMaxJsonDepth = 64;

[[noreturn]] void fail(std::size_t line, const std::string& message) {
    throw ParseError("line " + std::to_string(line) + ": " + message);
}

void strip_carriage_return(std::string& line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
}

bool is_blank(const std::string& line) {
    for (char c : line) {
        if (c != ' ' && c != '\t') return false;
    }
    return true;
}

std::vector<std::string_view> split_tabs(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = line.find('\t', start);
        if (pos == std::string_view::npos) {
            fields.push_back(line.substr(start));
            break;
        }
        fields.push_back(line.substr(start, pos - start));
        start = pos + 1;
    }
    return fields;
}

Document parse_tsv_line(const std::string& line, std::size_t line_number) {
    const auto fields = split_tabs(line);
    if (fields.size() < 2 || fields.size() > 3) {
        fail(line_number, "expected 2 or 3 tab-separated fields (id, [title,] text) but found " +
                              std::to_string(fields.size()));
    }
    if (fields[0].empty()) fail(line_number, "empty document id");
    Document doc;
    doc.external_id = std::string(fields[0]);
    if (fields.size() == 3) {
        doc.title = std::string(fields[1]);
        doc.text = std::string(fields[2]);
    } else {
        doc.text = std::string(fields[1]);
    }
    return doc;
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class JsonReader {
public:
    explicit JsonReader(std::string_view text) : s_(text) {}

    Document parse_document() {
        Document doc;
        bool have_id = false;
        bool have_text = false;
        skip_ws();
        expect('{');
        skip_ws();
        if (peek() == '}') {
            ++i_;
        } else {
            while (true) {
                skip_ws();
                const std::string key = parse_string();
                skip_ws();
                expect(':');
                skip_ws();
                if (key == "id") {
                    doc.external_id = parse_scalar_as_string("id");
                    have_id = true;
                } else if (key == "title") {
                    doc.title = parse_optional_string("title");
                } else if (key == "text") {
                    doc.text = parse_optional_string("text");
                    have_text = true;
                } else {
                    skip_value(0);
                }
                skip_ws();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect('}');
                break;
            }
        }
        skip_ws();
        if (i_ != s_.size()) throw std::runtime_error("trailing characters after JSON object");
        if (!have_id || doc.external_id.empty()) throw std::runtime_error("missing \"id\" field");
        if (!have_text) throw std::runtime_error("missing \"text\" field");
        return doc;
    }

private:
    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\r' || s_[i_] == '\n')) ++i_;
    }

    void expect(char c) {
        if (peek() != c) throw std::runtime_error(std::string("expected '") + c + "'");
        ++i_;
    }

    std::uint32_t parse_hex4() {
        if (i_ + 4 > s_.size()) throw std::runtime_error("truncated \\u escape");
        std::uint32_t value = 0;
        for (int k = 0; k < 4; ++k) {
            const char c = s_[i_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<std::uint32_t>(c - 'A' + 10);
            else throw std::runtime_error("invalid \\u escape");
        }
        return value;
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (true) {
            if (i_ >= s_.size()) throw std::runtime_error("unterminated string");
            const char c = s_[i_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) throw std::runtime_error("control character in string");
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (i_ >= s_.size()) throw std::runtime_error("unterminated escape");
            const char e = s_[i_++];
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    std::uint32_t cp = parse_hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (i_ + 2 > s_.size() || s_[i_] != '\\' || s_[i_ + 1] != 'u') {
                            throw std::runtime_error("unpaired surrogate");
                        }
                        i_ += 2;
                        const std::uint32_t low = parse_hex4();
                        if (low < 0xDC00 || low > 0xDFFF) throw std::runtime_error("invalid surrogate pair");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        throw std::runtime_error("unpaired surrogate");
                    }
                    append_utf8(out, cp);
                    break;
                }
                default: throw std::runtime_error("invalid escape sequence");
            }
        }
        return out;
    }

    std::string parse_scalar_as_string(const char* field) {
        if (peek() == '"') return parse_string();
        const std::size_t start = i_;
        while (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' ||
                                  s_[i_] == 'E' || (s_[i_] >= '0' && s_[i_] <= '9'))) {
            ++i_;
        }
        if (i_ == start) throw std::runtime_error(std::string("\"") + field + "\" must be a string or number");
        return std::string(s_.substr(start, i_ - start));
    }

    std::string parse_optional_string(const char* field) {
        if (peek() == '"') return parse_string();
        if (s_.substr(i_, 4) == "null") {
            i_ += 4;
            return {};
        }
        throw std::runtime_error(std::string("\"") + field + "\" must be a string");
    }

    void skip_value(int depth) {
        if (depth > kMaxJsonDepth) throw std::runtime_error("JSON nesting too deep");
        const char c = peek();
        if (c == '"') {
            parse_string();
        } else if (c == '{' || c == '[') {
            const char close = c == '{' ? '}' : ']';
            ++i_;
            skip_ws();
            if (peek() == close) {
                ++i_;
                return;
            }
            while (true) {
                skip_ws();
                if (c == '{') {
                    parse_string();
                    skip_ws();
                    expect(':');
                    skip_ws();
                }
                skip_value(depth + 1);
                skip_ws();
                if (peek() == ',') {
                    ++i_;
                    continue;
                }
                expect(close);
                break;
            }
        } else {
            const std::size_t start = i_;
            while (i_ < s_.size() && s_[i_] != ',' && s_[i_] != '}' && s_[i_] != ']' && s_[i_] != ' ' &&
                   s_[i_] != '\t' && s_[i_] != '\r' && s_[i_] != '\n') {
                ++i_;
            }
            if (i_ == start) throw std::runtime_error("unexpected token");
        }
    }

    std::string_view s_;
    std::size_t i_ = 0;
};

Document parse_jsonl_line(const std::string& line, std::size_t line_number) {
    try {
        return JsonReader(line).parse_document();
    } catch (const std::runtime_error& e) {
        fail(line_number, e.what());
    }
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
           value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

}

InputFormat parse_input_format(const std::string& name) {
    if (name == "auto") return InputFormat::Auto;
    if (name == "tsv") return InputFormat::Tsv;
    if (name == "jsonl") return InputFormat::Jsonl;
    throw ParseError("unknown input format '" + name + "' (expected auto, tsv or jsonl)");
}

std::vector<Document> parse_documents(std::istream& in, InputFormat format) {
    std::vector<Document> docs;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        strip_carriage_return(line);
        if (is_blank(line)) continue;
        if (format == InputFormat::Jsonl) {
            docs.push_back(parse_jsonl_line(line, line_number));
        } else {
            docs.push_back(parse_tsv_line(line, line_number));
        }
    }
    if (in.bad()) throw ParseError("I/O error while reading documents");
    return docs;
}

std::vector<Document> load_documents(const std::string& path, InputFormat format) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ParseError("cannot open input file '" + path + "'");
    if (format == InputFormat::Auto) {
        format = (ends_with(path, ".jsonl") || ends_with(path, ".json")) ? InputFormat::Jsonl : InputFormat::Tsv;
    }
    return parse_documents(in, format);
}

std::string make_snippet(const std::string& text, std::size_t max_bytes) {
    std::string out;
    out.reserve(std::min(text.size(), max_bytes));
    bool previous_space = true;
    for (char c : text) {
        if (out.size() >= max_bytes) break;
        const bool space = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (space) {
            if (!previous_space) out.push_back(' ');
            previous_space = true;
        } else {
            out.push_back(c);
            previous_space = false;
        }
    }
    if (out.size() >= max_bytes && out.size() < text.size()) {
        std::size_t lead = out.size();
        while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) --lead;
        if (lead > 0) {
            const auto b = static_cast<unsigned char>(out[lead - 1]);
            const std::size_t need = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
            if (out.size() - (lead - 1) < need) out.resize(lead - 1);
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

}
