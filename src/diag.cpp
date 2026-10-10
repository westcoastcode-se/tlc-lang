#include "diag.hpp"

namespace tlc {

void DiagnosticEngine::error(SourceLoc loc, std::string message) {
    items_.push_back(Diagnostic{loc, std::move(message)});
}

std::string DiagnosticEngine::render() const {
    std::string out;
    for (const Diagnostic &item : items_) {
        if (const SourceFile *file = sources_->find(item.loc.file)) {
            const LineCol at = file->line_col(item.loc.offset);
            out += file->path();
            out += ':';
            out += std::to_string(at.line);
            out += ':';
            out += std::to_string(at.column);
            out += ": error: ";
        } else {
            out += "error: ";
        }
        out += item.message;
        out += '\n';
    }
    return out;
}

} // namespace tlc
