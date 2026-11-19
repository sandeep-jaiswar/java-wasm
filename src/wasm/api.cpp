// C ABI for the browser build. Each call returns a JSON string in a buffer owned by this file;
// the JS side copies it immediately (the buffer is overwritten by the next call).
//
// Source text is passed as an explicit (pointer, length) pair in heap memory that the JS side
// allocates with _malloc. Emscripten's cwrap 'string' marshalling uses the WASM shadow stack,
// which overflows on large inputs. Short identifiers (pattern id, class name) still use cwrap.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <string_view>

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

// Largest source text (catalog JSON or Java source) accepted in one call.
constexpr std::size_t kMaxInputBytes = 4 * 1024 * 1024;
// Largest short identifier (pattern id or class name) accepted from cwrap.
constexpr std::size_t kMaxIdBytes = 256;

wb::Catalog g_catalog;
std::unique_ptr<wb::Verifier> g_verifier;
std::string g_out;

const char* respond(const nlohmann::json& j) noexcept {
    try {
        g_out = j.dump();
    } catch (...) {
        g_out = R"({"ok":false,"error":"internal error"})";
    }
    return g_out.c_str();
}

nlohmann::json error_json(const std::string& message) {
    return {{"ok", false}, {"error", message}};
}

// Bounded read of a NUL-terminated argument. Returns false when it is null or too long.
bool read_id(const char* s, std::string_view& out) {
    if (!s) return false;
    const std::size_t n = ::strnlen(s, kMaxIdBytes + 1);
    if (n > kMaxIdBytes) return false;
    out = std::string_view(s, n);
    return true;
}

}  // namespace

extern "C" {

// Loads the catalog from (json, len) and creates the verifier. Returns 1 on success, 0 on failure
// (including input larger than kMaxInputBytes).
WB_EXPORT int wb_init(const char* json, uint32_t len) {
    try {
        if (!json || len > kMaxInputBytes) return 0;
        g_catalog = wb::Catalog::parse(std::string_view(json, len));
        g_verifier = std::make_unique<wb::Verifier>();
        return 1;
    } catch (...) {
        return 0;
    }
}

// Returns {"ok":bool,"source":string,"rejections":[string]} or {"ok":false,"error":string}.
WB_EXPORT const char* wb_generate(const char* pattern_id, const char* class_name) {
    try {
        if (!g_verifier) return respond(error_json("not initialised"));
        std::string_view id;
        std::string_view name;
        if (!read_id(pattern_id, id) || !read_id(class_name, name)) {
            return respond(error_json("invalid argument"));
        }
        const wb::Pattern* pattern = g_catalog.find(std::string(id));
        if (!pattern) return respond(error_json("unknown pattern " + std::string(id)));
        if (!wb::is_identifier(name)) return respond(error_json("invalid class name"));
        const auto r = wb::generate(*g_verifier, *pattern, {{"Name", std::string(name)}});
        return respond({{"ok", r.ok}, {"source", r.source}, {"rejections", r.rejections}});
    } catch (const std::exception& e) {
        return respond(error_json(e.what()));
    } catch (...) {
        return respond(error_json("internal error"));
    }
}

// Returns {"ok":bool,"errors":[{"line":n,"type":string,"missing":bool}]}, or
// {"ok":false,"error":"input too large"} when len exceeds kMaxInputBytes.
WB_EXPORT const char* wb_check(const char* src, uint32_t len) {
    try {
        if (!g_verifier) {
            return respond({{"ok", false}, {"error", "not initialised"}, {"errors", nlohmann::json::array()}});
        }
        if (!src) return respond(error_json("invalid argument"));
        if (len > kMaxInputBytes) return respond(error_json("input too large"));
        const auto a = g_verifier->analyze(std::string_view(src, len));
        nlohmann::json errors = nlohmann::json::array();
        for (const auto& e : a.errors) {
            errors.push_back({{"line", e.line}, {"type", e.node_type}, {"missing", e.missing}});
        }
        return respond({{"ok", a.ok}, {"errors", errors}});
    } catch (const std::exception& e) {
        return respond(error_json(e.what()));
    } catch (...) {
        return respond(error_json("internal error"));
    }
}

}  // extern "C"
