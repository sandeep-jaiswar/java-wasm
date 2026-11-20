#include "symbols.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

#include <tree_sitter/api.h>

#include "ts_walk.hpp"

namespace wb {
namespace {

bool is(TSNode node, const char* type) {
    return std::strcmp(ts_node_type(node), type) == 0;
}

std::string text_of(TSNode node, std::string_view source) {
    const auto start = ts_node_start_byte(node);
    const auto end = ts_node_end_byte(node);
    return std::string(source.substr(start, end - start));
}

// Drops whitespace so "Map<String, Integer>" and "Map<String,Integer>" compare equal.
std::string strip_space(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return out;
}

std::vector<std::string> modifiers_of(TSNode decl, std::string_view source) {
    std::vector<std::string> out;
    for_each_child(decl, false, [&](TSNode child) {
        if (!is(child, "modifiers")) return;
        for_each_child(child, false, [&](TSNode kw) {
            if (ts_node_is_named(kw)) {
                out.push_back(text_of(kw, source));  // annotation, e.g. @Override
            } else {
                out.emplace_back(ts_node_type(kw));  // keyword, e.g. public
            }
        });
    });
    return out;
}

// Parameter type texts of a method or constructor, in order.
std::vector<std::string> params_of(TSNode decl, std::string_view source) {
    std::vector<std::string> out;
    const TSNode list = ts_node_child_by_field_name(decl, "parameters", 10);
    if (ts_node_is_null(list)) return out;
    for_each_child(list, true, [&](TSNode p) {
        if (!is(p, "formal_parameter") && !is(p, "spread_parameter")) return;
        TSNode type = ts_node_child_by_field_name(p, "type", 4);
        std::string text;
        if (!ts_node_is_null(type)) {
            text = text_of(type, source);
        } else {
            // Varargs parameters may have no "type" field: take the first type-like child.
            const std::uint32_t n = ts_node_named_child_count(p);
            for (std::uint32_t j = 0; j < n; ++j) {
                const TSNode c = ts_node_named_child(p, j);
                if (is(c, "modifiers") || is(c, "variable_declarator") || is(c, "identifier") ||
                    is(c, "annotation") || is(c, "marker_annotation")) {
                    continue;
                }
                text = text_of(c, source);
                break;
            }
        }
        if (is(p, "spread_parameter")) text += "...";
        out.push_back(strip_space(text));
    });
    return out;
}

bool is_container(const char* type) {
    return std::strcmp(type, "program") == 0 || std::strcmp(type, "class_body") == 0 ||
           std::strcmp(type, "interface_body") == 0 || std::strcmp(type, "enum_body") == 0 ||
           std::strcmp(type, "enum_body_declarations") == 0 ||
           std::strcmp(type, "ERROR") == 0;
}

// Fills `out` with declarations in source order. Explicit stack: no recursion limit.
void collect_symbols(TSNode root, std::string_view source, std::vector<Symbol>& out) {
    struct Item {
        TSNode node;
        std::int32_t parent;
    };
    std::vector<Item> stack{{root, -1}};
    while (!stack.empty()) {
        const Item item = stack.back();
        stack.pop_back();
        const TSNode node = item.node;
        const char* type = ts_node_type(node);

        if (is_container(type)) {
            // Push in reverse so the first child pops first.
            const std::size_t base = stack.size();
            for_each_child(node, true, [&](TSNode child) { stack.push_back({child, item.parent}); });
            std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(base), stack.end());
            continue;
        }

        SymbolKind kind;
        bool is_type = false;
        if (std::strcmp(type, "class_declaration") == 0) {
            kind = SymbolKind::Class;
            is_type = true;
        } else if (std::strcmp(type, "interface_declaration") == 0) {
            kind = SymbolKind::Interface;
            is_type = true;
        } else if (std::strcmp(type, "enum_declaration") == 0) {
            kind = SymbolKind::Enum;
            is_type = true;
        } else if (std::strcmp(type, "record_declaration") == 0) {
            kind = SymbolKind::Record;
            is_type = true;
        } else if (std::strcmp(type, "method_declaration") == 0) {
            kind = SymbolKind::Method;
        } else if (std::strcmp(type, "constructor_declaration") == 0) {
            kind = SymbolKind::Constructor;
        } else if (std::strcmp(type, "field_declaration") == 0) {
            // One symbol per declarator: "int a, b;" yields two fields sharing modifiers.
            const auto mods = modifiers_of(node, source);
            const TSNode type_node = ts_node_child_by_field_name(node, "type", 4);
            for_each_child(node, true, [&](TSNode d) {
                if (!is(d, "variable_declarator")) return;
                const TSNode name = ts_node_child_by_field_name(d, "name", 4);
                if (ts_node_is_null(name)) return;
                Symbol s;
                s.kind = SymbolKind::Field;
                s.name = text_of(name, source);
                s.modifiers = mods;
                if (!ts_node_is_null(type_node)) s.type_text = strip_space(text_of(type_node, source));
                // Range covers the whole field declaration (modifiers and type included).
                s.start_byte = ts_node_start_byte(node);
                s.end_byte = ts_node_end_byte(node);
                s.line = ts_node_start_point(node).row + 1;
                s.column = ts_node_start_point(node).column + 1;
                s.parent = item.parent;
                out.push_back(std::move(s));
            });
            continue;
        } else {
            // Statements, expressions, and anything else that cannot hold a declaration
            // at this level. Method bodies and initializers are never entered.
            continue;
        }

        const TSNode name = ts_node_child_by_field_name(node, "name", 4);
        if (ts_node_is_null(name)) continue;

        Symbol s;
        s.kind = kind;
        s.name = text_of(name, source);
        s.modifiers = modifiers_of(node, source);
        s.start_byte = ts_node_start_byte(node);
        s.end_byte = ts_node_end_byte(node);
        s.line = ts_node_start_point(node).row + 1;
        s.column = ts_node_start_point(node).column + 1;
        s.parent = item.parent;
        if (kind == SymbolKind::Method) {
            const TSNode ret = ts_node_child_by_field_name(node, "type", 4);
            if (!ts_node_is_null(ret)) s.return_type = strip_space(text_of(ret, source));
        }
        if (kind == SymbolKind::Method || kind == SymbolKind::Constructor) {
            s.params = params_of(node, source);
        }
        const auto index = static_cast<std::int32_t>(out.size());
        out.push_back(std::move(s));

        if (is_type) {
            const TSNode body = ts_node_child_by_field_name(node, "body", 4);
            if (!ts_node_is_null(body)) stack.push_back({body, index});
        }
    }
}

void collect_diagnostics(TSNode root, std::string_view source, Report& report) {
    std::vector<TSNode> stack;
    if (ts_node_has_error(root)) stack.push_back(root);
    while (!stack.empty()) {
        const TSNode node = stack.back();
        stack.pop_back();
        // Subtrees without errors are skipped entirely; this is what keeps clean files cheap.
        if (!ts_node_has_error(node)) continue;

        const bool missing = ts_node_is_missing(node);
        const bool error = !missing && std::strcmp(ts_node_type(node), "ERROR") == 0;
        if (missing || error) {
            if (report.diagnostics.size() >= kMaxDiagnostics) {
                report.truncated = true;
                return;
            }
            const TSPoint p = ts_node_start_point(node);
            Diagnostic d;
            d.start_byte = ts_node_start_byte(node);
            d.end_byte = ts_node_end_byte(node);
            d.line = p.row + 1;
            d.column = p.column + 1;
            d.missing = missing;
            d.message = missing ? std::string("missing ") + ts_node_type(node) : "syntax error";
            report.diagnostics.push_back(std::move(d));
            continue;  // do not descend into an ERROR node: its children are part of the same error
        }
        const std::size_t base = stack.size();
        for_each_child(node, false, [&](TSNode child) { stack.push_back(child); });
        std::reverse(stack.begin() + static_cast<std::ptrdiff_t>(base), stack.end());
    }
}

}  // namespace

const char* symbol_kind_name(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::Class: return "class";
        case SymbolKind::Interface: return "interface";
        case SymbolKind::Enum: return "enum";
        case SymbolKind::Record: return "record";
        case SymbolKind::Method: return "method";
        case SymbolKind::Constructor: return "constructor";
        case SymbolKind::Field: return "field";
    }
    return "unknown";
}

bool Symbol::operator==(const Symbol& o) const {
    return kind == o.kind && name == o.name && modifiers == o.modifiers &&
           start_byte == o.start_byte && end_byte == o.end_byte && line == o.line &&
           column == o.column && parent == o.parent && return_type == o.return_type &&
           params == o.params && type_text == o.type_text;
}

bool Import::operator==(const Import& o) const {
    return text == o.text && line == o.line;
}

bool Diagnostic::operator==(const Diagnostic& o) const {
    return start_byte == o.start_byte && end_byte == o.end_byte && line == o.line &&
           column == o.column && message == o.message && missing == o.missing;
}

// "package a.b;" -> "a.b". Annotations on the package line are ignored.
std::string package_of(TSNode decl, std::string_view source) {
    std::string name;
    for_each_child(decl, true, [&](TSNode c) {
        if (is(c, "scoped_identifier") || is(c, "identifier")) name = strip_space(text_of(c, source));
    });
    return name;
}

// "import static a.B.c;" -> "static a.B.c". Built from the children so comments are dropped.
std::string import_text(TSNode decl, std::string_view source) {
    std::string prefix;
    std::string body;
    for_each_child(decl, false, [&](TSNode c) {
        if (ts_node_is_named(c)) {
            if (is(c, "line_comment") || is(c, "block_comment")) return;
            body += strip_space(text_of(c, source));
        } else if (is(c, "static")) {
            prefix = "static ";
        } else if (is(c, ".") || is(c, "*")) {
            body += ts_node_type(c);
        }
    });
    return prefix + body;
}

Report extract_report(const TSTree* tree, std::string_view source) {
    Report report;
    if (!tree) return report;
    const TSNode root = ts_tree_root_node(tree);
    for_each_child(root, true, [&](TSNode c) {
        if (is(c, "package_declaration")) {
            report.package = package_of(c, source);
        } else if (is(c, "import_declaration")) {
            Import imp;
            imp.text = import_text(c, source);
            imp.line = ts_node_start_point(c).row + 1;
            report.imports.push_back(std::move(imp));
        }
    });
    collect_symbols(root, source, report.symbols);
    collect_diagnostics(root, source, report);
    return report;
}

}  // namespace wb
