#pragma once

#include "source/source.hpp"

#include <string>
#include <utility>
#include <vector>

namespace tlc {

struct Diagnostic {
    SourceLoc loc;
    std::string message;
};

class DiagnosticEngine {
  public:
    explicit DiagnosticEngine(SourceSet &sources) : sources_(&sources) {}

    void error(SourceLoc loc, std::string message);
    const SourceFile *source(uint32_t id) const { return sources_ == nullptr ? nullptr : sources_->find(id); }
    bool ok() const { return items_.empty(); }
    std::size_t size() const { return items_.size(); }
    // Drops the diagnostics after the first `count`.
    void truncate(std::size_t count);
    // Each later diagnostic is also kept for `--max-memory`, which prints them if it stops the compile.
    // The command line's one engine does this. A comptime run and the language server do not.
    void mirror_for_memory_limit();
    const std::vector<Diagnostic> &items() const { return items_; }
    std::string render() const;
    // The first `limit` diagnostics, then `error: too many errors` when more were reported.
    // The command line prints this. The language server and the tests read every item.
    std::string render(std::size_t limit) const;

  private:
    // `path:line:column: error: message` and a newline, as `render` prints it.
    std::string line_of(const Diagnostic &item) const;

    SourceSet *sources_ = nullptr;
    std::vector<Diagnostic> items_;
    bool mirror_ = false;
};

} // namespace tlc
