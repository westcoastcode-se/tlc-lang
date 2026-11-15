#include "diag/diag.hpp"
#include "diag/memory_limit.hpp"

namespace tlc {

void DiagnosticEngine::error(SourceLoc loc, std::string message) {
    items_.push_back(Diagnostic{loc, std::move(message)});
    if (mirror_) {
        note_memory_diagnostic(line_of(items_.back()));
    }
}

void DiagnosticEngine::truncate(std::size_t count) {
    if (count < items_.size()) {
        items_.resize(count);
    }
    if (mirror_) {
        truncate_memory_notes(count);
    }
}

void DiagnosticEngine::mirror_for_memory_limit() {
    mirror_ = true;
    reset_memory_notes();
    for (const Diagnostic &item : items_) {
        note_memory_diagnostic(line_of(item));
    }
}

std::string DiagnosticEngine::line_of(const Diagnostic &item) const {
    std::string out;
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
    return out;
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
        out += line_of(item);
    }
    return out;
}

} // namespace tlc
