#include "catalog.hpp"

#include <nlohmann/json.hpp>

namespace wb {
namespace {

// A string names a method that must exist somewhere. An object adds structure:
// {"name":"getInstance","in":"{{Name}}","static":true,"returns":"{{Name}}","modifier":"public"}
MethodSpec parse_method_spec(const nlohmann::json& m) {
    if (m.is_string()) {
        MethodSpec spec;
        spec.name = m.get<std::string>();
        return spec;
    }
    MethodSpec spec;
    spec.name = m.at("name").get<std::string>();
    spec.in = m.value("in", "");
    if (m.contains("static")) spec.is_static = m.at("static").get<bool>();
    spec.returns = m.value("returns", "");
    spec.modifier = m.value("modifier", "");
    return spec;
}

}  // namespace

Catalog Catalog::parse(std::string_view json) {
    const auto doc = nlohmann::json::parse(json);
    Catalog catalog;
    for (const auto& p : doc.at("patterns")) {
        Pattern pattern;
        pattern.id = p.at("id").get<std::string>();
        pattern.name = p.at("name").get<std::string>();
        pattern.intent = p.value("intent", "");
        pattern.params = p.at("params").get<std::vector<std::string>>();
        pattern.templ = p.at("template").get<std::string>();
        const auto& req = p.at("requires");
        pattern.requires.types = req.value("types", std::vector<std::string>{});
        for (const auto& m : req.value("methods", nlohmann::json::array())) {
            pattern.requires.methods.push_back(parse_method_spec(m));
        }
        for (const auto& c : req.value("constructors", nlohmann::json::array())) {
            CtorSpec spec;
            spec.in = c.at("constructor").get<std::string>();
            spec.modifier = c.value("modifier", "");
            pattern.requires.constructors.push_back(std::move(spec));
        }
        catalog.index_[pattern.id] = catalog.patterns_.size();
        catalog.patterns_.push_back(std::move(pattern));
    }
    return catalog;
}

const Pattern* Catalog::find(const std::string& id) const {
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : &patterns_[it->second];
}

}  // namespace wb
