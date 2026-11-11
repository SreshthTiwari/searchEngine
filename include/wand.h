#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "bm25.h"

namespace se {

template <class Index>
std::vector<SearchHit> wand_search(const Index& index, const Bm25Model& model,
                                   std::span<const double> upper_bounds,
                                   std::span<const std::uint32_t> terms, std::size_t k,
                                   QueryStats* stats = nullptr);

}
