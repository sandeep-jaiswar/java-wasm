#include "session.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include <tree_sitter/api.h>

extern "C" const TSLanguage* tree_sitter_java(void);

namespace wb {
namespace {

// Advances a point over `s`. Columns are byte counts from the last newline.
TSPoint advance(TSPoint p, std::string_view s) {
    p.row += static_cast<std::uint32_t>(std::count(s.begin(), s.end(), '\n'));
    const std::size_t last = s.rfind('\n');
    if (last == std::string_view::npos) {
        p.column += static_cast<std::uint32_t>(s.size());
    } else {
        p.column = static_cast<std::uint32_t>(s.size() - last - 1);
    }
    return p;
}

}  // namespace

Session::Session() {
    parser_ = ts_parser_new();
    if (!ts_parser_set_language(parser_, tree_sitter_java())) {
        ts_parser_delete(parser_);
        parser_ = nullptr;
        throw std::runtime_error("tree-sitter Java language ABI mismatch");
    }
}

Session::~Session() {
    ts_tree_delete(tree_);
    ts_parser_delete(parser_);
}

void Session::set_text(std::string text) {
    if (text.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("source too large");
    }
    text_ = std::move(text);
    ts_tree_delete(tree_);
    tree_ = ts_parser_parse_string(parser_, nullptr, text_.data(),
                                   static_cast<std::uint32_t>(text_.size()));
    if (!tree_) throw std::runtime_error("tree-sitter parse failed");
}

void Session::apply_edit(std::uint32_t start_byte, std::uint32_t old_end_byte,
                         std::string_view new_text) {
    if (start_byte > old_end_byte || old_end_byte > text_.size()) {
        throw std::out_of_range("edit range outside text");
    }
    const std::size_t new_len = text_.size() - (old_end_byte - start_byte) + new_text.size();
    if (new_len > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("source too large");
    }

    // Points are computed before the splice: start and old end in the old text, new end from
    // the inserted text alone (advance from start, no rescan of the whole buffer).
    const TSPoint start_point = advance(TSPoint{0, 0}, std::string_view(text_).substr(0, start_byte));
    const TSPoint old_end_point =
        advance(start_point, std::string_view(text_).substr(start_byte, old_end_byte - start_byte));
    const TSPoint new_end_point = advance(start_point, new_text);

    text_.replace(start_byte, old_end_byte - start_byte, new_text);

    if (tree_) {
        const TSInputEdit edit{start_byte,
                               old_end_byte,
                               static_cast<std::uint32_t>(start_byte + new_text.size()),
                               start_point,
                               old_end_point,
                               new_end_point};
        ts_tree_edit(tree_, &edit);
    }

    TSTree* next = ts_parser_parse_string(parser_, tree_, text_.data(),
                                          static_cast<std::uint32_t>(text_.size()));
    ts_tree_delete(tree_);
    tree_ = next;  // null only if parsing was cancelled; analyze() then reports nothing
    if (!tree_) throw std::runtime_error("tree-sitter reparse failed");
}

Report Session::analyze() const {
    return extract_report(tree_, text_);
}

}  // namespace wb
