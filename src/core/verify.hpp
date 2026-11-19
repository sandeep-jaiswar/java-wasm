#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct TSParser;

namespace wb {

struct SyntaxIssue {
    std::string node_type;  // "ERROR" or the expected token for missing nodes
    std::uint32_t line;     // 1-based
    bool missing;
};

// A method declared directly in a type body (not in an anonymous class or lambda).
struct MethodDecl {
    std::string name;
    std::string return_type;               // source text, whitespace removed
    std::vector<std::string> modifiers;    // keywords such as "public", "static"
};

// A constructor declared directly in a type body.
struct ConstructorDecl {
    std::vector<std::string> modifiers;
};

// A class, interface, enum or record declaration with its own members.
struct TypeDecl {
    std::string name;
    std::string kind;  // tree-sitter node type, e.g. "class_declaration"
    std::vector<MethodDecl> methods;
    std::vector<ConstructorDecl> constructors;
};

struct Analysis {
    bool ok = false;
    std::vector<SyntaxIssue> errors;
    std::unordered_set<std::string> types;    // flat: every type name in the file
    std::unordered_set<std::string> methods;  // flat: every method name in the file
    std::vector<TypeDecl> type_decls;         // scoped: members per declaring type
};

// Requirement on a method. Empty `in` matches a method in any type.
// Unset optional fields and empty strings are not checked.
struct MethodSpec {
    std::string name;
    std::string in;
    std::optional<bool> is_static;
    std::string returns;   // compared with whitespace removed
    std::string modifier;  // e.g. "public"
};

// Requirement on constructors of type `in`: at least one must exist and every one must
// carry `modifier`.
struct CtorSpec {
    std::string in;
    std::string modifier;
};

// Owns one tree-sitter parser configured for Java. Reuse it across calls.
class Verifier {
public:
    Verifier();
    ~Verifier();
    Verifier(const Verifier&) = delete;
    Verifier& operator=(const Verifier&) = delete;

    // One iterative pass over the syntax tree: O(n) in source size.
    Analysis analyze(std::string_view source);

private:
    TSParser* parser_ = nullptr;
};

// Returns problems such as "getInstance must be static in Demo". Empty means the
// requirements hold. Specs with no `in` and no other constraint use the flat name sets.
std::vector<std::string> check_requirements(const Analysis& analysis,
                                            const std::vector<std::string>& types,
                                            const std::vector<MethodSpec>& methods,
                                            const std::vector<CtorSpec>& constructors);

}  // namespace wb
