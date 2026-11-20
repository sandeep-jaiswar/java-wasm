#include "verify.hpp"

#include <cctype>
#include <cstring>
#include <stdexcept>

#include <tree_sitter/api.h>

#include "ts_walk.hpp"

extern "C" const TSLanguage* tree_sitter_java(void);

namespace wb {
namespace {

bool is_type_declaration(const char* type) {
    return std::strcmp(type, "class_declaration") == 0 ||
           std::strcmp(type, "interface_declaration") == 0 ||
           std::strcmp(type, "enum_declaration") == 0 ||
           std::strcmp(type, "record_declaration") == 0;
}

std::string node_text(TSNode node, std::string_view source) {
    const auto start = ts_node_start_byte(node);
    const auto end = ts_node_end_byte(node);
    return std::string(source.substr(start, end - start));
}

std::string strip_space(std::string s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (!std::isspace(static_cast<unsigned char>(c))) out.push_back(c);
    }
    return out;
}

// Keyword modifiers ("public", "static", ...). Annotations are named nodes and are skipped.
std::vector<std::string> modifiers_of(TSNode decl) {
    std::vector<std::string> out;
    for_each_child(decl, false, [&](TSNode child) {
        if (std::strcmp(ts_node_type(child), "modifiers") != 0) return;
        for_each_child(child, false, [&](TSNode kw) {
            if (!ts_node_is_named(kw)) out.emplace_back(ts_node_type(kw));
        });
    });
    return out;
}

bool has_modifier(const std::vector<std::string>& mods, const std::string& kw) {
    for (const auto& m : mods) {
        if (m == kw) return true;
    }
    return false;
}

// Collects members that are direct children of a type body. Nested type declarations are
// visited separately by the main loop, so their members never leak into the outer type.
void collect_members(TSNode body, TypeDecl& decl, std::string_view source) {
    for_each_child(body, false, [&](TSNode child) {
        const char* type = ts_node_type(child);
        if (std::strcmp(type, "method_declaration") == 0) {
            const TSNode name = ts_node_child_by_field_name(child, "name", 4);
            if (ts_node_is_null(name)) return;
            MethodDecl m;
            m.name = node_text(name, source);
            const TSNode ret = ts_node_child_by_field_name(child, "type", 4);
            if (!ts_node_is_null(ret)) m.return_type = strip_space(node_text(ret, source));
            m.modifiers = modifiers_of(child);
            decl.methods.push_back(std::move(m));
        } else if (std::strcmp(type, "constructor_declaration") == 0) {
            decl.constructors.push_back({modifiers_of(child)});
        } else if (std::strcmp(type, "enum_body_declarations") == 0) {
            collect_members(child, decl, source);
        }
    });
}

std::string describe_in(const std::string& type) {
    return type.empty() ? std::string() : " in " + type;
}

// Reasons why one method declaration fails the spec. Empty means it satisfies the spec.
std::vector<std::string> method_violations(const MethodSpec& spec, const MethodDecl& m,
                                           const TypeDecl& owner) {
    std::vector<std::string> out;
    const std::string where = " in " + owner.name;
    const bool is_static = has_modifier(m.modifiers, "static");
    if (spec.is_static) {
        if (*spec.is_static && !is_static) out.push_back(spec.name + " must be static" + where);
        if (!*spec.is_static && is_static) {
            out.push_back(spec.name + " must not be static" + where);
        }
    }
    if (!spec.returns.empty() && m.return_type != strip_space(spec.returns)) {
        out.push_back(spec.name + " must return " + spec.returns + where);
    }
    if (!spec.modifier.empty() && !has_modifier(m.modifiers, spec.modifier)) {
        out.push_back(spec.name + " must be " + spec.modifier + where);
    }
    return out;
}

// Records syntax errors, type declarations and method names. Pre-order over the whole tree.
void walk_declarations(TSTree* tree, std::string_view source, Analysis& result) {
    ScopedCursor cursor(ts_tree_root_node(tree));
    do {
        const TSNode node = ts_tree_cursor_current_node(cursor.get());
        const char* type = ts_node_type(node);
        const bool missing = ts_node_is_missing(node);

        if (missing || std::strcmp(type, "ERROR") == 0) {
            result.errors.push_back({type, ts_node_start_point(node).row + 1, missing});
        }

        if (is_type_declaration(type)) {
            const TSNode name = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name)) {
                TypeDecl decl;
                decl.name = node_text(name, source);
                decl.kind = type;
                result.types.insert(decl.name);
                const TSNode body = ts_node_child_by_field_name(node, "body", 4);
                if (!ts_node_is_null(body)) collect_members(body, decl, source);
                result.type_decls.push_back(std::move(decl));
            }
        } else if (std::strcmp(type, "method_declaration") == 0) {
            // Flat set: also covers methods of anonymous classes, which are not scoped.
            const TSNode name = ts_node_child_by_field_name(node, "name", 4);
            if (!ts_node_is_null(name)) result.methods.insert(node_text(name, source));
        }
    } while (advance_preorder(cursor.get()));
}

}  // namespace

Verifier::Verifier() {
    parser_ = ts_parser_new();
    if (!ts_parser_set_language(parser_, tree_sitter_java())) {
        ts_parser_delete(parser_);
        throw std::runtime_error("tree-sitter Java language ABI mismatch");
    }
}

Verifier::~Verifier() {
    ts_parser_delete(parser_);
}

Analysis Verifier::analyze(std::string_view source) {
    Analysis result;
    TSTree* tree = ts_parser_parse_string(parser_, nullptr, source.data(),
                                          static_cast<std::uint32_t>(source.size()));
    if (!tree) return result;

    // Pre-order cursor walk: no recursion (no depth limit on deeply nested input) and no stack.
    walk_declarations(tree, source, result);
    ts_tree_delete(tree);

    result.ok = result.errors.empty();
    return result;
}

std::vector<std::string> check_requirements(const Analysis& analysis,
                                            const std::vector<std::string>& types,
                                            const std::vector<MethodSpec>& methods,
                                            const std::vector<CtorSpec>& constructors) {
    std::vector<std::string> problems;
    for (const auto& t : types) {
        if (!analysis.types.count(t)) problems.push_back("missing type " + t);
    }

    for (const auto& spec : methods) {
        const bool plain = spec.in.empty() && !spec.is_static && spec.returns.empty() &&
                           spec.modifier.empty();
        if (plain) {
            if (!analysis.methods.count(spec.name)) problems.push_back("missing method " + spec.name);
            continue;
        }

        // Candidate declarations: every type named `in`, or every type when `in` is empty.
        std::vector<const TypeDecl*> owners;
        for (const auto& decl : analysis.type_decls) {
            if (spec.in.empty() || decl.name == spec.in) owners.push_back(&decl);
        }
        if (!spec.in.empty() && owners.empty()) {
            problems.push_back("missing type " + spec.in);
            continue;
        }

        // The requirement holds if some same-named method satisfies every constraint.
        // Otherwise report the reasons the first candidate fails.
        std::vector<std::string> first_failure;
        bool found = false;
        bool satisfied = false;
        for (const TypeDecl* owner : owners) {
            for (const auto& m : owner->methods) {
                if (m.name != spec.name) continue;
                found = true;
                auto violations = method_violations(spec, m, *owner);
                if (violations.empty()) {
                    satisfied = true;
                    break;
                }
                if (first_failure.empty()) first_failure = std::move(violations);
            }
            if (satisfied) break;
        }
        if (satisfied) continue;
        if (!found) {
            problems.push_back("missing method " + spec.name + describe_in(spec.in));
        } else {
            problems.insert(problems.end(), first_failure.begin(), first_failure.end());
        }
    }

    for (const auto& spec : constructors) {
        std::vector<const ConstructorDecl*> ctors;
        bool type_found = false;
        for (const auto& decl : analysis.type_decls) {
            if (decl.name != spec.in) continue;
            type_found = true;
            for (const auto& c : decl.constructors) ctors.push_back(&c);
        }
        if (!type_found) {
            problems.push_back("missing type " + spec.in);
            continue;
        }
        if (ctors.empty()) {
            problems.push_back("missing " + (spec.modifier.empty() ? std::string() : spec.modifier + " ") +
                               "constructor in " + spec.in);
            continue;
        }
        if (spec.modifier.empty()) continue;
        for (const auto* c : ctors) {
            if (!has_modifier(c->modifiers, spec.modifier)) {
                problems.push_back("constructor of " + spec.in + " must be " + spec.modifier);
                break;
            }
        }
    }
    return problems;
}

}  // namespace wb
