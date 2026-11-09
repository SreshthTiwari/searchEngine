#include "index_builder.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace se {

namespace {

using PartialPostings = std::unordered_map<std::string, std::vector<Posting>>;

void run_workers(std::size_t workers, const std::function<void(std::size_t)>& body) {
    if (workers <= 1) {
        body(0);
        return;
    }
    std::vector<std::thread> threads;
    threads.reserve(workers);
    std::exception_ptr error;
    std::mutex error_mutex;
    for (std::size_t w = 0; w < workers; ++w) {
        threads.emplace_back([&, w] {
            try {
                body(w);
            } catch (...) {
                std::lock_guard<std::mutex> lock(error_mutex);
                if (!error) error = std::current_exception();
            }
        });
    }
    for (std::thread& t : threads) t.join();
    if (error) std::rethrow_exception(error);
}

std::pair<std::size_t, std::size_t> slice(std::size_t total, std::size_t workers, std::size_t w) {
    const std::size_t base = total / workers;
    const std::size_t extra = total % workers;
    const std::size_t begin = w * base + std::min(w, extra);
    const std::size_t end = begin + base + (w < extra ? 1 : 0);
    return {begin, end};
}

}

std::size_t resolve_thread_count(std::size_t requested) {
    if (requested > 0) return requested;
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : hw;
}

InvertedIndex build_index(const std::vector<Document>& docs, const BuildOptions& options) {
    const std::size_t doc_count = docs.size();
    const std::size_t workers = std::max<std::size_t>(1, std::min(resolve_thread_count(options.threads),
                                                                  std::max<std::size_t>(doc_count, 1)));
    const Tokenizer tokenizer(options.tokenizer);

    IndexCore core;
    core.tokenizer = options.tokenizer;
    core.docs.resize(doc_count);

    std::vector<PartialPostings> partials(workers);

    run_workers(workers, [&](std::size_t w) {
        const auto [begin, end] = slice(doc_count, workers, w);
        PartialPostings& partial = partials[w];
        std::vector<std::string> tokens;
        for (std::size_t i = begin; i < end; ++i) {
            const Document& doc = docs[i];
            tokens.clear();
            tokenizer.tokenize(doc.text, tokens);

            DocInfo& info = core.docs[i];
            info.external_id = doc.external_id;
            info.title = doc.title;
            info.snippet = make_snippet(doc.text, options.snippet_bytes);
            info.length = static_cast<std::uint32_t>(tokens.size());

            std::sort(tokens.begin(), tokens.end());
            for (std::size_t j = 0; j < tokens.size();) {
                std::size_t k = j + 1;
                while (k < tokens.size() && tokens[k] == tokens[j]) ++k;
                partial[tokens[j]].push_back({static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(k - j)});
                j = k;
            }
        }
    });

    for (const PartialPostings& partial : partials) {
        for (const auto& entry : partial) core.terms.push_back(entry.first);
    }
    std::sort(core.terms.begin(), core.terms.end());
    core.terms.erase(std::unique(core.terms.begin(), core.terms.end()), core.terms.end());

    const std::size_t term_count = core.terms.size();
    core.doc_freq.assign(term_count, 0);

    run_workers(workers, [&](std::size_t w) {
        const auto [begin, end] = slice(term_count, workers, w);
        for (std::size_t t = begin; t < end; ++t) {
            std::uint32_t df = 0;
            for (const PartialPostings& partial : partials) {
                const auto it = partial.find(core.terms[t]);
                if (it != partial.end()) df += static_cast<std::uint32_t>(it->second.size());
            }
            core.doc_freq[t] = df;
        }
    });

    std::vector<std::uint64_t> offsets(term_count + 1, 0);
    for (std::size_t t = 0; t < term_count; ++t) offsets[t + 1] = offsets[t] + core.doc_freq[t];

    std::vector<Posting> postings(offsets[term_count]);

    run_workers(workers, [&](std::size_t w) {
        const auto [begin, end] = slice(term_count, workers, w);
        for (std::size_t t = begin; t < end; ++t) {
            Posting* out = postings.data() + offsets[t];
            for (const PartialPostings& partial : partials) {
                const auto it = partial.find(core.terms[t]);
                if (it == partial.end()) continue;
                out = std::copy(it->second.begin(), it->second.end(), out);
            }
        }
    });

    return InvertedIndex(std::move(core), std::move(postings));
}

}
