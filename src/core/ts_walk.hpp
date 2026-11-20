#pragma once

#include <algorithm>
#include <cstdint>

#include <tree_sitter/api.h>

namespace wb {

// Owns a tree cursor and frees it on scope exit, including when a callback throws.
class ScopedCursor {
public:
    explicit ScopedCursor(TSNode node) : cursor_(ts_tree_cursor_new(node)) {}
    ~ScopedCursor() { ts_tree_cursor_delete(&cursor_); }
    ScopedCursor(const ScopedCursor&) = delete;
    ScopedCursor& operator=(const ScopedCursor&) = delete;

    TSTreeCursor* get() { return &cursor_; }

private:
    TSTreeCursor cursor_;
};

// Nodes with at most this many children use the index loop. Its cost is at most about 1000 steps
// there, and it allocates nothing, which a tree cursor does.
inline constexpr std::uint32_t kCursorChildThreshold = 32;

// Calls fn(child) for each child of `node` in source order, or only the named children when
// `named` is true. For wide nodes this walks with a tree cursor. ts_node_child(node, i) and
// ts_node_named_child(node, i) each scan from the first child, so an index loop over k children is
// O(k^2) in total; the cursor walk is O(k). The child counts read are stored values, so O(1).
template <typename Fn>
void for_each_child(TSNode node, bool named, Fn&& fn) {
    const std::uint32_t count = named ? ts_node_named_child_count(node) : ts_node_child_count(node);
    if (count <= kCursorChildThreshold) {
        for (std::uint32_t i = 0; i < count; ++i) {
            fn(named ? ts_node_named_child(node, i) : ts_node_child(node, i));
        }
        return;
    }
    ScopedCursor cursor(node);
    if (!ts_tree_cursor_goto_first_child(cursor.get())) return;
    do {
        const TSNode child = ts_tree_cursor_current_node(cursor.get());
        if (!named || ts_node_is_named(child)) fn(child);
    } while (ts_tree_cursor_goto_next_sibling(cursor.get()));
}

// Moves a cursor to the next node in pre-order. Returns false when the walk is back at the root.
inline bool advance_preorder(TSTreeCursor* cursor) {
    if (ts_tree_cursor_goto_first_child(cursor)) return true;
    for (;;) {
        if (ts_tree_cursor_goto_next_sibling(cursor)) return true;
        if (!ts_tree_cursor_goto_parent(cursor)) return false;
    }
}

}  // namespace wb
