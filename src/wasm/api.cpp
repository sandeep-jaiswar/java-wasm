// C ABI for the browser build. Each call returns a JSON string in a buffer owned by this file;
// the JS side copies it immediately (the buffer is overwritten by the next call).
#include <string>

#include <nlohmann/json.hpp>

#include "catalog.hpp"
#include "engine.hpp"
#include "verify.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define WB_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WB_EXPORT
#endif

namespace {

wb::Catalog g_catalog;
wb::Verifier* g_verifier = nullptr;
std::string g_out;

const char* respond(const nlohmann::json& j) {
    g_out = j.dump();
    return g_out.c_str();
}

}  // namespace

extern "C" {

// Loads the catalog and creates the verifier. Returns 1 on success, 0 on failure.
WB_EXPORT int wb_init(const char* catalog_json) {
    try {
        g_catalog = wb::Catalog::parse(catalog_json);
        delete g_verifier;
        g_verifier = new wb::Verifier();
        return 1;
    } catch (...) {
        return 0;
    }
}

// Returns {"ok":bool,"source":string,"rejections":[string],"error":string}.
WB_EXPORT const char* wb_generate(const char* pattern_id, const char* class_name) {
    if (!g_verifier) return respond({{"ok", false}, {"error", "not initialised"}});
    const wb::Pattern* pattern = g_catalog.find(pattern_id);
    if (!pattern) return respond({{"ok", false}, {"error", std::string("unknown pattern ") + pattern_id}});
    try {
        const auto r = wb::generate(*g_verifier, *pattern, {{"Name", class_name}});
        return respond({{"ok", r.ok}, {"source", r.source}, {"rejections", r.rejections}});
    } catch (const std::exception& e) {
        return respond({{"ok", false}, {"error", e.what()}});
    }
}

// Returns {"ok":bool,"errors":[{"line":n,"type":string,"missing":bool}]}.
WB_EXPORT const char* wb_check(const char* source) {
    if (!g_verifier) return respond({{"ok", false}, {"errors", nlohmann::json::array()}});
    const auto a = g_verifier->analyze(source);
    nlohmann::json errors = nlohmann::json::array();
    for (const auto& e : a.errors) {
        errors.push_back({{"line", e.line}, {"type", e.node_type}, {"missing", e.missing}});
    }
    return respond({{"ok", a.ok}, {"errors", errors}});
}

}  // extern "C"
