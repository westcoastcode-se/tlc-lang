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

// `id_needle` is `"id":110`. The returned text is `name type modifiers` per token.
// A missing payload returns the raw messages so the failure shows the server reply.
std::string semantic_summary(const std::string &message, const std::string &source, std::string_view id_needle) {
    const std::size_t id_at = message.find(id_needle);
    if (id_at == std::string::npos) {
        return message;
    }
    const std::string key = "\"data\":[";
    const std::size_t data_at = message.find(key, id_at);
    if (data_at == std::string::npos) {
        return message;
    }
    std::size_t i = data_at + key.size();
    std::vector<int> nums;
    while (i < message.size() && message[i] != ']') {
        if (message[i] == ',') {
            ++i;
            continue;
        }
        int value = 0;
        const std::from_chars_result parsed = std::from_chars(message.data() + i, message.data() + message.size(), value);
        if (parsed.ec != std::errc{}) {
            return message;
        }
        nums.push_back(value);
        i = static_cast<std::size_t>(parsed.ptr - message.data());
    }
    std::string out;
    int line = 0;
    int character = 0;
    for (std::size_t n = 0; n + 4 < nums.size(); n += 5) {
        line += nums[n];
        if (nums[n] == 0) {
            character += nums[n + 1];
        } else {
            character = nums[n + 1];
        }
        int row = 0;
        std::size_t at = 0;
        while (at < source.size() && row < line) {
            if (source[at] == '\n') {
                ++row;
            }
            ++at;
        }
        std::string text = "?";
        if (row == line && character >= 0 &&
            at + static_cast<std::size_t>(character) + static_cast<std::size_t>(nums[n + 2]) <= source.size()) {
            text = source.substr(at + static_cast<std::size_t>(character), static_cast<std::size_t>(nums[n + 2]));
        }
        out += text;
        out.push_back(' ');
        out += std::to_string(nums[n + 3]);
        out.push_back(' ');
        out += std::to_string(nums[n + 4]);
        out.push_back('\n');
    }
    return out;
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
        "func Point::copy(self this) Point {\n"
        "    return self;\n"
        "}\n"
        "\n"
        "func points() []Point {\n"
        "    var out []Point;\n"
        "    return out;\n"
        "}\n"
        "\n"
        "func maybe() ?*Point {\n"
        "    return null;\n"
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
        "    var cells = [_]i32{1, 2, 3, 4};\n"
        "    var span *[4]i32 = &cells;\n"
        "    var span_n = span.len;\n"
        "    var bare *i32 = &kept;\n"
        "    var bare_n = bare.len;\n"
        "    var optp ?*[4]i32 = null;\n"
        "    var optp_n = optp.len;\n"
        "    var crowd [1]Point;\n"
        "    var headed *[1]Point = &crowd;\n"
        "    var opt ?*Point = null;\n"
        "    var shown = Point.make(1).x;\n"
        "    var copied_p = p.copy().x;\n"
        "    var nlen = points().len;\n"
        "    var cell = slice[0].x;\n"
        "    var at = crowd[0].x;\n"
        "    var paren = (p).x;\n"
        "    var wrapped = (Point.make(1)).x;\n"
        "    var ranged = slice[0:0].len;\n"
        "    var bad_opt = opt[0].x;\n"
        "    var kept_paren = (kept).x;\n"
        "    var summed = p.sum().x;\n"
        "    var span_cell = span[0].x;\n"
        "    var grid_row = grid[0].len;\n"
        "    var head_cell = headed[0].x;\n"
        "    var maybe_cell = maybe()[0].x;\n"
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
    EXPECT_HAS(check, hello, "semanticTokensProvider");
    EXPECT_HAS(check, hello,
               "\"tokenTypes\":[\"namespace\",\"type\",\"struct\",\"enum\",\"interface\",\"typeParameter\",\"variable\","
               "\"property\",\"enumMember\",\"function\",\"method\"]");
    EXPECT_HAS(check, hello, "\"tokenModifiers\":[\"declaration\",\"readonly\",\"static\"]");
    EXPECT_HAS(check, hello, "\"range\":true");
    EXPECT_HAS(check, hello, "\"full\":true");
    EXPECT(check, hello.find("delta") == std::string::npos);

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

    const LineCol span_dot = at_needle(source, "span.", true);
    EXPECT(check, span_dot.line >= 0);
    const std::string span_len = request_at(to_child[1], from_child[0], 37, "textDocument/completion", uri, span_dot);
    EXPECT_HAS(check, span_len, "\"len\"");

    const LineCol span_len_at = at_needle(source, "span.", true);
    EXPECT(check, span_len_at.line >= 0);
    const std::string span_len_hover = request_at(to_child[1], from_child[0], 38, "textDocument/hover", uri, span_len_at);
    EXPECT_HAS(check, span_len_hover, "len i32");

    const LineCol bare_dot = at_needle(source, "bare.", true);
    EXPECT(check, bare_dot.line >= 0);
    const std::string bare_items = request_at(to_child[1], from_child[0], 39, "textDocument/completion", uri, bare_dot);
    EXPECT(check, bare_items.find("\"len\"") == std::string::npos);

    const LineCol optp_dot = at_needle(source, "optp.", true);
    EXPECT(check, optp_dot.line >= 0);
    const std::string optp_items = request_at(to_child[1], from_child[0], 40, "textDocument/completion", uri, optp_dot);
    EXPECT(check, optp_items.find("\"len\"") == std::string::npos);

    const LineCol made_dot = at_needle(source, "Point.make(1).", true);
    EXPECT(check, made_dot.line >= 0);
    const std::string made_items = request_at(to_child[1], from_child[0], 90, "textDocument/completion", uri, made_dot);
    EXPECT_HAS(check, made_items, "\"x\"");
    EXPECT_HAS(check, made_items, "\"sum\"");
    EXPECT(check, made_items.find("\"make\"") == std::string::npos);

    const LineCol copy_dot = at_needle(source, "p.copy().", true);
    EXPECT(check, copy_dot.line >= 0);
    const std::string copy_items = request_at(to_child[1], from_child[0], 91, "textDocument/completion", uri, copy_dot);
    EXPECT_HAS(check, copy_items, "\"x\"");
    EXPECT_HAS(check, copy_items, "\"sum\"");
    EXPECT(check, copy_items.find("\"make\"") == std::string::npos);

    const LineCol points_dot = at_needle(source, "points().", true);
    EXPECT(check, points_dot.line >= 0);
    const std::string points_items = request_at(to_child[1], from_child[0], 92, "textDocument/completion", uri, points_dot);
    EXPECT_HAS(check, points_items, "\"len\"");
    EXPECT(check, points_items.find("\"x\"") == std::string::npos);

    const LineCol slice_cell = at_needle(source, "slice[0].", true);
    EXPECT(check, slice_cell.line >= 0);
    const std::string slice_cell_items = request_at(to_child[1], from_child[0], 93, "textDocument/completion", uri, slice_cell);
    EXPECT_HAS(check, slice_cell_items, "\"x\"");
    EXPECT_HAS(check, slice_cell_items, "\"sum\"");
    EXPECT(check, slice_cell_items.find("\"len\"") == std::string::npos);

    const LineCol crowd_dot = at_needle(source, "crowd[0].", true);
    EXPECT(check, crowd_dot.line >= 0);
    const std::string crowd_items = request_at(to_child[1], from_child[0], 94, "textDocument/completion", uri, crowd_dot);
    EXPECT_HAS(check, crowd_items, "\"x\"");
    EXPECT(check, crowd_items.find("\"len\"") == std::string::npos);

    const LineCol paren_dot = at_needle(source, "(p).", true);
    EXPECT(check, paren_dot.line >= 0);
    const std::string paren_items = request_at(to_child[1], from_child[0], 95, "textDocument/completion", uri, paren_dot);
    EXPECT_HAS(check, paren_items, "\"x\"");
    EXPECT_HAS(check, paren_items, "\"sum\"");
    EXPECT(check, paren_items.find("\"make\"") == std::string::npos);

    const LineCol wrapped_dot = at_needle(source, "(Point.make(1)).", true);
    EXPECT(check, wrapped_dot.line >= 0);
    const std::string wrapped_items = request_at(to_child[1], from_child[0], 96, "textDocument/completion", uri, wrapped_dot);
    EXPECT_HAS(check, wrapped_items, "\"x\"");
    EXPECT_HAS(check, wrapped_items, "\"sum\"");

    const LineCol range_dot = at_needle(source, "slice[0:0].", true);
    EXPECT(check, range_dot.line >= 0);
    const std::string range_items = request_at(to_child[1], from_child[0], 97, "textDocument/completion", uri, range_dot);
    EXPECT_HAS(check, range_items, "\"len\"");
    EXPECT(check, range_items.find("\"x\"") == std::string::npos);

    const LineCol opt_dot = at_needle(source, "opt[0].", true);
    EXPECT(check, opt_dot.line >= 0);
    const std::string opt_items = request_at(to_child[1], from_child[0], 98, "textDocument/completion", uri, opt_dot);
    EXPECT(check, opt_items.find("\"x\"") == std::string::npos);
    EXPECT(check, opt_items.find("\"func\"") == std::string::npos);

    const LineCol kept_dot = at_needle(source, "(kept).", true);
    EXPECT(check, kept_dot.line >= 0);
    const std::string kept_dot_items = request_at(to_child[1], from_child[0], 99, "textDocument/completion", uri, kept_dot);
    EXPECT(check, kept_dot_items.find("\"x\"") == std::string::npos);
    EXPECT(check, kept_dot_items.find("\"func\"") == std::string::npos);

    const LineCol sum_dot = at_needle(source, "p.sum().", true);
    EXPECT(check, sum_dot.line >= 0);
    const std::string sum_items = request_at(to_child[1], from_child[0], 100, "textDocument/completion", uri, sum_dot);
    EXPECT(check, sum_items.find("\"x\"") == std::string::npos);
    EXPECT(check, sum_items.find("\"func\"") == std::string::npos);

    const LineCol span_cell = at_needle(source, "span[0].", true);
    EXPECT(check, span_cell.line >= 0);
    const std::string span_cell_items = request_at(to_child[1], from_child[0], 101, "textDocument/completion", uri, span_cell);
    EXPECT(check, span_cell_items.find("\"len\"") == std::string::npos);

    const LineCol grid_dot = at_needle(source, "grid[0].", true);
    EXPECT(check, grid_dot.line >= 0);
    const std::string grid_items = request_at(to_child[1], from_child[0], 102, "textDocument/completion", uri, grid_dot);
    EXPECT_HAS(check, grid_items, "\"len\"");
    EXPECT(check, grid_items.find("\"x\"") == std::string::npos);

    const LineCol headed_dot = at_needle(source, "headed[0].", true);
    EXPECT(check, headed_dot.line >= 0);
    const std::string headed_items = request_at(to_child[1], from_child[0], 103, "textDocument/completion", uri, headed_dot);
    EXPECT_HAS(check, headed_items, "\"x\"");
    EXPECT_HAS(check, headed_items, "\"sum\"");
    EXPECT(check, headed_items.find("\"len\"") == std::string::npos);
    EXPECT(check, headed_items.find("\"make\"") == std::string::npos);

    const LineCol maybe_dot = at_needle(source, "maybe()[0].", true);
    EXPECT(check, maybe_dot.line >= 0);
    const std::string maybe_items = request_at(to_child[1], from_child[0], 104, "textDocument/completion", uri, maybe_dot);
    EXPECT(check, maybe_items.find("\"x\"") == std::string::npos);
    EXPECT(check, maybe_items.find("\"func\"") == std::string::npos);

    LineCol shown_x = at_needle(source, "Point.make(1).x", true);
    EXPECT(check, shown_x.line >= 0);
    shown_x.character -= 1;
    const std::string shown_hover = request_at(to_child[1], from_child[0], 105, "textDocument/hover", uri, shown_x);
    EXPECT_HAS(check, shown_hover, "x i32");

    const std::string points_hover = request_at(to_child[1], from_child[0], 106, "textDocument/hover", uri, points_dot);
    EXPECT_HAS(check, points_hover, "len i32");

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

    const std::string each_source =
        "type Bag struct { n i32; };\n"
        "func main() i32 {\n"
        "    var item i32 = 7;\n"
        "    var items [4]i32 = {1, 2, 3, 4};\n"
        "    var view []i32 = items[:];\n"
        "    var bag Bag;\n"
        "    var sum i32 = 0;\n"
        "    for (var item : items) {\n"
        "        sum = sum + item;\n"
        "    }\n"
        "    for (const cell : items) {\n"
        "        sum = sum + cell;\n"
        "    }\n"
        "    for (var p : ptr_of(items)) {\n"
        "        sum = sum + *p;\n"
        "    }\n"
        "    for (var n : view) {\n"
        "        sum = sum + n;\n"
        "    }\n"
        "    for (var part : bag) {\n"
        "        sum = sum + 0;\n"
        "    }\n"
        "    return item + sum;\n"
        "}\n";
    const std::string each_uri = "file:///tmp/tlc-for-each-lsp.tlc";
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  each_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(each_source) + R"("}}})");

    const LineCol each_decl = at_needle(each_source, "for (var item :", false);
    const LineCol each_use = at_needle(each_source, "item;", false);
    const std::string each_def = request_at(to_child[1], from_child[0], 54, "textDocument/definition", each_uri, each_use);
    EXPECT(check, each_decl.line >= 0 && each_use.line >= 0);
    EXPECT_HAS(check, each_def, "\"line\":" + std::to_string(each_decl.line) + ",");
    const LineCol outer_item = at_needle(each_source, "var item i32", false);
    EXPECT(check, each_def.find("\"line\":" + std::to_string(outer_item.line) + ",") == std::string::npos);
    const std::string each_hover = request_at(to_child[1], from_child[0], 55, "textDocument/hover", each_uri, each_use);
    EXPECT_HAS(check, each_hover, "item i32");
    const LineCol each_complete = at_needle(each_source, "item;", false);
    const std::string each_items =
        request_at(to_child[1], from_child[0], 56, "textDocument/completion", each_uri, each_complete);
    EXPECT_HAS(check, each_items, "\"label\":\"item\"");

    const LineCol cell_use = at_needle(each_source, "cell;", false);
    const std::string cell_hover = request_at(to_child[1], from_child[0], 57, "textDocument/hover", each_uri, cell_use);
    EXPECT_HAS(check, cell_hover, "const cell i32");
    const LineCol cell_decl = at_needle(each_source, "for (const cell :", false);
    const std::string cell_def = request_at(to_child[1], from_child[0], 58, "textDocument/definition", each_uri, cell_use);
    EXPECT_HAS(check, cell_def, "\"line\":" + std::to_string(cell_decl.line) + ",");

    const LineCol ptr_use = at_needle(each_source, "p;", false);
    const std::string ptr_hover = request_at(to_child[1], from_child[0], 59, "textDocument/hover", each_uri, ptr_use);
    EXPECT_HAS(check, ptr_hover, "p *i32");
    const LineCol ptr_decl = at_needle(each_source, "for (var p :", false);
    const std::string ptr_def = request_at(to_child[1], from_child[0], 60, "textDocument/definition", each_uri, ptr_use);
    EXPECT_HAS(check, ptr_def, "\"line\":" + std::to_string(ptr_decl.line) + ",");

    const LineCol slice_use = at_needle(each_source, "n;", false);
    const std::string slice_hover = request_at(to_child[1], from_child[0], 61, "textDocument/hover", each_uri, slice_use);
    EXPECT_HAS(check, slice_hover, "n i32");

    const LineCol part_use = at_needle(each_source, "part :", false);
    const std::string part_hover = request_at(to_child[1], from_child[0], 62, "textDocument/hover", each_uri, part_use);
    EXPECT_HAS(check, part_hover, "\"value\":\"part\"");
    EXPECT(check, part_hover.find("part Bag") == std::string::npos);
    const LineCol part_body = at_needle(each_source, "sum + 0", false);
    const std::string each_part_items =
        request_at(to_child[1], from_child[0], 63, "textDocument/completion", each_uri, part_body);
    EXPECT_HAS(check, each_part_items, "\"label\":\"part\"");

    const LineCol after_item = at_needle(each_source, "item +", false);
    const std::string after_item_def =
        request_at(to_child[1], from_child[0], 65, "textDocument/definition", each_uri, after_item);
    EXPECT_HAS(check, after_item_def, "\"line\":" + std::to_string(outer_item.line) + ",");
    EXPECT(check, after_item_def.find("\"line\":" + std::to_string(each_decl.line) + ",") == std::string::npos);
    const std::string after_items =
        request_at(to_child[1], from_child[0], 66, "textDocument/completion", each_uri, after_item);
    EXPECT(check, after_items.find("\"label\":\"cell\"") == std::string::npos);
    EXPECT(check, after_items.find("\"label\":\"part\"") == std::string::npos);

    const std::string edit_source =
        "func add(x i32, y i32) i32 {\n"
        "return x + y;\n"
        "}\n"
        "func main() i32 {\n"
        "    var n = 1;\n"
        "    var n = n + 1;\n"
        "    return n + add(1, 2);\n"
        "}\n";
    const std::string edit_uri = "file:///tmp/tlc-lsp-edit.tlc";
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  edit_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(edit_source) + R"("}}})");

    LineCol rename_use = at_needle(edit_source, "return n", false);
    rename_use.character += static_cast<int>(std::string("return ").size());
    send_json(to_child[1], std::string(R"({"jsonrpc":"2.0","id":70,"method":"textDocument/rename","params":{"textDocument":{"uri":")") +
                         edit_uri + R"("},"position":{"line":)" + std::to_string(rename_use.line) + R"(,"character":)" +
                         std::to_string(rename_use.character) + R"(},"newName":"m"}})");
    const std::string renamed = read_until(from_child[0], "\"id\":70", 3000);
    EXPECT_HAS(check, renamed, "\"newText\":\"m\"");
    EXPECT(check, renamed.find("\"newText\":\"m\"") != renamed.rfind("\"newText\":\"m\""));

    LineCol rename_old = at_needle(edit_source, "var n = 1", false);
    rename_old.character += static_cast<int>(std::string("var ").size());
    send_json(to_child[1], std::string(R"({"jsonrpc":"2.0","id":71,"method":"textDocument/rename","params":{"textDocument":{"uri":")") +
                         edit_uri + R"("},"position":{"line":)" + std::to_string(rename_old.line) + R"(,"character":)" +
                         std::to_string(rename_old.character) + R"(},"newName":"kept"}})");
    const std::string renamed_old = read_until(from_child[0], "\"id\":71", 3000);
    EXPECT_HAS(check, renamed_old, "\"newText\":\"kept\"");

    send_json(to_child[1], std::string(R"({"jsonrpc":"2.0","id":72,"method":"textDocument/formatting","params":{"textDocument":{"uri":")") +
                         edit_uri + R"("},"options":{"tabSize":4,"insertSpaces":true}}})");
    const std::string formatted = read_until(from_child[0], "\"id\":72", 3000);
    EXPECT_HAS(check, formatted, "    return x + y;");

    LineCol call_at = at_needle(edit_source, "add(1, 2)", false);
    call_at.character += static_cast<int>(std::string("add(").size());
    send_json(to_child[1], std::string(R"({"jsonrpc":"2.0","id":73,"method":"textDocument/signatureHelp","params":{"textDocument":{"uri":")") +
                         edit_uri + R"("},"position":{"line":)" + std::to_string(call_at.line) + R"(,"character":)" +
                         std::to_string(call_at.character) + "}}}");
    const std::string signature = read_until(from_child[0], "\"id\":73", 3000);
    EXPECT_HAS(check, signature, "x i32");
    EXPECT_HAS(check, signature, "y i32");
    EXPECT_HAS(check, signature, "\"activeParameter\":0");

    const std::string semi_source = "func main() i32 {\n    return 1\n}\n";
    const std::string semi_uri = "file:///tmp/tlc-lsp-semi.tlc";
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  semi_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(semi_source) + R"("}}})");
    send_json(to_child[1], std::string(R"({"jsonrpc":"2.0","id":74,"method":"textDocument/codeAction","params":{"textDocument":{"uri":")") +
                         semi_uri +
                         R"("},"range":{"start":{"line":0,"character":0},"end":{"line":3,"character":0}},"context":{"diagnostics":[]}}})");
    const std::string action = read_until(from_child[0], "\"id\":74", 3000);
    EXPECT_HAS(check, action, "Insert ';'");
    EXPECT_HAS(check, action, "\"newText\":\";\"");

    const std::string sync_source = "func main() i32 {\n    return 1;\n}\n";
    const std::string sync_uri = "file:///tmp/tlc-lsp-sync.tlc";
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  sync_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(sync_source) + R"("}}})");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")") +
                  sync_uri +
                  R"(","version":2},"contentChanges":[{"range":{"start":{"line":1,"character":0},"end":{"line":1,"character":0}},"text":"    var n = 4;\n"},{"range":{"start":{"line":2,"character":11},"end":{"line":2,"character":12}},"text":"n"}]}})");
    const LineCol sync_use = {2, 11};
    const std::string sync_hover = request_at(to_child[1], from_child[0], 80, "textDocument/hover", sync_uri, sync_use);
    EXPECT_HAS(check, sync_hover, "\"value\":\"n\"");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")") +
                  sync_uri +
                  R"(","version":3},"contentChanges":[{"text":"func main() i32 {\n    var kept = 9;\n    return kept;\n}\n"}]}})");
    const LineCol kept_use = {2, 11};
    const std::string kept_hover = request_at(to_child[1], from_child[0], 81, "textDocument/hover", sync_uri, kept_use);
    EXPECT_HAS(check, kept_hover, "kept");
    EXPECT(check, kept_hover.find("\"value\":\"n\"") == std::string::npos);

    char token_path[] = "/tmp/tlc-lsp-tokens-XXXXXX.tlc";
    const int token_file = ::mkstemps(token_path, 4);
    EXPECT(check, token_file >= 0);
    const std::string token_source =
        "// Point stays a comment\n"
        "type Point struct {\n"
        "    x i32;\n"
        "};\n"
        "\n"
        "func Point::make(n i32) Point {\n"
        "    var p Point;\n"
        "    p.x = n;\n"
        "    return p;\n"
        "}\n"
        "\n"
        "type Color enum {\n"
        "    Red;\n"
        "};\n"
        "\n"
        "const limit = 4;\n"
        "\n"
        "func main() i32 {\n"
        "    var row []Point;\n"
        "    var n = row.len;\n"
        "    var label = \"Point\";\n"
        "    return Point.make(1).x + Color.Red + limit;\n"
        "}\n";
    if (token_file >= 0) {
        EXPECT(check, ::write(token_file, token_source.data(), token_source.size()) == static_cast<ssize_t>(token_source.size()));
        ::close(token_file);
    }
    const std::string token_uri = std::string("file://") + token_path;
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  token_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(token_source) + R"("}}})");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":110,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":")") +
                  token_uri + "\"}}}");
    const std::string colored = read_until(from_child[0], "\"id\":110", 4000);
    const std::string want_tokens =
        "Point 2 1\n"
        "x 7 1\n"
        "Point 2 0\n"
        "make 10 5\n"
        "n 6 1\n"
        "Point 2 0\n"
        "p 6 1\n"
        "Point 2 0\n"
        "p 6 0\n"
        "x 7 0\n"
        "n 6 0\n"
        "p 6 0\n"
        "Color 3 1\n"
        "Red 8 1\n"
        "limit 6 3\n"
        "main 9 1\n"
        "row 6 1\n"
        "Point 2 0\n"
        "n 6 1\n"
        "row 6 0\n"
        "len 7 0\n"
        "label 6 1\n"
        "Point 2 0\n"
        "make 10 4\n"
        "x 7 0\n"
        "Color 3 0\n"
        "Red 8 0\n"
        "limit 6 2\n";
    EXPECT_EQ(check, semantic_summary(colored, token_source, "\"id\":110"), want_tokens);

    const LineCol call_start = at_needle(token_source, "Point.make(1).x", false);
    const LineCol call_end = at_needle(token_source, "Point.make(1).x", true);
    EXPECT(check, call_start.line >= 0);
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":111,"method":"textDocument/semanticTokens/range","params":{"textDocument":{"uri":")") +
                  token_uri + R"("},"range":{"start":{"line":)" + std::to_string(call_start.line) + R"(,"character":)" +
                  std::to_string(call_start.character) + R"(},"end":{"line":)" + std::to_string(call_end.line) +
                  R"(,"character":)" + std::to_string(call_end.character) + "}}}}");
    const std::string call_tokens = read_until(from_child[0], "\"id\":111", 3000);
    EXPECT_EQ(check, semantic_summary(call_tokens, token_source, "\"id\":111"),
              std::string("Point 2 0\nmake 10 4\nx 7 0\n"));

    send_json(to_child[1],
              R"({"jsonrpc":"2.0","id":112,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":"file:///tmp/tlc-lsp-missing.tlc"}}})");
    const std::string missing_tokens = read_until(from_child[0], "\"id\":112", 3000);
    EXPECT_HAS(check, missing_tokens, "\"result\":null");

    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didChange","params":{"textDocument":{"uri":")") +
                  token_uri + R"(","version":2},"contentChanges":[{"text":"// note\n"}]}})");
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":113,"method":"textDocument/semanticTokens/full","params":{"textDocument":{"uri":")") +
                  token_uri + "\"}}}");
    const std::string blank_tokens = read_until(from_child[0], "\"id\":113", 3000);
    EXPECT_HAS(check, blank_tokens, "\"id\":113");
    EXPECT_HAS(check, blank_tokens, "\"data\":[]");

    const std::filesystem::path builtin_root = "/tmp/tlc-lsp-builtin";
    std::filesystem::remove_all(builtin_root);
    std::filesystem::create_directories(builtin_root);
    const std::string builtin_source = "import \"builtin\";\n"
                                       "\n"
                                       "// Adds one.\n"
                                       "func inc(n i32) i32 {\n"
                                       "    return n + 1;\n"
                                       "}\n"
                                       "\n"
                                       "func main() i32 {\n"
                                       "    var p = builtin.trace;\n"
                                       "    var t builtin.Type = typeof(i32);\n"
                                       "    var fs = t.fields();\n"
                                       "    var heap builtin.Malloc = builtin.allocator;\n"
                                       "    var block = unsafe heap.malloc(8, 1);\n"
                                       "    return inc(1);\n"
                                       "}\n";
    {
        std::ofstream builtin_out(builtin_root / "main.tlc");
        builtin_out << builtin_source;
    }
    const std::string builtin_uri = std::string("file://") + (builtin_root / "main.tlc").string();
    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":")") +
                  builtin_uri + R"(","languageId":"tlc","version":1,"text":")" + json_escape(builtin_source) + R"("}}})");
    const LineCol trace_at = at_needle(builtin_source, "trace", false);
    EXPECT(check, trace_at.line >= 0);
    const std::string trace_hover = request_at(to_child[1], from_child[0], 120, "textDocument/hover", builtin_uri, trace_at);
    EXPECT_HAS(check, trace_hover, "func trace() *const u8");
    EXPECT_HAS(check, trace_hover, "newest first");
    const std::string trace_def =
        request_at(to_child[1], from_child[0], 121, "textDocument/definition", builtin_uri, trace_at);
    EXPECT_HAS(check, trace_def, "builtin.tlc");
    const LineCol type_at = at_needle(builtin_source, "Type", false);
    EXPECT(check, type_at.line >= 0);
    const std::string type_hover = request_at(to_child[1], from_child[0], 122, "textDocument/hover", builtin_uri, type_at);
    EXPECT_HAS(check, type_hover, "type Type");
    EXPECT_HAS(check, type_hover, "typeof produces it");
    LineCol inc_at = at_needle(builtin_source, "return inc", false);
    EXPECT(check, inc_at.line >= 0);
    inc_at.character += static_cast<int>(std::string("return ").size());
    const std::string inc_hover = request_at(to_child[1], from_child[0], 123, "textDocument/hover", builtin_uri, inc_at);
    EXPECT_HAS(check, inc_hover, "func inc(n i32) i32");
    EXPECT_HAS(check, inc_hover, "Adds one.");
    const LineCol builtin_dot = at_needle(builtin_source, "builtin.", true);
    EXPECT(check, builtin_dot.line >= 0);
    const std::string member_items =
        request_at(to_child[1], from_child[0], 124, "textDocument/completion", builtin_uri, builtin_dot);
    EXPECT_HAS(check, member_items, "\"trace\"");
    EXPECT_HAS(check, member_items, "\"va\"");
    EXPECT_HAS(check, member_items, "\"panic\"");
    EXPECT_HAS(check, member_items, "\"Type\"");
    EXPECT_HAS(check, member_items, "\"Malloc\"");
    EXPECT_HAS(check, member_items, "\"allocator\"");
    const LineCol fields_at = at_needle(builtin_source, "fields", false);
    EXPECT(check, fields_at.line >= 0);
    const std::string fields_hover =
        request_at(to_child[1], from_child[0], 125, "textDocument/hover", builtin_uri, fields_at);
    EXPECT_HAS(check, fields_hover, "func Type.fields");
    EXPECT_HAS(check, fields_hover, "[]const Field");
    EXPECT_HAS(check, fields_hover, "named struct");
    LineCol malloc_at = at_needle(builtin_source, "heap.malloc", false);
    malloc_at.character += static_cast<int>(std::string("heap.").size());
    EXPECT(check, malloc_at.line >= 0);
    const std::string malloc_hover =
        request_at(to_child[1], from_child[0], 126, "textDocument/hover", builtin_uri, malloc_at);
    EXPECT_HAS(check, malloc_hover, "func Malloc.malloc");
    EXPECT_HAS(check, malloc_hover, "aligned_alloc");
    std::filesystem::remove_all(builtin_root);

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
    ::unlink(token_path);
    return check.failed;
}
