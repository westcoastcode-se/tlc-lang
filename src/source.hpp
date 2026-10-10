#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tlc {

struct SourceLoc {
    uint32_t file = 0;
    uint32_t offset = 0;

    bool valid() const { return file != 0; }
};

struct LineCol {
    int line = 1;
    int column = 1;
};

class SourceFile {
  public:
    SourceFile(uint32_t id, std::string path, std::string text);

    uint32_t id() const { return id_; }
    const std::string &path() const { return path_; }
    const std::string &text() const { return text_; }

    LineCol line_col(uint32_t offset) const;
    std::string_view slice(uint32_t offset, uint32_t length) const;

  private:
    uint32_t id_ = 0;
    std::string path_;
    std::string text_;
};

class SourceSet {
  public:
    SourceFile &add(std::string path, std::string text);
    const SourceFile *find(uint32_t id) const;
    SourceFile *find(uint32_t id);

  private:
    std::vector<std::unique_ptr<SourceFile>> files_;
};

std::optional<std::string> read_file(const std::string &path);

} // namespace tlc
