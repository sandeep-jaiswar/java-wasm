// File-based catalog loading. Native only: the browser build never reads files,
// so this translation unit is excluded from the WASM target (see CMakeLists.txt).
#include "catalog.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

namespace wb {

Catalog Catalog::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("cannot open catalog: " + path);
    std::stringstream buf;
    buf << in.rdbuf();
    return parse(buf.str());
}

}  // namespace wb
