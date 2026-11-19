#include <cstdio>
#include <stdexcept>
#include <string>

#include "catalog.hpp"
#include "engine.hpp"
#include "verify.hpp"

namespace {

int failures = 0;
int passed = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (cond) {                                                              \
            ++passed;                                                            \
        } else {                                                                 \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: test_core <catalog.json>\n");
        return 2;
    }
    const wb::Catalog catalog = wb::Catalog::load(argv[1]);
    wb::Verifier verifier;

    // Every catalog template generates valid, structurally correct Java with name Demo.
    for (const auto& p : catalog.patterns()) {
        const auto r = wb::generate(verifier, p, {{"Name", "Demo"}});
        if (!r.ok) {
            ++failures;
            std::fprintf(stderr, "FAIL pattern %s did not generate:\n", p.id.c_str());
            for (const auto& why : r.rejections) std::fprintf(stderr, "  %s\n", why.c_str());
        } else {
            ++passed;
        }
    }

    // Syntax error is rejected.
    {
        const auto* p = catalog.find("singleton");
        CHECK(p != nullptr);
        const auto broken = wb::fill(p->templ, {{"Name", "Demo"}});
        const auto r = wb::generate(verifier, *p, {{"Name", "Demo"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{broken.substr(0, broken.size() - 2)};
                                    });
        CHECK(!r.ok);
        CHECK(r.rejections.size() == 1 && r.rejections[0].rfind("syntax", 0) == 0);
    }

    // Missing required method is rejected by the structure check.
    {
        const auto* p = catalog.find("singleton");
        auto src = wb::fill(p->templ, {{"Name", "Demo"}});
        const auto pos = src.find("getInstance()");
        src.replace(pos, 13, "fetch()");
        const auto r = wb::generate(verifier, *p, {{"Name", "Demo"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{src};
                                    });
        CHECK(!r.ok);
        CHECK(r.rejections.size() == 1 && r.rejections[0].find("getInstance") != std::string::npos);
    }

    // Best-of-N returns the first valid candidate.
    {
        const auto* p = catalog.find("strategy");
        const auto good = wb::fill(p->templ, {{"Name", "Sort"}});
        const auto broken = good.substr(0, good.size() - 2);
        const auto r = wb::generate(verifier, *p, {{"Name", "Sort"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{broken, good};
                                    });
        CHECK(r.ok);
        CHECK(r.source == good);
        CHECK(r.rejections.size() == 1);
    }

    // All-invalid candidates report failure.
    {
        const auto* p = catalog.find("observer");
        const auto broken = wb::fill(p->templ, {{"Name", "Feed"}});
        const auto bad = broken.substr(0, broken.size() - 2);
        const auto r = wb::generate(verifier, *p, {{"Name", "Feed"}},
                                    [&](const auto&, const auto&, const auto&) {
                                        return std::vector<std::string>{bad, bad};
                                    });
        CHECK(!r.ok);
        CHECK(r.source.empty());
        CHECK(r.rejections.size() == 2);
    }

    // Invalid identifier throws before any work.
    {
        bool threw = false;
        try {
            wb::generate(verifier, *catalog.find("singleton"), {{"Name", "bad name"}});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // Unknown pattern id.
    CHECK(catalog.find("visitor") == nullptr);

    // analyze reports declared types and methods.
    {
        const auto a = verifier.analyze("class A { void m() {} }");
        CHECK(a.ok);
        CHECK(a.types.count("A") == 1);
        CHECK(a.methods.count("m") == 1);
    }

    // fill rejects missing keys.
    {
        bool threw = false;
        try {
            wb::fill("{{Missing}}", {});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        CHECK(threw);
    }

    // Singleton verification against the structured requirements (Name = Demo).
        const auto* singleton = catalog.find("singleton");
    CHECK(singleton != nullptr);
    auto verify_singleton = [&](const std::string& src) {
        return wb::generate(verifier, *singleton, {{"Name", "Demo"}},
                            [&](const auto&, const auto&, const auto&) {
                                return std::vector<std::string>{src};
                            });
    };
    auto rejected_with = [](const wb::GenerateResult& r, const std::string& text) {
        if (r.ok || r.rejections.size() != 1) return false;
        return r.rejections[0].find(text) != std::string::npos;
    };

    // Audit 1: getInstance is not static.
    {
        const auto r = verify_singleton(
            "public final class Demo { private Demo() {} public Demo getInstance() { return null; } }");
        CHECK(rejected_with(r, "getInstance must be static in Demo"));
    }
    // Audit 2: getInstance is declared in another type.
    {
        const auto r = verify_singleton(
            "class Other { public static Other getInstance() { return null; } } "
            "public final class Demo { private Demo() {} }");
        CHECK(rejected_with(r, "missing method getInstance in Demo"));
    }
    // Audit 3: public constructor.
    {
        const auto r = verify_singleton(
            "public final class Demo { public Demo() {} public static Demo getInstance() { return null; } }");
        CHECK(rejected_with(r, "constructor of Demo must be private"));
    }
    // Valid singleton.
    {
        const auto r = verify_singleton(
            "public final class Demo { private static Demo instance; private Demo() {} "
            "public static synchronized Demo getInstance() { return instance; } }");
        CHECK(r.ok);
    }
    // Stay rejected: a field named getInstance is not a method.
    CHECK(!verify_singleton("public final class Demo { int getInstance; }").ok);
    // Stay rejected: a string literal is not a declaration.
    CHECK(!verify_singleton("public final class Demo { String s = \"getInstance\"; }").ok);
    // Nested: a Demo nested in another type is still checked as Demo.
    CHECK(verify_singleton(
              "public final class Outer { public static final class Demo { private Demo() {} "
              "public static Demo getInstance() { return null; } } }")
              .ok);
    // Nested: a method in an inner type does not satisfy the outer Demo.
    CHECK(!verify_singleton(
              "public final class Demo { private Demo() {} static final class Inner { "
              "public static Inner getInstance() { return null; } } }")
              .ok);

    std::printf("%d checks passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
