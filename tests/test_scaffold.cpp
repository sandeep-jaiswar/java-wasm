#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "scaffold.hpp"
#include "session.hpp"
#include "symbols.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            ++failures;                                                          \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

std::vector<wb::Symbol> symbols_of(const std::string& src) {
    wb::Session s;
    s.set_text(src);
    return s.analyze().symbols;
}

std::size_t index_of(const std::vector<wb::Symbol>& syms, const std::string& name) {
    for (std::size_t i = 0; i < syms.size(); ++i) {
        if (syms[i].name == name) return i;
    }
    return syms.size();
}

bool contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

void test_cart() {
    const std::string src =
        "package demo;\n"
        "public class Cart {\n"
        "    public void add(Item item) {}\n"
        "    public int total() { return 0; }\n"
        "    private void helper() {}\n"
        "    private int count;\n"
        "}\n";
    const auto syms = symbols_of(src);
    const std::size_t cart = index_of(syms, "Cart");
    CHECK(cart < syms.size());
    if (cart >= syms.size()) return;

    const std::string text = wb::scaffold_test_class("demo", syms, cart);
    CHECK(contains(text, "package demo;"));
    CHECK(contains(text, "class CartTest"));
    CHECK(contains(text, "public void testAdd()"));
    CHECK(contains(text, "public void testTotal()"));
    CHECK(!contains(text, "testHelper"));
    CHECK(!contains(text, "testCount"));
    CHECK(contains(text, "UnsupportedOperationException(\"TODO: Cart.add\")"));

    const std::string no_pkg = wb::scaffold_test_class("", syms, cart);
    CHECK(!contains(no_pkg, "package"));

    // Declaration order: testAdd appears before testTotal.
    CHECK(text.find("testAdd") < text.find("testTotal"));

    // Bad inputs are reported, not silently accepted.
    bool threw = false;
    try {
        wb::scaffold_test_class("demo", syms, syms.size());
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);

    // Compile check: the generated test plus a stub Cart must build with javac --release 17.
    if (std::system("javac -version > /dev/null 2>&1") != 0) {
        std::fprintf(stderr, "javac not on PATH; skipping compile check\n");
        return;
    }
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "wb_scaffold_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "out");
    {
        std::ofstream(dir / "CartTest.java") << text;
        std::ofstream(dir / "Cart.java")
            << "package demo;\npublic class Cart {\n"
               "    public static class Item {}\n"
               "    public void add(Item item) {}\n"
               "    public int total() { return 0; }\n}\n";
    }
    const std::string cmd = "javac --release 17 -d \"" + (dir / "out").string() + "\" \"" +
                            (dir / "CartTest.java").string() + "\" \"" +
                            (dir / "Cart.java").string() + "\" 2>&1";
    const int rc = std::system(cmd.c_str());
    if (rc != 0) {
        std::fprintf(stderr, "javac failed on %s\n", (dir / "CartTest.java").string().c_str());
    }
    CHECK(rc == 0);
    fs::remove_all(dir);
}

void test_overloads() {
    const std::string src =
        "public class Calc {\n"
        "    public void f(int x) {}\n"
        "    public void f(String s) {}\n"
        "    static void g() {}\n"
        "    protected void p() {}\n"
        "}\n";
    const auto syms = symbols_of(src);
    const std::string text = wb::scaffold_test_class("", syms, index_of(syms, "Calc"));
    CHECK(contains(text, "public void testF1()"));
    CHECK(contains(text, "public void testF2()"));
    CHECK(!contains(text, "public void testF()"));
    CHECK(contains(text, "public void testG()"));  // static, package-visible: included
    CHECK(contains(text, "public void testP()"));  // protected: included
}

void test_missing() {
    const auto syms = symbols_of(
        "package p;\n"
        "class Cart {}\n"
        "interface Store {}\n"
        "record Point(int x, int y) {}\n"
        "class Inner { class Nested {} }\n");
    const auto missing = wb::missing_test_classes(syms, {"CartTest", "PointTest", "OtherTest"});
    CHECK(missing.size() == 2);
    CHECK(missing.size() == 2 && missing[0] == "Store");
    CHECK(missing.size() == 2 && missing[1] == "Inner");

    const auto all = wb::missing_test_classes(syms, {});
    CHECK(all.size() == 4);
}

}  // namespace

int main() {
    test_cart();
    test_overloads();
    test_missing();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("scaffold: all checks passed\n");
    return 0;
}
