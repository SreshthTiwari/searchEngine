#pragma once

#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "document.h"

namespace testing {

inline std::vector<se::Document> random_documents(std::uint32_t seed, std::size_t count, std::size_t vocabulary,
                                                  std::size_t max_length) {
    std::mt19937 rng(seed);
    std::vector<double> weights(vocabulary);
    for (std::size_t i = 0; i < vocabulary; ++i) weights[i] = 1.0 / std::pow(static_cast<double>(i + 1), 1.05);
    std::discrete_distribution<std::size_t> word(weights.begin(), weights.end());
    std::uniform_int_distribution<std::size_t> length(0, max_length);

    std::vector<se::Document> docs;
    docs.reserve(count);
    for (std::size_t d = 0; d < count; ++d) {
        se::Document doc;
        doc.external_id = "doc" + std::to_string(d);
        doc.title = "Title " + std::to_string(d);
        const std::size_t len = length(rng);
        for (std::size_t i = 0; i < len; ++i) {
            if (i > 0) doc.text += ' ';
            doc.text += "w" + std::to_string(word(rng));
        }
        docs.push_back(std::move(doc));
    }
    return docs;
}

inline std::string random_query(std::mt19937& rng, std::size_t vocabulary, std::size_t max_terms) {
    std::uniform_int_distribution<std::size_t> terms(1, max_terms);
    std::vector<double> weights(vocabulary);
    for (std::size_t i = 0; i < vocabulary; ++i) weights[i] = 1.0 / std::pow(static_cast<double>(i + 1), 0.8);
    std::discrete_distribution<std::size_t> word(weights.begin(), weights.end());
    std::string query;
    const std::size_t n = terms(rng);
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0) query += ' ';
        query += "w" + std::to_string(word(rng));
    }
    return query;
}

inline std::vector<se::Document> make_documents(const std::vector<std::string>& texts) {
    std::vector<se::Document> docs;
    for (std::size_t i = 0; i < texts.size(); ++i) {
        docs.push_back({"d" + std::to_string(i), "", texts[i]});
    }
    return docs;
}

}
