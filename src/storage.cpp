#include "storage.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace se {

namespace {

constexpr std::array<std::uint8_t, 8> kMagic = {'S', 'R', 'C', 'H', 'I', 'D', 'X', '\0'};
constexpr std::uint32_t kFlagCompressed = 1;

std::uint64_t fnv1a(const std::uint8_t* data, std::size_t size) {
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

class Writer {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u32(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void u64(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) buf_.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    void raw(const std::uint8_t* data, std::size_t n) { buf_.insert(buf_.end(), data, data + n); }
    void str(const std::string& s) {
        if (s.size() > std::numeric_limits<std::uint32_t>::max()) throw IndexError("string too long to serialize");
        u32(static_cast<std::uint32_t>(s.size()));
        raw(reinterpret_cast<const std::uint8_t*>(s.data()), s.size());
    }
    std::vector<std::uint8_t>& buffer() { return buf_; }

private:
    std::vector<std::uint8_t> buf_;
};

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : p_(data), end_(data + size) {}

    std::size_t remaining() const { return static_cast<std::size_t>(end_ - p_); }

    void need(std::size_t n) const {
        if (remaining() < n) throw IndexError("index file is truncated");
    }
    std::uint8_t u8() {
        need(1);
        return *p_++;
    }
    std::uint32_t u32() {
        need(4);
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<std::uint32_t>(p_[i]) << (8 * i);
        p_ += 4;
        return v;
    }
    std::uint64_t u64() {
        need(8);
        std::uint64_t v = 0;
        for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p_[i]) << (8 * i);
        p_ += 8;
        return v;
    }
    const std::uint8_t* raw(std::size_t n) {
        need(n);
        const std::uint8_t* start = p_;
        p_ += n;
        return start;
    }
    std::string str() {
        const std::uint32_t n = u32();
        const std::uint8_t* data = raw(n);
        return std::string(reinterpret_cast<const char*>(data), n);
    }

private:
    const std::uint8_t* p_;
    const std::uint8_t* end_;
};

struct Header {
    std::uint32_t version = 0;
    std::uint32_t flags = 0;
    std::uint64_t num_docs = 0;
    std::uint64_t num_terms = 0;
    std::uint64_t num_postings = 0;
    std::uint64_t total_length = 0;
    std::uint64_t body_size = 0;
    std::uint64_t checksum = 0;
};

Header parse_header(const std::uint8_t* data, std::size_t size) {
    if (size < kIndexHeaderSize) throw IndexError("file is too small to be a search index");
    Reader r(data, size);
    const std::uint8_t* magic = r.raw(kMagic.size());
    if (std::memcmp(magic, kMagic.data(), kMagic.size()) != 0) throw IndexError("not a search index file (bad magic)");
    Header h;
    h.version = r.u32();
    if (h.version != kIndexFormatVersion) {
        throw IndexError("unsupported index format version " + std::to_string(h.version) + " (expected " +
                         std::to_string(kIndexFormatVersion) + ")");
    }
    h.flags = r.u32();
    if (h.flags & ~kFlagCompressed) throw IndexError("index file uses unknown feature flags");
    h.num_docs = r.u64();
    h.num_terms = r.u64();
    h.num_postings = r.u64();
    h.total_length = r.u64();
    h.body_size = r.u64();
    h.checksum = r.u64();
    if (h.body_size != size - kIndexHeaderSize) throw IndexError("index file size does not match its header");
    return h;
}

void write_header(Writer& w, const IndexCore& core, std::uint32_t flags, std::size_t body_size,
                  std::uint64_t checksum) {
    w.raw(kMagic.data(), kMagic.size());
    w.u32(kIndexFormatVersion);
    w.u32(flags);
    w.u64(core.num_docs());
    w.u64(core.num_terms());
    w.u64(core.num_postings());
    w.u64(core.total_length());
    w.u64(body_size);
    w.u64(checksum);
}

void write_core(Writer& body, const IndexCore& core) {
    body.u8(core.tokenizer.remove_stopwords ? 1 : 0);
    body.u32(static_cast<std::uint32_t>(core.tokenizer.max_token_length));
    for (const std::string& term : core.terms) body.str(term);
    for (std::uint32_t df : core.doc_freq) body.u32(df);
    for (const DocInfo& doc : core.docs) {
        body.u32(doc.length);
        body.str(doc.external_id);
        body.str(doc.title);
        body.str(doc.snippet);
    }
}

std::vector<std::uint8_t> assemble(const IndexCore& core, std::uint32_t flags, Writer& body) {
    const std::vector<std::uint8_t>& payload = body.buffer();
    Writer out;
    write_header(out, core, flags, payload.size(), fnv1a(payload.data(), payload.size()));
    out.raw(payload.data(), payload.size());
    return std::move(out.buffer());
}

IndexCore read_core(Reader& r, const Header& h) {
    IndexCore core;
    core.tokenizer.remove_stopwords = r.u8() != 0;
    core.tokenizer.max_token_length = r.u32();

    if (h.num_terms > r.remaining() / 8) throw IndexError("index header claims more terms than the file can hold");
    core.terms.reserve(h.num_terms);
    for (std::uint64_t t = 0; t < h.num_terms; ++t) core.terms.push_back(r.str());

    if (h.num_terms > r.remaining() / 4) throw IndexError("index file is truncated");
    core.doc_freq.resize(h.num_terms);
    for (std::uint64_t t = 0; t < h.num_terms; ++t) core.doc_freq[t] = r.u32();

    if (h.num_docs > r.remaining() / 16) throw IndexError("index header claims more documents than the file can hold");
    core.docs.resize(h.num_docs);
    for (std::uint64_t d = 0; d < h.num_docs; ++d) {
        DocInfo& doc = core.docs[d];
        doc.length = r.u32();
        doc.external_id = r.str();
        doc.title = r.str();
        doc.snippet = r.str();
    }
    return core;
}

void check_core_against_header(const IndexCore& core, const Header& h) {
    if (core.num_postings() != h.num_postings) throw IndexError("posting count does not match header");
    if (core.total_length() != h.total_length) throw IndexError("collection length does not match header");
}

void atomic_write(const std::string& path, const std::vector<std::uint8_t>& data) {
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw IndexError("cannot open '" + temp + "' for writing");
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) {
            std::remove(temp.c_str());
            throw IndexError("failed while writing '" + temp + "'");
        }
    }
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        throw IndexError("cannot move index into place at '" + path + "'");
    }
}

std::vector<std::uint8_t> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw IndexError("cannot open index file '" + path + "'");
    const std::streamsize size = in.tellg();
    if (size < 0) throw IndexError("cannot determine size of '" + path + "'");
    in.seekg(0);
    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) {
        throw IndexError("failed while reading '" + path + "'");
    }
    return data;
}

}

std::vector<std::uint8_t> serialize_index(const InvertedIndex& index) {
    Writer body;
    body.buffer().reserve(index.postings_bytes() + (1 << 20));
    write_core(body, index.core());
    for (const Posting& p : index.all_postings()) {
        body.u32(p.doc);
        body.u32(p.tf);
    }
    return assemble(index.core(), 0, body);
}

std::vector<std::uint8_t> serialize_index(const CompressedIndex& index) {
    Writer body;
    body.buffer().reserve(index.postings_bytes() + (1 << 20));
    write_core(body, index.core());
    const auto& offsets = index.byte_offsets();
    for (std::size_t t = 0; t + 1 < offsets.size(); ++t) body.u64(offsets[t + 1] - offsets[t]);
    for (const BlockMeta& block : index.blocks()) {
        body.u32(block.last_doc);
        body.u32(block.byte_offset);
    }
    body.raw(index.bytes().data(), index.bytes().size());
    return assemble(index.core(), kFlagCompressed, body);
}

AnyIndex deserialize_index(const std::vector<std::uint8_t>& data) {
    const Header h = parse_header(data.data(), data.size());
    const std::uint8_t* body = data.data() + kIndexHeaderSize;
    if (fnv1a(body, h.body_size) != h.checksum) throw IndexError("index checksum mismatch (file is corrupted)");

    try {
        Reader r(body, h.body_size);
        IndexCore core = read_core(r, h);
        core.finalize();
        check_core_against_header(core, h);

        if (!(h.flags & kFlagCompressed)) {
            if (core.num_postings() > r.remaining() / 8) throw IndexError("index file is truncated");
            std::vector<Posting> postings(core.num_postings());
            for (Posting& p : postings) {
                p.doc = r.u32();
                p.tf = r.u32();
            }
            if (r.remaining() != 0) throw IndexError("unexpected trailing bytes in index file");
            return InvertedIndex(std::move(core), std::move(postings));
        }

        if (core.num_terms() > r.remaining() / 8) throw IndexError("index file is truncated");
        std::vector<std::uint64_t> offsets(core.num_terms() + 1, 0);
        for (std::size_t t = 0; t < core.num_terms(); ++t) {
            const std::uint64_t size = r.u64();
            if (size > std::numeric_limits<std::uint64_t>::max() - offsets[t]) throw IndexError("corrupt term sizes");
            offsets[t + 1] = offsets[t] + size;
        }

        std::uint64_t block_total = 0;
        for (std::uint32_t df : core.doc_freq) block_total += block_count_for(df);
        if (block_total > r.remaining() / 8) throw IndexError("index file is truncated");
        std::vector<BlockMeta> blocks(block_total);
        for (BlockMeta& block : blocks) {
            block.last_doc = r.u32();
            block.byte_offset = r.u32();
        }

        if (r.remaining() != offsets.back()) throw IndexError("compressed posting data size mismatch");
        const std::uint8_t* blob = r.raw(r.remaining());
        std::vector<std::uint8_t> bytes(blob, blob + offsets.back());
        return CompressedIndex(std::move(core), std::move(bytes), std::move(offsets), std::move(blocks));
    } catch (const IndexError&) {
        throw;
    } catch (const std::exception& e) {
        throw IndexError(std::string("invalid index contents: ") + e.what());
    }
}

void save_index(const InvertedIndex& index, const std::string& path) { atomic_write(path, serialize_index(index)); }

void save_index(const CompressedIndex& index, const std::string& path) { atomic_write(path, serialize_index(index)); }

AnyIndex load_index(const std::string& path) { return deserialize_index(read_file(path)); }

IndexFileInfo read_index_info(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw IndexError("cannot open index file '" + path + "'");
    const std::streamsize size = in.tellg();
    in.seekg(0);
    std::vector<std::uint8_t> head(std::min<std::size_t>(static_cast<std::size_t>(size), kIndexHeaderSize));
    if (!head.empty()) in.read(reinterpret_cast<char*>(head.data()), static_cast<std::streamsize>(head.size()));
    if (static_cast<std::size_t>(size) < kIndexHeaderSize) throw IndexError("file is too small to be a search index");
    Reader r(head.data(), head.size());
    r.raw(kMagic.size());
    IndexFileInfo info;
    info.version = r.u32();
    const std::uint32_t flags = r.u32();
    info.compressed = (flags & kFlagCompressed) != 0;
    info.num_docs = r.u64();
    info.num_terms = r.u64();
    info.num_postings = r.u64();
    info.total_length = r.u64();
    info.file_bytes = static_cast<std::uint64_t>(size);
    if (std::memcmp(head.data(), kMagic.data(), kMagic.size()) != 0) throw IndexError("not a search index file (bad magic)");
    return info;
}

}
