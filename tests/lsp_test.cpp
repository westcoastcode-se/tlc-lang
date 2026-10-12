#include "harness.hpp"

#include <cerrno>
#include <charconv>
#include <fcntl.h>
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

// Read one LSP message. Returns an empty string on timeout or a closed pipe.
std::string read_message(int fd, int timeout_ms) {
    std::string buffer;
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
            return {};
        }
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
    const std::string disk = "func main() int {\n    return 1;\n}\n";
    EXPECT(check, ::write(file, disk.data(), disk.size()) == static_cast<ssize_t>(disk.size()));
    ::close(file);

    const std::string source =
        "func add(a int, b int) int {\n"
        "    return a + b;\n"
        "}\n"
        "\n"
        "type Point struct {\n"
        "    x int;\n"
        "};\n"
        "\n"
        "type Inner struct {\n"
        "    inner_value int;\n"
        "};\n"
        "\n"
        "type Box struct {\n"
        "    value int;\n"
        "    complex Inner;\n"
        "};\n"
        "\n"
        "type K struct {\n"
        "    value int;\n"
        "};\n"
        "\n"
        "func main() int {\n"
        "    var p Point;\n"
        "    var n int = \"no\";\n"
        "    var kk K = {.value = 10};\n"
        "    var box Box = {.complex = {.inner_value = 1}};\n"
        "    kk = {.value = 2};\n"
        "    return add(p.x, 1);\n"
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
    EXPECT_HAS(check, hovered, "func add(a int, b int) int");

    send_json(to_child[1],
              std::string(R"({"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":")") +
                  uri + "\"}}}");
    const std::string symbols = read_until(from_child[0], "\"id\":5", 3000);
    EXPECT_HAS(check, symbols, "Point");
    EXPECT_HAS(check, symbols, "main");

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
