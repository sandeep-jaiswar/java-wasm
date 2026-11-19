#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

#include "session.hpp"
#include "symbols.hpp"

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

using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

wb::Report report_of(const std::string& src) {
    wb::Session s;
    s.set_text(src);
    return s.analyze();
}

void test_declarations() {
    const std::string src =
        "package p;\n"
        "public class Outer {\n"
        "    private static final int X = 1, Y;\n"
        "    public Outer() {}\n"
        "    @Override public Map<String, Integer> run(final int a, String... rest) { return null; }\n"
        "    interface Shape { double area(); }\n"
        "    enum Color { RED, GREEN; void paint() {} }\n"
        "    record Point(int x, int y) {}\n"
        "}\n";
    const auto r = report_of(src);
    CHECK(r.diagnostics.empty());
    CHECK(!r.truncated);
    // Outer, X, Y, ctor, run, Shape, area, Color, paint, Point (record components are not symbols)
    CHECK(r.symbols.size() == 10);
    if (r.symbols.size() != 10) return;

    const auto& outer = r.symbols[0];
    CHECK(outer.kind == wb::SymbolKind::Class);
    CHECK(outer.name == "Outer");
    CHECK(outer.parent == -1);
    CHECK(outer.line == 2 && outer.column == 1);
    CHECK(outer.modifiers == std::vector<std::string>{"public"});

    CHECK(r.symbols[1].kind == wb::SymbolKind::Field && r.symbols[1].name == "X");
    CHECK(r.symbols[1].parent == 0);
    CHECK((r.symbols[1].modifiers == std::vector<std::string>{"private", "static", "final"}));
    CHECK(r.symbols[2].kind == wb::SymbolKind::Field && r.symbols[2].name == "Y");

    CHECK(r.symbols[3].kind == wb::SymbolKind::Constructor);
    CHECK(r.symbols[3].name == "Outer");
    CHECK(r.symbols[3].params.empty());

    const auto& run = r.symbols[4];
    CHECK(run.kind == wb::SymbolKind::Method);
    CHECK(run.name == "run");
    CHECK(run.return_type == "Map<String,Integer>");
    CHECK((run.params == std::vector<std::string>{"int", "String..."}));
    CHECK(run.modifiers.size() == 2 && run.modifiers[0] == "@Override" && run.modifiers[1] == "public");
    CHECK(run.parent == 0);
    CHECK(run.column == 5);

    CHECK(r.symbols[5].kind == wb::SymbolKind::Interface && r.symbols[5].parent == 0);
    CHECK(r.symbols[6].kind == wb::SymbolKind::Method && r.symbols[6].parent == 5);
    CHECK(r.symbols[7].kind == wb::SymbolKind::Enum && r.symbols[7].parent == 0);
    CHECK(r.symbols[8].kind == wb::SymbolKind::Method && r.symbols[8].parent == 7);
    CHECK(r.symbols[9].kind == wb::SymbolKind::Record && r.symbols[9].name == "Point");
    CHECK(r.symbols[9].parent == 0);

    // Byte range of the method covers its whole declaration, including the body.
    const auto text = src.substr(run.start_byte, run.end_byte - run.start_byte);
    CHECK(text.find("@Override") == 0);
    CHECK(text.back() == '}');
}

void test_diagnostics() {
    // Missing semicolon is reported as a missing token with a zero-width range.
    const auto missing = report_of("class A { int x = 1 }\n");
    CHECK(!missing.diagnostics.empty());
    bool saw_missing = false;
    for (const auto& d : missing.diagnostics) {
        if (d.missing && d.message == "missing ;") {
            saw_missing = true;
            CHECK(d.start_byte == d.end_byte);
            CHECK(d.line == 1);
        }
    }
    CHECK(saw_missing);

    // Garbage produces ERROR nodes with a range and the "syntax error" message.
    const auto bad = report_of("class A { void f() { int = = ; } }\n");
    CHECK(!bad.diagnostics.empty());
    for (const auto& d : bad.diagnostics) CHECK(d.end_byte >= d.start_byte);

    // Clean file: no diagnostics.
    CHECK(report_of("class A { void f() {} }\n").diagnostics.empty());
}

void test_truncation() {
    std::string src;
    for (int i = 0; i < 600; ++i) {
        src += "class C" + std::to_string(i) + " { int x = ; }\n";
    }
    const auto r = report_of(src);
    CHECK(r.truncated);
    CHECK(r.diagnostics.size() == wb::kMaxDiagnostics);
}

// Builds a class with `count` methods. Returns the text; `probe` gets the byte offset of
// the digit that test_incremental edits.
std::string big_class(int count) {
    std::string src = "public class Big {\n";
    src.reserve(static_cast<std::size_t>(count) * 64);
    for (int i = 0; i < count; ++i) {
        src += "    public int m" + std::to_string(i) + "(int a) {\n";
        src += "        return a + " + std::to_string(i) + ";\n";
        src += "    }\n";
    }
    src += "}\n";
    return src;
}

void test_incremental() {
    constexpr int kMethods = 20000;
    const std::string src = big_class(kMethods);

    wb::Session session;
    const auto t_full0 = Clock::now();
    session.set_text(src);
    const double full_ms = ms_since(t_full0);

    const auto baseline = session.analyze();
    CHECK(baseline.diagnostics.empty());
    CHECK(baseline.symbols.size() == kMethods + 1);

    // One-character edits: change the digit of "return a + 10000;" to 7. Repeated with
    // different edits so the timing is not a single sample.
    const std::string needle = "return a + 10000;";
    std::size_t at = session.text().find(needle);
    CHECK(at != std::string::npos);
    double best_incremental_ms = 1e9;
    for (int round = 0; round < 5 && at != std::string::npos; ++round) {
        const auto start = static_cast<std::uint32_t>(at + std::string("return a + ").size());
        const char replacement = static_cast<char>('1' + round);
        const auto t0 = Clock::now();
        session.apply_edit(start, start + 1, std::string(1, replacement));
        best_incremental_ms = std::min(best_incremental_ms, ms_since(t0));
    }

    std::printf("full parse: %.3f ms, incremental edit+reparse (best of 5): %.3f ms, speedup %.1fx\n",
                full_ms, best_incremental_ms, full_ms / best_incremental_ms);
    CHECK(best_incremental_ms * 5.0 <= full_ms);

    // Symbols after the edits equal a fresh full parse of the same final text.
    const auto incremental = session.analyze();
    const auto fresh = report_of(session.text());
    CHECK(incremental.symbols == fresh.symbols);
    CHECK(incremental.diagnostics == fresh.diagnostics);
    CHECK(incremental.symbols.size() == kMethods + 1);
}

void test_edit_bounds() {
    wb::Session s;
    s.set_text("class A {}");
    bool threw = false;
    try {
        s.apply_edit(5, 100, "x");
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(s.text() == "class A {}");
}

}  // namespace

int main() {
    test_declarations();
    test_diagnostics();
    test_truncation();
    test_edit_bounds();
    test_incremental();

    std::printf("test_symbols: %d passed, %d failed\n", passed, failures);
    return failures == 0 ? 0 : 1;
}
