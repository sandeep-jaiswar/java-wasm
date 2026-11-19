#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "verify.hpp"

namespace wb {

// Strings may contain {{Param}} placeholders. A plain method name in the catalog becomes a
// MethodSpec with only `name` set.
struct Requirements {
    std::vector<std::string> types;
    std::vector<MethodSpec> methods;
    std::vector<CtorSpec> constructors;
};

struct Pattern {
    std::string id;
    std::string name;
    std::string intent;
    std::vector<std::string> params;   // e.g. {"Name"}
    std::string templ;                 // Java source with {{Param}} placeholders
    Requirements requires;
};

class Catalog {
public:
    // Parses catalog JSON text. Throws std::runtime_error or nlohmann::json exceptions on bad input.
    static Catalog parse(std::string_view json);

    // Reads a file and parses it (defined in catalog_file.cpp, native builds only).
    // Throws std::runtime_error when the file cannot be read.
    static Catalog load(const std::string& path);

    const std::vector<Pattern>& patterns() const { return patterns_; }
    // O(1) lookup by id; returns nullptr when unknown.
    const Pattern* find(const std::string& id) const;

private:
    std::vector<Pattern> patterns_;
    std::unordered_map<std::string, std::size_t> index_;
};

}  // namespace wb
