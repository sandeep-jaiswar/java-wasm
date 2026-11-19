#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "symbols.hpp"

namespace wb {

// Builds the text of a JUnit-free test stub class for the type at `symbols[class_index]`.
// The class is named "<Name>Test" in `package_name` (no package line when empty). It has one
// "public void test<Method>()" per non-private (public, protected or package) method whose parent is that type, in declaration
// order. Overloaded names get a 1-based numeric suffix. Each body throws
// UnsupportedOperationException, so the stub fails until someone writes the test.
// Throws std::out_of_range for a bad index and std::invalid_argument when the symbol is not a
// class, interface, enum or record. Single pass over `symbols`.
std::string scaffold_test_class(const std::string& package_name, const std::vector<Symbol>& symbols,
                                std::size_t class_index);

// Returns the names of top-level types (parent == -1) in `main_symbols` that have no
// "<Name>Test" in `test_class_names`. Output keeps symbol order. O(n) with a hash set.
std::vector<std::string> missing_test_classes(const std::vector<Symbol>& main_symbols,
                                              const std::vector<std::string>& test_class_names);

}  // namespace wb
