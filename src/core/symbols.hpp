#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct TSTree;

namespace wb {

enum class SymbolKind : std::uint8_t { Class, Interface, Enum, Record, Method, Constructor, Field };

const char* symbol_kind_name(SymbolKind kind);

// One declaration. Positions: bytes are 0-based offsets; line and column are 1-based
// (column counts bytes, not characters). The range covers the whole declaration; for a
// multi-variable field ("int a, b;") both fields share the field declaration's range.
// `parent` is the index of the enclosing declaration in the same vector, or -1 at top level.
struct Symbol {
    SymbolKind kind = SymbolKind::Class;
    std::string name;
    std::vector<std::string> modifiers;  // keywords and annotation text, in source order
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::int32_t parent = -1;
    std::string return_type;             // methods only; empty otherwise
    std::vector<std::string> params;     // methods and constructors: parameter type text

    bool operator==(const Symbol& o) const;
};

struct Diagnostic {
    std::uint32_t start_byte = 0;
    std::uint32_t end_byte = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string message;  // "syntax error" or "missing <token>"
    bool missing = false;

    bool operator==(const Diagnostic& o) const;
};

// Diagnostics are capped so output stays bounded; `truncated` is set when the cap was hit.
constexpr std::size_t kMaxDiagnostics = 200;

struct Report {
    std::vector<Diagnostic> diagnostics;
    std::vector<Symbol> symbols;
    bool truncated = false;
};

// Extracts diagnostics and declarations from an existing tree. `source` must be the exact
// text the tree was parsed from. Walks only declaration nodes (method and constructor bodies
// are skipped) and only subtrees that contain errors, so clean files cost O(declarations).
Report extract_report(const TSTree* tree, std::string_view source);

}  // namespace wb
