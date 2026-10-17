#include "harness.hpp"

#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#ifndef TLC_BIN
#error "TLC_BIN must point at the tlc executable"
#endif

namespace {

std::string json_escape(std::string_view text) {
    std::string out;
    for (unsigned char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out.push_back(static_cast<char>(c));
            break;
        }
    }
    return out;
}

bool write_all(int fd, std::string_view bytes) {
    while (!bytes.empty()) {
        const ssize_t wrote = ::write(fd, bytes.data(), bytes.size());
        if (wrote < 0 && errno == EINTR) {
            continue;
        }
        if (wrote <= 0) {
            return false;
        }
        bytes.remove_prefix(static_cast<std::size_t>(wrote));
    }
    return true;
}

void send_json(int fd, const std::string &json) {
    const std::string header = "Content-Length: " + std::to_string(json.size()) + "\r\n\r\n";
    write_all(fd, header);
    write_all(fd, json);
}

// Bytes already pulled from the pipe that belong to a later message.
std::string pending;

// Read one LSP message. Returns an empty string on timeout or a closed pipe.
std::string read_message(int fd, int timeout_ms) {
    std::string buffer = std::move(pending);
    pending.clear();
    const int step = timeout_ms > 50 ? 50 : timeout_ms;
    int left = timeout_ms;
    auto pull = [&]() {
        if (left <= 0) {
            return false;
        }
        pollfd poller{fd, POLLIN, 0};
        const int waited = ::poll(&poller, 1, step);
        if (waited <= 0) {
            left -= step;
            return waited < 0 && errno == EINTR;
        }
        char block[4096];
        const ssize_t n = ::read(fd, block, sizeof block);
        if (n <= 0) {
            left = 0;
            return false;
        }
        buffer.append(block, block + n);
        return true;
    };
    while (buffer.find("\r\n\r\n") == std::string::npos && buffer.find("\n\n") == std::string::npos) {
        if (!pull()) {
            pending = std::move(buffer);
            return {};
        }
    }
    const std::size_t header_end = buffer.find("\r\n\r\n") != std::string::npos ? buffer.find("\r\n\r\n") + 4
                                                                                 : buffer.find("\n\n") + 2;
    const std::string header = buffer.substr(0, header_end);
    std::size_t length = 0;
    const std::size_t key = header.find("Content-Length:");
    if (key == std::string::npos) {
        return {};
    }
    const std::string digits = header.substr(key + std::string("Content-Length:").size());
    const char *begin = digits.data();
    const char *end = digits.data() + digits.size();
    while (begin < end && (*begin == ' ' || *begin == '\t')) {
        ++begin;
    }
    std::from_chars(begin, end, length);
    while (buffer.size() < header_end + length) {
        if (!pull()) {
            pending = std::move(buffer);
            return {};
        }
    }
    if (buffer.size() > header_end + length) {
        pending = buffer.substr(header_end + length);
    }
    return buffer.substr(header_end, length);
}

std::string read_until(int fd, std::string_view needle, int timeout_ms) {
    std::string all;
    const int slice = 200;
    for (int left = timeout_ms; left > 0; left -= slice) {
        const std::string message = read_message(fd, slice);
        if (message.empty()) {
            continue;
        }
        all += message;
        all.push_back('\n');
        if (message.find(needle) != std::string::npos) {
            return all;
        }
    }
    return all;
}

struct LineCol {
    int line = 0;
    int character = 0;
};

std::string request_at(int to_fd, int from_fd, int id, const std::string &method, const std::string &uri, LineCol pos) {
    send_json(to_fd, std::string(R"({"jsonrpc":"2.0","id":)") + std::to_string(id) + R"(,"method":")" + method +
                         R"(","params":{"textDocument":{"uri":")" + uri + R"("},"position":{"line":)" +
                         std::to_string(pos.line) + R"(,"character":)" + std::to_string(pos.character) + "}}}");
    return read_until(from_fd, "\"id\":" + std::to_string(id), 3000);
}

LineCol at_needle(const std::string &text, std::string_view needle, bool after) {
    const std::size_t found = text.find(needle);
    LineCol pos;
    if (found == std::string::npos) {
        pos.line = -1;
        return pos;
    }
    for (std::size_t i = 0; i < found; ++i) {
        if (text[i] == '\n') {
            ++pos.line;
            pos.character = 0;
        } else {
            ++pos.character;
        }
    }
    if (after) {
        pos.character += static_cast<int>(needle.size());
    }
    return pos;
}

} // namespace

int run_lsp_tests() {
    tlc::test::Check check;
    const std::string bin = TLC_BIN;

    char path[] = "/tmp/tlc-lsp-XXXXXX.tlc";
    const int file = ::mkstemps(path, 4);
    EXPECT(check, file >= 0);
    if (file < 0) {
        return check.failed;
    }
    const std::string disk = "func main() i32 {\n    return 1;\n}\n";
    EXPECT(check, ::write(file, disk.data(), disk.size()) == static_cast<ssize_t>(disk.size()));
    ::close(file);

    const std::string source =
        "func add(a i32, b i32) i32 {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "type Point struct {\n"
        "    x i32;\n"
        "};\n"
        "\n"
        "type Box[T type] struct {\n"
        "    value T;\n"
        "};\n"
        "\n"
        "func Point::make(n i32) Point {\n"
        "    var p Point;\n"
        "    p.x = n;\n"
        "    return p;\n"
        "}\n"
        "\n"
        "func Point::sum(self this) i32 {\n"
        "    return self.x;\n"
        "}\n"
        "\n"
        "type Shape trait {\n"
        "    w i32;\n"
        "    func area() i32;\n"
        "    func grow(n i32) void;\n"
        "};\n"
        "\n"
        "func measure(s *Shape) i32 {\n"
        "    return s.area();\n"
        "}\n"
        "\n"
        "type Inner struct {\n"
        "    inner_value i32;\n"
        "};\n"
        "\n"
        "type Box struct {\n"
        "    value i32;\n"
        "    complex Inner;\n"
        "};\n"
        "\n"
        "type K struct {\n"
        "    value i32;\n"
        "};\n"
        "\n"
        "type Row []i32;\n"
        "\n"
        "func Row::width(self this) i32 {\n"
        "    return self.len;\n"
        "}\n"
        "\n"
        "func main() i32 {\n"
        "    var p Point;\n"
        "    var slice []Point;\n"
        "    part := slice[0:0];\n"
        "    var extra = part.len;\n"
        "    var named Row;\n"
        "    var built = Point.make(1);\n"
        "    q := Point.make(2);\n"
        "    r := Point{};\n"
        "    var n i32 = \"no\";\n"
        "    var kk K = {.value = 10};\n"
        "    var box Box = {.complex = {.inner_value = 1}};\n"
        "    kk = {.value = 2};\n"
        "    var kept i32 = 1;\n"
        "    var raw = `}`;\n"
        "    var tail = kept;\n"
        "    func inner() i32 {\n"
        "        return 1;\n"
        "    }\n"
        "    {\n"
        "        var hidden i32 = 1;\n"
        "    }\n"
        "    var seen = inner;\n"
        "    var gone = hidden;\n"
        "    var made *Point = new Point{.x = 1};\n"
        "    var boxed *Box[i32] = new Box[i32]{.value = 1};\n"
        "    var row = new [_]Point{{.x = 1}};\n"
        "    var verts = [_]f32{-0.6f, -0.5f, 0.95f};\n"
        "    var count = verts.len;\n"
        "    var copied = verts;\n"
        "    var grid = [_][2]i32{{1, 2}, {3, 4}};\n"
        "    var ptrs = [_]*i32{&kept};\n"
        "    var ops = [_]func (i32) i32{add};\n"
        "    return add(p.x, q.x) + r.x + slice.len + named.len + tail;\n"
        "}\n";

    int to_child[2] = {-1, -1};
    int from_child[2] = {-1, -1};
    EXPECT(check, ::pipe(to_child) == 0);
    EXPECT(check, ::pipe(from_child) == 0);
    const pid_t pid = ::fork();
    EXPECT(check, pid >= 0);
    if (pid == 0) {
        ::dup2(to_child[0], STDIN_FILENO);
        ::dup2(from_child[1], STDOUT_FILENO);
        ::close(to_child[0]);
        ::close(to_child[1]);
        ::close(from_child[0]);
        ::close(from_child[1]);
        ::execl(bin.c_str(), bin.c_str(), "lsp", nullptr);
        _exit(127);
    }
    ::close(to_child[0]);
    ::close(from_child[1]);

    const std::string uri = std::string("file://") + path;
    send_json(to_child[1],
              R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"processId":null,"capabilities":{},"rootUri":null}})");
    const std::string hello = read_message(from_child[0], 3000);
    EXPECT_HAS(check, hello, "completionProvider");
    EXPECT_HAS(check, hello, "tlc");

    send_json(to_child[1], R"({"jsonrpc":"2.0","method":"initialized","params":{}})");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") + uri +
                  R"(","languageId":"tlc","version":1,"text":")" + json_escape(source) + R"("}}})");
    const std::string opened = read_until(from_child[0], "type mismatch", 4000);
    EXPECT_HAS(check, opened, "type mismatch");
    EXPECT_HAS(check, opened, uri);

    const LineCol member = at_needle(source, "p.", true);
    EXPECT(check, member.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":2,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(member.line) + R"(,"character":)" +
                  std::to_string(member.character) + "}}}");
    const std::string members = read_until(from_child[0], "\"id\":2", 3000);
    EXPECT_HAS(check, members, "\"x\"");
    EXPECT_HAS(check, members, "\"sum\"");
    EXPECT(check, members.find("\"make\"") == std::string::npos);

    const LineCol slice_at = at_needle(source, "slice.", true);
    EXPECT(check, slice_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":17,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(slice_at.line) + R"(,"character":)" +
                  std::to_string(slice_at.character) + "}}}");
    const std::string slice_items = read_until(from_child[0], "\"id\":17", 3000);
    EXPECT_HAS(check, slice_items, "\"len\"");
    EXPECT(check, slice_items.find("\"x\"") == std::string::npos);
    EXPECT(check, slice_items.find("\"sum\"") == std::string::npos);
    EXPECT(check, slice_items.find("\"width\"") == std::string::npos);

    const LineCol part_at = at_needle(source, "part.", true);
    EXPECT(check, part_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":19,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(part_at.line) + R"(,"character":)" +
                  std::to_string(part_at.character) + "}}}");
    const std::string part_items = read_until(from_child[0], "\"id\":19", 3000);
    EXPECT_HAS(check, part_items, "\"len\"");
    EXPECT(check, part_items.find("\"x\"") == std::string::npos);

    const LineCol alias_len = at_needle(source, "named.", true);
    EXPECT(check, alias_len.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":18,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(alias_len.line) + R"(,"character":)" +
                  std::to_string(alias_len.character) + "}}}");
    const std::string alias_items = read_until(from_child[0], "\"id\":18", 3000);
    EXPECT_HAS(check, alias_items, "\"len\"");
    EXPECT_HAS(check, alias_items, "\"width\"");

    const LineCol type_call = at_needle(source, "Point.", true);
    EXPECT(check, type_call.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":10,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(type_call.line) + R"(,"character":)" +
                  std::to_string(type_call.character) + "}}}");
    const std::string type_items = read_until(from_child[0], "\"id\":10", 3000);
    EXPECT_HAS(check, type_items, "\"make\"");
    EXPECT(check, type_items.find("\"sum\"") == std::string::npos);

    const LineCol inferred = at_needle(source, "q.", true);
    EXPECT(check, inferred.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":12,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(inferred.line) + R"(,"character":)" +
                  std::to_string(inferred.character) + "}}}");
    const std::string inferred_items = read_until(from_child[0], "\"id\":12", 3000);
    EXPECT_HAS(check, inferred_items, "\"x\"");
    EXPECT_HAS(check, inferred_items, "\"sum\"");
    EXPECT(check, inferred_items.find("\"make\"") == std::string::npos);

    const LineCol literal = at_needle(source, "r.", true);
    EXPECT(check, literal.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":14,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(literal.line) + R"(,"character":)" +
                  std::to_string(literal.character) + "}}}");
    const std::string literal_items = read_until(from_child[0], "\"id\":14", 3000);
    EXPECT_HAS(check, literal_items, "\"x\"");
    EXPECT_HAS(check, literal_items, "\"sum\"");

    const LineCol trait_call = at_needle(source, "s.", true);
    EXPECT(check, trait_call.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":13,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(trait_call.line) + R"(,"character":)" +
                  std::to_string(trait_call.character) + "}}}");
    const std::string trait_items = read_until(from_child[0], "\"id\":13", 3000);
    EXPECT_HAS(check, trait_items, "\"area\"");
    EXPECT_HAS(check, trait_items, "\"grow\"");
    EXPECT_HAS(check, trait_items, "\"w\"");

    const LineCol init_field = at_needle(source, "var kk K = {.", true);
    EXPECT(check, init_field.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":7,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(init_field.line) + R"(,"character":)" +
                  std::to_string(init_field.character) + "}}}");
    const std::string init_items = read_until(from_child[0], "\"id\":7", 3000);
    EXPECT_HAS(check, init_items, "\"value\"");

    const LineCol nested_field = at_needle(source, "{.complex = {.", true);
    EXPECT(check, nested_field.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":8,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(nested_field.line) + R"(,"character":)" +
                  std::to_string(nested_field.character) + "}}}");
    const std::string nested_items = read_until(from_child[0], "\"id\":8", 3000);
    EXPECT_HAS(check, nested_items, "\"inner_value\"");

    const LineCol assigned = at_needle(source, "kk = {.", true);
    EXPECT(check, assigned.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":9,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(assigned.line) + R"(,"character":)" +
                  std::to_string(assigned.character) + "}}}");
    const std::string assigned_items = read_until(from_child[0], "\"id\":9", 3000);
    EXPECT_HAS(check, assigned_items, "\"value\"");

    const LineCol call = at_needle(source, "return add", true);
    EXPECT(check, call.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":3,"method":"textDocument/definition","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(call.line) + R"(,"character":)" +
                  std::to_string(call.character) + "}}}");
    const std::string defined = read_until(from_child[0], "\"id\":3", 3000);
    EXPECT_HAS(check, defined, "\"line\":0");
    EXPECT_HAS(check, defined, uri);

    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":4,"method":"textDocument/hover","params":{"textDocument":{"uri":")") + uri +
                  R"("},"position":{"line":)" + std::to_string(call.line) + R"(,"character":)" +
                  std::to_string(call.character) + "}}}");
    const std::string hovered = read_until(from_child[0], "\"id\":4", 3000);
    EXPECT_HAS(check, hovered, "func add(a i32, b i32) i32");

    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":")") +
                  uri + "\"}}}");
    const std::string symbols = read_until(from_child[0], "\"id\":5", 3000);
    EXPECT_HAS(check, symbols, "Point");
    EXPECT_HAS(check, symbols, "main");

    const std::filesystem::path goto_root = "/tmp/tlc-goto-def";
    std::filesystem::remove_all(goto_root);
    std::filesystem::create_directories(goto_root / "math");
    {
        std::ofstream math(goto_root / "math" / "add.tlc");
        math << "export func add(a i32, b i32) i32 {\n    return a + b;\n}\n";
        std::ofstream entry(goto_root / "main.tlc");
        entry << "import { add } from \"math\";\nfunc main() i32 {\n    return add(1, 2);\n}\n";
    }
    const std::string goto_source = "import { add } from \"math\";\nfunc main() i32 {\n    return add(1, 2);\n}\n";
    const std::string goto_uri = std::string("file://") + (goto_root / "main.tlc").string();
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  goto_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(goto_source) + R"("}}})");
    const LineCol imported = at_needle(goto_source, "return add", true);
    EXPECT(check, imported.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":11,"method":"textDocument/definition","params":{"textDocument":{"uri":")") +
                  goto_uri + R"("},"position":{"line":)" + std::to_string(imported.line) + R"(,"character":)" +
                  std::to_string(imported.character) + "}}}");
    const std::string jumped = read_until(from_child[0], "\"id\":11", 4000);
    EXPECT_HAS(check, jumped, "math/add.tlc");
    EXPECT_HAS(check, jumped, "\"line\":0");

    const std::filesystem::path export_root = "/tmp/tlc-export-lsp";
    std::filesystem::remove_all(export_root);
    std::filesystem::create_directories(export_root / "math");
    {
        std::ofstream math(export_root / "math" / "lib.tlc");
        math << "package math;\nexport type Entity struct { id i32; };\nexport var Entity::next i32 = 1;\n"
                "var Entity::hidden i32 = 2;\nexport func Entity::make() i32 { return 1; }\n"
                "func Entity::buried() i32 { return 2; }\n"
                "export func Entity::total(self this) i32 { return self.id; }\n"
                "export type Color enum { Red };\n";
        std::ofstream entry(export_root / "main.tlc");
        entry << "import \"math\";\nfunc main() i32 {\n    return math.Entity.next + math.Entity.make();\n}\n";
    }
    const std::string export_source =
        "import \"math\";\nfunc main() i32 {\n    return math.Entity.next + math.Entity.make();\n}\n";
    const std::string export_uri = std::string("file://") + (export_root / "main.tlc").string();
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  export_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(export_source) + R"("}}})");
    const LineCol exported_var = at_needle(export_source, "math.Entity.", true);
    EXPECT(check, exported_var.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":15,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  export_uri + R"("},"position":{"line":)" + std::to_string(exported_var.line) + R"(,"character":)" +
                  std::to_string(exported_var.character) + "}}}");
    const std::string export_items = read_until(from_child[0], "\"id\":15", 4000);
    EXPECT_HAS(check, export_items, "\"next\"");
    EXPECT_HAS(check, export_items, "\"make\"");
    EXPECT(check, export_items.find("\"hidden\"") == std::string::npos);
    EXPECT(check, export_items.find("\"buried\"") == std::string::npos);
    EXPECT(check, export_items.find("\"total\"") == std::string::npos);
    const LineCol make_at = at_needle(export_source, "make", false);
    EXPECT(check, make_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":25,"method":"textDocument/hover","params":{"textDocument":{"uri":")") +
                  export_uri + R"("},"position":{"line":)" + std::to_string(make_at.line) + R"(,"character":)" +
                  std::to_string(make_at.character) + "}}}");
    const std::string make_hover = read_until(from_child[0], "\"id\":25", 4000);
    EXPECT_HAS(check, make_hover, "func Entity.make() i32");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":16,"method":"textDocument/definition","params":{"textDocument":{"uri":")") +
                  export_uri + R"("},"position":{"line":)" + std::to_string(exported_var.line) + R"(,"character":)" +
                  std::to_string(exported_var.character) + "}}}");
    const std::string export_def = read_until(from_child[0], "\"id\":16", 4000);
    EXPECT_HAS(check, export_def, "math/lib.tlc");

    const LineCol kept_at = at_needle(source, "tail = kept", true);
    EXPECT(check, kept_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":20,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(kept_at.line) + R"(,"character":)" +
                  std::to_string(kept_at.character) + "}}}");
    const std::string kept_items = read_until(from_child[0], "\"id\":20", 3000);
    EXPECT_HAS(check, kept_items, "\"kept\"");

    const LineCol raw_at = at_needle(source, "var raw = `", true);
    EXPECT(check, raw_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":21,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(raw_at.line) + R"(,"character":)" +
                  std::to_string(raw_at.character) + "}}}");
    const std::string raw_items = read_until(from_child[0], "\"id\":21", 3000);
    EXPECT(check, raw_items.find("\"func\"") == std::string::npos);

    const LineCol inner_at = at_needle(source, "seen = inner", true);
    EXPECT(check, inner_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":22,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(inner_at.line) + R"(,"character":)" +
                  std::to_string(inner_at.character) + "}}}");
    const std::string inner_items = read_until(from_child[0], "\"id\":22", 3000);
    EXPECT_HAS(check, inner_items, "\"inner\"");

    const LineCol hidden_at = at_needle(source, "gone = hidden", true);
    EXPECT(check, hidden_at.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":23,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(hidden_at.line) + R"(,"character":)" +
                  std::to_string(hidden_at.character) + "}}}");
    const std::string hidden_items = read_until(from_child[0], "\"id\":23", 3000);
    EXPECT(check, hidden_items.find("\"hidden\"") == std::string::npos);

    const LineCol new_field = at_needle(source, "new Point{.", true);
    EXPECT(check, new_field.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":24,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(new_field.line) + R"(,"character":)" +
                  std::to_string(new_field.character) + "}}}");
    const std::string new_items = read_until(from_child[0], "\"id\":24", 3000);
    EXPECT_HAS(check, new_items, "\"x\"");
    EXPECT(check, new_items.find("\"sum\"") == std::string::npos);

    const LineCol box_field = at_needle(source, "new Box[i32]{.", true);
    EXPECT(check, box_field.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":25,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(box_field.line) + R"(,"character":)" +
                  std::to_string(box_field.character) + "}}}");
    const std::string box_items = read_until(from_child[0], "\"id\":25", 3000);
    EXPECT_HAS(check, box_items, "\"value\"");

    const LineCol row_field = at_needle(source, "[_]Point{{.", true);
    EXPECT(check, row_field.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":26,"method":"textDocument/completion","params":{"textDocument":{"uri":")") +
                  uri + R"("},"position":{"line":)" + std::to_string(row_field.line) + R"(,"character":)" +
                  std::to_string(row_field.character) + "}}}");
    const std::string row_items = read_until(from_child[0], "\"id\":26", 3000);
    EXPECT_HAS(check, row_items, "\"x\"");

    const LineCol verts_at = at_needle(source, "verts", false);
    EXPECT(check, verts_at.line >= 0);
    const std::string verts_hover = request_at(to_child[1], from_child[0], 27, "textDocument/hover", uri, verts_at);
    EXPECT_HAS(check, verts_hover, "verts [3]f32");

    const LineCol copied_at = at_needle(source, "copied", false);
    EXPECT(check, copied_at.line >= 0);
    const std::string copied_hover = request_at(to_child[1], from_child[0], 28, "textDocument/hover", uri, copied_at);
    EXPECT_HAS(check, copied_hover, "copied [3]f32");

    const LineCol grid_at = at_needle(source, "grid", false);
    EXPECT(check, grid_at.line >= 0);
    const std::string grid_hover = request_at(to_child[1], from_child[0], 29, "textDocument/hover", uri, grid_at);
    EXPECT_HAS(check, grid_hover, "grid [2][2]i32");

    const LineCol ptrs_at = at_needle(source, "ptrs", false);
    EXPECT(check, ptrs_at.line >= 0);
    const std::string ptrs_hover = request_at(to_child[1], from_child[0], 30, "textDocument/hover", uri, ptrs_at);
    EXPECT_HAS(check, ptrs_hover, "ptrs [1]*i32");

    const LineCol ops_at = at_needle(source, "ops", false);
    EXPECT(check, ops_at.line >= 0);
    const std::string ops_hover = request_at(to_child[1], from_child[0], 31, "textDocument/hover", uri, ops_at);
    EXPECT_HAS(check, ops_hover, "ops [1]func (i32) i32");

    const LineCol verts_dot = at_needle(source, "verts.", true);
    EXPECT(check, verts_dot.line >= 0);
    const std::string verts_len = request_at(to_child[1], from_child[0], 32, "textDocument/completion", uri, verts_dot);
    EXPECT_HAS(check, verts_len, "\"len\"");

    const LineCol count_at = at_needle(source, "count", false);
    EXPECT(check, count_at.line >= 0);
    const std::string count_hover = request_at(to_child[1], from_child[0], 33, "textDocument/hover", uri, count_at);
    EXPECT_HAS(check, count_hover, "count i32");

    const LineCol verts_len_at = at_needle(source, "verts.", true);
    EXPECT(check, verts_len_at.line >= 0);
    const std::string verts_len_hover = request_at(to_child[1], from_child[0], 34, "textDocument/hover", uri, verts_len_at);
    EXPECT_HAS(check, verts_len_hover, "len i32");

    const LineCol slice_len_at = at_needle(source, "slice.", true);
    EXPECT(check, slice_len_at.line >= 0);
    const std::string slice_len_hover = request_at(to_child[1], from_child[0], 35, "textDocument/hover", uri, slice_len_at);
    EXPECT_HAS(check, slice_len_hover, "len i32");

    const LineCol named_len_at = at_needle(source, "named.", true);
    EXPECT(check, named_len_at.line >= 0);
    const std::string named_len_hover = request_at(to_child[1], from_child[0], 36, "textDocument/hover", uri, named_len_at);
    EXPECT_HAS(check, named_len_hover, "len i32");

    const std::string over_source =
        "func once(n i32) i32;\n"
        "\n"
        "func once(n i32) i32 {\n"
        "    return n;\n"
        "}\n"
        "\n"
        "func zero() i32 {\n"
        "    return 1;\n"
        "}\n"
        "\n"
        "func zero(n i32) i32 {\n"
        "    return n;\n"
        "}\n"
        "\n"
        "func twin(n i32) i32 {\n"
        "    return n;\n"
        "}\n"
        "\n"
        "func twin(text *const u8) i32 {\n"
        "    return 2;\n"
        "}\n"
        "\n"
        "type Point struct { x i32; };\n"
        "\n"
        "func Point::draw(self *this) i32 {\n"
        "    return 1;\n"
        "}\n"
        "\n"
        "func Point::draw(self *this, n i32) i32 {\n"
        "    return n;\n"
        "}\n"
        "\n"
        "func main() i32 {\n"
        "    func local() i32 {\n"
        "        return 1;\n"
        "    }\n"
        "    func local(n i32) i32 {\n"
        "        return n;\n"
        "    }\n"
        "    var p Point;\n"
        "    var text *const u8 = *const u8(0);\n"
        "    return once(1) + zero() + zero(1) + twin(1) + twin(text) + p.draw() + p.draw(1) + local() + local(1);\n"
        "}\n";
    const std::string over_uri = "file:///tmp/tlc-overload-lsp.tlc";
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  over_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(over_source) + R"("}}})");

    const LineCol once_call = at_needle(over_source, "once(1)", false);
    const std::string once_def = request_at(to_child[1], from_child[0], 40, "textDocument/definition", over_uri, once_call);
    EXPECT_HAS(check, once_def, "\"line\":2,");
    EXPECT(check, once_def.find("\"line\":0,") == std::string::npos);

    const LineCol zero_call = at_needle(over_source, "zero() +", false);
    const std::string zero_def = request_at(to_child[1], from_child[0], 41, "textDocument/definition", over_uri, zero_call);
    EXPECT_HAS(check, zero_def, "\"line\":6,");
    const std::string zero_hover = request_at(to_child[1], from_child[0], 42, "textDocument/hover", over_uri, zero_call);
    EXPECT_HAS(check, zero_hover, "func zero() i32");

    const LineCol zero_n = at_needle(over_source, "zero(1)", false);
    const std::string zero_n_def = request_at(to_child[1], from_child[0], 43, "textDocument/definition", over_uri, zero_n);
    EXPECT_HAS(check, zero_n_def, "\"line\":10,");
    const std::string zero_n_hover = request_at(to_child[1], from_child[0], 44, "textDocument/hover", over_uri, zero_n);
    EXPECT_HAS(check, zero_n_hover, "func zero(n i32) i32");

    const LineCol twin_decl = at_needle(over_source, "twin(n", false);
    const std::string twin_decl_def =
        request_at(to_child[1], from_child[0], 45, "textDocument/definition", over_uri, twin_decl);
    EXPECT_HAS(check, twin_decl_def, "\"line\":14,");
    EXPECT(check, twin_decl_def.find("\"line\":18,") == std::string::npos);

    const LineCol twin_call = at_needle(over_source, "twin(1)", false);
    const std::string twin_def = request_at(to_child[1], from_child[0], 46, "textDocument/definition", over_uri, twin_call);
    EXPECT_HAS(check, twin_def, "\"result\":null");
    const std::string twin_hover = request_at(to_child[1], from_child[0], 47, "textDocument/hover", over_uri, twin_call);
    EXPECT_HAS(check, twin_hover, "\"result\":null");

    const LineCol draw_call = at_needle(over_source, "draw()", false);
    const std::string draw_def = request_at(to_child[1], from_child[0], 48, "textDocument/definition", over_uri, draw_call);
    EXPECT_HAS(check, draw_def, "\"line\":24,");
    const std::string draw_hover = request_at(to_child[1], from_child[0], 49, "textDocument/hover", over_uri, draw_call);
    EXPECT_HAS(check, draw_hover, "func Point.draw(self *Point) i32");

    const LineCol draw_n = at_needle(over_source, "draw(1)", false);
    const std::string draw_n_def = request_at(to_child[1], from_child[0], 50, "textDocument/definition", over_uri, draw_n);
    EXPECT_HAS(check, draw_n_def, "\"line\":28,");
    const std::string draw_n_hover = request_at(to_child[1], from_child[0], 51, "textDocument/hover", over_uri, draw_n);
    EXPECT_HAS(check, draw_n_hover, "func Point.draw(self *Point, n i32) i32");

    const LineCol local_call = at_needle(over_source, "local() +", false);
    const std::string local_def = request_at(to_child[1], from_child[0], 52, "textDocument/definition", over_uri, local_call);
    EXPECT_HAS(check, local_def, "\"line\":33,");

    const LineCol local_n = at_needle(over_source, "local(1)", false);
    const std::string local_n_def = request_at(to_child[1], from_child[0], 53, "textDocument/definition", over_uri, local_n);
    EXPECT_HAS(check, local_n_def, "\"line\":36,");

    send_json(to_child[1], R"({"jsonrpc":"2.0","id":6,"method":"shutdown","params":null})");
    const std::string stopped = read_until(from_child[0], "\"id\":6", 3000);
    EXPECT_HAS(check, stopped, "\"result\":null");
    send_json(to_child[1], R"({"jsonrpc":"2.0","method":"exit"})");

    ::close(to_child[1]);
    int status = 0;
    pid_t waited = 0;
    for (int n = 0; n < 50 && waited == 0; ++n) {
        waited = ::waitpid(pid, &status, WNOHANG);
        if (waited == 0) {
            ::usleep(100000);
        }
    }
    if (waited == 0) {
        ::kill(pid, SIGKILL);
        waited = ::waitpid(pid, &status, 0);
    }
    EXPECT(check, waited == pid);
    EXPECT(check, WIFEXITED(status) && WEXITSTATUS(status) == 0);
    ::close(from_child[0]);
    ::unlink(path);
    return check.failed;
}
