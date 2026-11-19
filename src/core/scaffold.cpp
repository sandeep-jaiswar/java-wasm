#include "scaffold.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace wb {
namespace {

bool is_type_kind(SymbolKind kind) {
    return kind == SymbolKind::Class || kind == SymbolKind::Interface ||
           kind == SymbolKind::Enum || kind == SymbolKind::Record;
}

bool is_private(const Symbol& s) {
    return std::find(s.modifiers.begin(), s.modifiers.end(), "private") != s.modifiers.end();
}

// "add" -> "testAdd".
std::string test_base(const std::string& method) {
    std::string out = "test";
    if (!method.empty()) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(method[0]))));
        out.append(method, 1, std::string::npos);
    }
    return out;
}

}  // namespace

std::string scaffold_test_class(const std::string& package_name, const std::vector<Symbol>& symbols,
                                std::size_t class_index) {
    if (class_index >= symbols.size()) {
        throw std::out_of_range("scaffold_test_class: class_index outside symbols");
    }
    const Symbol& cls = symbols[class_index];
    if (!is_type_kind(cls.kind)) {
        throw std::invalid_argument("scaffold_test_class: symbol is not a class, interface, enum or record");
    }

    // One pass: collect the eligible methods of this type and count names for overloads.
    const auto parent = static_cast<std::int32_t>(class_index);
    std::vector<const Symbol*> methods;
    std::unordered_map<std::string, std::size_t> name_count;
    for (const Symbol& s : symbols) {
        if (s.parent != parent || s.kind != SymbolKind::Method || is_private(s)) continue;
        methods.push_back(&s);
        ++name_count[s.name];
    }

    std::string out;
    if (!package_name.empty()) {
        out += "package " + package_name + ";\n\n";
    }
    out += "public class " + cls.name + "Test {\n";

    // Every emitted name is unique: a collision with an earlier name gets "_" appended.
    std::unordered_set<std::string> used;
    std::unordered_map<std::string, std::size_t> ordinal;
    for (const Symbol* m : methods) {
        const std::string base = test_base(m->name);
        std::string id = base;
        if (name_count[m->name] > 1) {
            id = base + std::to_string(++ordinal[m->name]);
        }
        while (!used.insert(id).second) id += "_";

        out += "\n    public void " + id + "() {\n";
        out += "        throw new UnsupportedOperationException(\"TODO: " + cls.name + "." + m->name + "\");\n";
        out += "    }\n";
    }
    out += "}\n";
    return out;
}

std::vector<std::string> missing_test_classes(const std::vector<Symbol>& main_symbols,
                                              const std::vector<std::string>& test_class_names) {
    const std::unordered_set<std::string> tests(test_class_names.begin(), test_class_names.end());
    std::vector<std::string> missing;
    for (const Symbol& s : main_symbols) {
        if (s.parent != -1 || !is_type_kind(s.kind)) continue;
        if (tests.find(s.name + "Test") == tests.end()) missing.push_back(s.name);
    }
    return missing;
}

}  // namespace wb
