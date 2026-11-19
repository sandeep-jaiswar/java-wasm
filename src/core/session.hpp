#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "symbols.hpp"

struct TSParser;
struct TSTree;

namespace wb {

// Editor-side state for one Java source buffer: the text, its parse tree, and a parser.
// Not thread-safe. Reuse one Session per open buffer.
class Session {
public:
    Session();
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Replaces the whole text and parses it from scratch.
    void set_text(std::string text);

    // Replaces bytes [start_byte, old_end_byte) with new_text. Calls ts_tree_edit on the old
    // tree, then reparses with that tree so unchanged subtrees are reused.
    // Throws std::out_of_range when the range is outside the current text.
    // Byte offsets are not checked for UTF-8 character boundaries: the tree is byte-based.
    void apply_edit(std::uint32_t start_byte, std::uint32_t old_end_byte, std::string_view new_text);

    // Diagnostics and symbols for the current tree. Returns an empty report before set_text.
    Report analyze() const;

    const std::string& text() const { return text_; }

private:
    TSParser* parser_ = nullptr;
    TSTree* tree_ = nullptr;
    std::string text_;
};

}  // namespace wb
