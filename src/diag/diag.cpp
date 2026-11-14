#include "diag/diag.hpp"

namespace tlc {

void DiagnosticEngine::error(SourceLoc loc, std::string message) {
    items_.push_back(Diagnostic{loc, std::move(message)});
}

std::string DiagnosticEngine::render() const {
    return render(items_.size());
}

std::string DiagnosticEngine::render(std::size_t limit) const {
    std::string out;
    std::size_t shown = 0;
    for (const Diagnostic &item : items_) {
        if (shown == limit) {
            out += "error: too many errors\n";
            break;
        }
        ++shown;
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
