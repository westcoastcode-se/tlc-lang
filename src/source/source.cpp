#include <algorithm>
#include "source/source.hpp"

#include <fstream>
#include <sstream>
#include <utility>

namespace tlc {

SourceFile::SourceFile(uint32_t id, std::string path, std::string text)
    : id_(id), path_(std::move(path)), text_(std::move(text)) {
    line_starts_.push_back(0);
    for (uint32_t i = 0; i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            line_starts_.push_back(i + 1);
        }
    }
}

LineCol SourceFile::line_col(uint32_t offset) const {
    if (offset > text_.size()) {
        offset = static_cast<uint32_t>(text_.size());
    }
    // The last line start at or before `offset`. A newline itself belongs to the line it ends.
    const auto after = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    const std::size_t index = static_cast<std::size_t>(after - line_starts_.begin()) - 1;
    return LineCol{static_cast<int>(index) + 1, static_cast<int>(offset - line_starts_[index]) + 1};
}

std::string_view SourceFile::slice(uint32_t offset, uint32_t length) const {
    if (offset > text_.size()) {
        return {};
    }
    const uint32_t available = static_cast<uint32_t>(text_.size()) - offset;
    if (length > available) {
        length = available;
    }
    return std::string_view(text_).substr(offset, length);
}

SourceFile &SourceSet::add(std::string path, std::string text) {
    const uint32_t id = static_cast<uint32_t>(files_.size()) + 1;
    files_.push_back(std::make_unique<SourceFile>(id, std::move(path), std::move(text)));
    return *files_.back();
}

const SourceFile *SourceSet::find(uint32_t id) const {
    if (id == 0 || id > files_.size()) {
        return nullptr;
    }
    return files_[id - 1].get();
}

SourceFile *SourceSet::find(uint32_t id) {
    return const_cast<SourceFile *>(std::as_const(*this).find(id));
}

std::optional<std::string> read_file(const std::string &path) {
    std::ifstream input(path, std::ios::in | std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input && !input.eof()) {
        return std::nullopt;
    }
    return buffer.str();
}

} // namespace tlc
