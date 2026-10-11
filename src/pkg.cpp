#include "pkg.hpp"

#include "lexer.hpp"
#include "parser.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tlc {
namespace {

namespace fs = std::filesystem;

bool is_importer_relative(std::string_view spec) {
    return spec == "." || spec == ".." || spec.starts_with("./") || spec.starts_with("../");
}

std::vector<fs::path> source_files(const fs::path &dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) {
        return files;
    }
    fs::directory_iterator it(dir, ec);
    const fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code file_ec;
        if (!it->is_regular_file(file_ec) || file_ec) {
            continue;
        }
        if (it->path().extension() == ".tlc") {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

// Filename test, matching the linker's library-file rule used on the command line.
bool is_vendor_library(const fs::path &path) {
    const std::string name = path.filename().string();
    if (ends_with(name, ".a") || ends_with(name, ".so") || ends_with(name, ".dylib")) {
        return true;
    }
    return name.find(".so.") != std::string::npos;
}

std::string library_stem(const std::string &filename) {
    std::string name = filename;
    if (name.starts_with("lib")) {
        name.erase(0, 3);
    }
    if (const std::size_t pos = name.find(".so"); pos != std::string::npos) {
        name.resize(pos);
        return name;
    }
    if (ends_with(name, ".dylib")) {
        name.resize(name.size() - 6);
        return name;
    }
    if (ends_with(name, ".a")) {
        name.resize(name.size() - 2);
    }
    return name;
}

bool plain_shared_name(const std::string &filename) {
    return ends_with(filename, ".so") || ends_with(filename, ".dylib");
}

std::string rooted_path(const fs::path &dir, const std::string &path) {
    if (path.empty() || path.starts_with('$') || fs::path(path).is_absolute()) {
        return path;
    }
    return (dir / path).lexically_normal().string();
}

bool is_under(const fs::path &dir, const fs::path &root) {
    if (root.empty()) {
        return false;
    }
    const std::string dir_text = dir.generic_string();
    std::string root_text = root.generic_string();
    if (dir_text == root_text) {
        return false;
    }
    if (!root_text.ends_with('/')) {
        root_text.push_back('/');
    }
    return dir_text.starts_with(root_text);
}

class Resolver {
  public:
    Resolver(const ResolveRequest &request, SourceSet &sources, DiagnosticEngine &diags, PackageGraph &graph)
        : sources_(sources), diags_(diags), graph_(graph), vendor_request_(request.vendor_root),
          std_request_(request.std_root) {
        for (const std::string &root : request.package_paths) {
            extra_roots_.emplace_back(root);
        }
        entry_ = request.entry;
    }

    void run() {
        std::error_code ec;
        if (!fs::is_regular_file(entry_, ec) || ec) {
            diags_.error({}, "cannot read file");
            return;
        }
        const fs::path canonical = fs::canonical(entry_, ec);
        if (ec) {
            diags_.error({}, "cannot read file");
            return;
        }
        roots_.push_back(canonical.parent_path());
        for (const fs::path &extra : extra_roots_) {
            roots_.push_back(extra);
        }
        add_vendor_root(vendor_request_);
        add_std_root(std_request_);
        const std::size_t index = load_new(canonical);
        if (index == kMissing) {
            diags_.error({}, "cannot read file");
            return;
        }
        graph_.nodes[index].package_key = canonical.string();
        if (graph_.nodes[index].qualifier.empty()) {
            graph_.nodes[index].qualifier = canonical.stem().string();
        }
        by_package_.emplace(graph_.nodes[index].package_key, index);
        walk_package(graph_.nodes[index].package_key);
    }

  private:
    static constexpr std::size_t kMissing = static_cast<std::size_t>(-1);

    enum class HitKind { Missing, Found, Failed };

    struct Hit {
        HitKind kind = HitKind::Missing;
        fs::path path;
    };

    SourceSet &sources_;
    DiagnosticEngine &diags_;
    PackageGraph &graph_;
    fs::path entry_;
    std::string vendor_request_;
    std::string std_request_;
    fs::path vendor_root_;
    fs::path std_root_;
    std::vector<fs::path> extra_roots_;
    std::vector<fs::path> roots_;
    std::unordered_map<std::string, std::size_t> by_path_;
    std::unordered_map<std::string, std::size_t> by_package_;
    std::vector<std::string> package_stack_;

    std::size_t load_new(const fs::path &canonical) {
        std::optional<std::string> text = read_file(canonical.string());
        if (!text) {
            return kMissing;
        }
        SourceFile &file = sources_.add(canonical.string(), std::move(*text));
        Lexer lexer(file, diags_);
        Parser parser(lexer.tokenize(), diags_);
        PackageNode node;
        node.file_id = file.id();
        node.path = canonical.string();
        node.unit = parser.parse();
        if (node.unit.package && !node.unit.package->name.empty()) {
            node.qualifier = node.unit.package->name;
        } else {
            node.qualifier = canonical.stem().string();
        }
        const std::size_t index = graph_.nodes.size();
        by_path_.emplace(node.path, index);
        graph_.nodes.push_back(std::move(node));
        return index;
    }

    bool on_stack(const std::string &key) const {
        for (const std::string &stacked : package_stack_) {
            if (stacked == key) {
                return true;
            }
        }
        return false;
    }

    std::string qualifier_of(const std::string &key) const {
        const auto found = by_package_.find(key);
        if (found == by_package_.end() || found->second >= graph_.nodes.size()) {
            return key;
        }
        return graph_.nodes[found->second].qualifier;
    }

    std::string cycle_message(const std::string &repeated) const {
        std::string message = "circular import ";
        bool started = false;
        bool first = true;
        for (const std::string &key : package_stack_) {
            if (!started) {
                if (key != repeated) {
                    continue;
                }
                started = true;
            }
            if (!first) {
                message += " -> ";
            }
            first = false;
            message += qualifier_of(key);
        }
        message += " -> ";
        message += qualifier_of(repeated);
        return message;
    }

    std::string choose_qualifier(const std::vector<std::size_t> &indexes, const fs::path &dir, SourceLoc loc) {
        std::string expected;
        bool saw = false;
        bool mismatch = false;
        SourceLoc where = loc;
        for (const std::size_t index : indexes) {
            const TranslationUnit &unit = graph_.nodes[index].unit;
            const std::string name = unit.package ? unit.package->name : std::string{};
            if (!saw) {
                expected = name;
                saw = true;
                if (unit.package) {
                    where = unit.package->loc;
                }
                continue;
            }
            if (name != expected) {
                mismatch = true;
                if (unit.package) {
                    where = unit.package->loc;
                }
            }
        }
        if (mismatch) {
            diags_.error(where, "package mismatch");
        }
        if (expected.empty()) {
            return dir.filename().string();
        }
        return expected;
    }

    Hit find_in_base(const fs::path &base, std::string_view spec) {
        const fs::path dir = base / fs::path(std::string(spec));
        if (source_files(dir).empty()) {
            return {};
        }
        std::error_code ec;
        fs::path canonical = fs::canonical(dir, ec);
        if (ec) {
            return {};
        }
        return {HitKind::Found, std::move(canonical)};
    }

    Hit locate(const fs::path &importer_dir, const ImportDecl &import) {
        const std::string_view spec = import.path;
        if (spec.empty()) {
            diags_.error(import.loc, "empty package path");
            return {HitKind::Failed, {}};
        }
        if (fs::path(std::string(spec)).is_absolute()) {
            diags_.error(import.loc, "absolute package path");
            return {HitKind::Failed, {}};
        }
        if (is_importer_relative(spec)) {
            const Hit hit = find_in_base(importer_dir, spec);
            if (hit.kind == HitKind::Missing) {
                diags_.error(import.loc, "unknown package");
                return {HitKind::Failed, {}};
            }
            return hit;
        }
        for (const fs::path &root : roots_) {
            const Hit hit = find_in_base(root, spec);
            if (hit.kind == HitKind::Missing) {
                continue;
            }
            return hit;
        }
        if (!vendor_root_.empty() && (spec == "vendor" || spec.starts_with("vendor/"))) {
            Hit hit;
            if (spec == "vendor") {
                if (!source_files(vendor_root_).empty()) {
                    hit = {HitKind::Found, vendor_root_};
                }
            } else {
                const std::string_view inner = spec.substr(std::string_view("vendor/").size());
                hit = find_in_base(vendor_root_, inner);
                if (hit.kind == HitKind::Found && !is_under(hit.path, vendor_root_)) {
                    hit = {};
                }
            }
            if (hit.kind == HitKind::Found) {
                return hit;
            }
        }
        if (!std_root_.empty() && spec != "vendor" && !spec.starts_with("vendor/")) {
            const Hit hit = find_in_base(std_root_, spec);
            if (hit.kind == HitKind::Found && is_under(hit.path, std_root_)) {
                return hit;
            }
        }
        if (spec == "builtin") {
            return {HitKind::Found, fs::path("<builtin>")};
        }
        diags_.error(import.loc, "unknown package");
        return {HitKind::Failed, {}};
    }

    void add_edge(uint32_t from_file, const std::string &key, const ImportDecl &import) {
        const auto found = by_package_.find(key);
        if (found == by_package_.end()) {
            return;
        }
        graph_.edges.push_back(ImportEdge{import.loc, from_file, graph_.nodes[found->second].file_id, import.path});
    }

    void load_directory(const fs::path &dir, uint32_t from_file, const ImportDecl &import) {
        const std::string key = dir.string();
        const std::vector<fs::path> files = source_files(dir);
        if (files.empty()) {
            diags_.error(import.loc, "unknown package");
            return;
        }
        for (const fs::path &file : files) {
            std::error_code ec;
            const fs::path canonical = fs::canonical(file, ec);
            if (ec) {
                diags_.error(import.loc, "cannot read file");
                return;
            }
            const auto existing = by_path_.find(canonical.string());
            if (existing == by_path_.end()) {
                continue;
            }
            const std::string &existing_key = graph_.nodes[existing->second].package_key;
            if (on_stack(existing_key)) {
                diags_.error(import.loc, cycle_message(existing_key));
            } else {
                diags_.error(import.loc, "redefinition");
            }
            return;
        }
        std::vector<std::size_t> created;
        created.reserve(files.size());
        for (const fs::path &file : files) {
            std::error_code ec;
            const fs::path canonical = fs::canonical(file, ec);
            if (ec) {
                diags_.error(import.loc, "cannot read file");
                return;
            }
            const std::size_t index = load_new(canonical);
            if (index == kMissing) {
                diags_.error(import.loc, "cannot read file");
                return;
            }
            created.push_back(index);
        }
        const std::string qualifier = choose_qualifier(created, dir, import.loc);
        for (const std::size_t index : created) {
            PackageNode &node = graph_.nodes[index];
            node.package_key = key;
            node.qualifier = qualifier;
            node.imported_from = from_file;
            node.import_spec = import.path;
            node.import_loc = import.loc;
        }
        by_package_.emplace(key, created.front());
        add_edge(from_file, key, import);
        if (is_under(dir, vendor_root_) || is_under(dir, std_root_)) {
            append_vendor_libraries(dir, import.loc);
        }
        walk_package(key);
    }

    void add_vendor_root(const std::string &root) {
        if (root.empty()) {
            return;
        }
        std::error_code ec;
        fs::path canonical = fs::canonical(root, ec);
        if (ec || !fs::is_directory(canonical, ec)) {
            return;
        }
        vendor_root_ = std::move(canonical);
    }

    void add_std_root(const std::string &root) {
        if (root.empty()) {
            return;
        }
        std::error_code ec;
        fs::path canonical = fs::canonical(root, ec);
        if (ec || !fs::is_directory(canonical, ec)) {
            return;
        }
        std_root_ = std::move(canonical);
    }

    void append_token(const fs::path &dir, const std::string &token) {
        if (token == "-Bstatic" || token == "-Bdynamic") {
            graph_.link_args.push_back("-Wl," + token);
            return;
        }
        constexpr std::string_view kRpath = "-Wl,-rpath,";
        if (token.starts_with(kRpath)) {
            graph_.link_args.push_back(std::string(kRpath) + rooted_path(dir, token.substr(kRpath.size())));
            return;
        }
        if (token.starts_with("-L") && token.size() > 2) {
            graph_.link_args.push_back("-L" + rooted_path(dir, token.substr(2)));
            return;
        }
        if (!token.empty() && token[0] != '-') {
            graph_.link_args.push_back(rooted_path(dir, token));
            return;
        }
        graph_.link_args.push_back(token);
    }

    void append_link_file(const fs::path &dir, const fs::path &link_file, SourceLoc loc) {
        const std::optional<std::string> text = read_file(link_file.string());
        if (!text) {
            diags_.error(loc, "cannot read file");
            return;
        }
        std::string line;
        bool have_l_dir = false;
        auto finish_line = [&]() {
            std::string words;
            for (std::size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '#' && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t')) {
                    break;
                }
                words.push_back(line[i]);
            }
            std::string token;
            auto flush = [&]() {
                if (token.empty()) {
                    return;
                }
                if (have_l_dir) {
                    graph_.link_args.push_back("-L" + rooted_path(dir, token));
                    have_l_dir = false;
                } else if (token == "-L") {
                    have_l_dir = true;
                } else {
                    append_token(dir, token);
                }
                token.clear();
            };
            for (const char ch : words) {
                if (ch == ' ' || ch == '\t' || ch == '\r') {
                    flush();
                } else {
                    token.push_back(ch);
                }
            }
            flush();
            line.clear();
        };
        for (const char ch : *text) {
            if (ch == '\n') {
                finish_line();
            } else {
                line.push_back(ch);
            }
        }
        if (!line.empty() || have_l_dir) {
            finish_line();
        }
    }

    // A static archive wins over a shared library of the same stem. The
    // optional link file adds arguments after those libraries.
    void append_vendor_libraries(const fs::path &dir, SourceLoc loc) {
        struct Chosen {
            std::string filename;
            fs::path path;
            bool archive = false;
            bool plain_shared = false;
        };
        std::vector<Chosen> chosen;
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        const fs::directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code file_ec;
            if (!it->is_regular_file(file_ec) || file_ec || !is_vendor_library(it->path())) {
                continue;
            }
            const fs::path canonical = fs::canonical(it->path(), file_ec);
            if (file_ec) {
                diags_.error(loc, "cannot read file");
                return;
            }
            const std::string filename = it->path().filename().string();
            Chosen next;
            next.filename = filename;
            next.path = canonical;
            next.archive = ends_with(filename, ".a");
            next.plain_shared = plain_shared_name(filename);
            const std::string stem = library_stem(filename);
            Chosen *existing = nullptr;
            for (Chosen &item : chosen) {
                if (library_stem(item.filename) == stem) {
                    existing = &item;
                    break;
                }
            }
            if (existing == nullptr) {
                chosen.push_back(std::move(next));
                continue;
            }
            if (existing->archive) {
                continue;
            }
            if (next.archive) {
                *existing = std::move(next);
                continue;
            }
            if (existing->plain_shared != next.plain_shared) {
                if (next.plain_shared) {
                    *existing = std::move(next);
                }
                continue;
            }
            if (next.filename < existing->filename) {
                *existing = std::move(next);
            }
        }
        if (ec) {
            diags_.error(loc, "cannot read file");
            return;
        }
        std::vector<Chosen> archives;
        std::vector<Chosen> shared;
        for (Chosen &item : chosen) {
            if (item.archive) {
                archives.push_back(std::move(item));
            } else {
                shared.push_back(std::move(item));
            }
        }
        auto by_name = [](const Chosen &left, const Chosen &right) { return left.filename < right.filename; };
        std::sort(archives.begin(), archives.end(), by_name);
        std::sort(shared.begin(), shared.end(), by_name);
        for (const Chosen &item : archives) {
            graph_.link_args.push_back(item.path.string());
        }
        if (!shared.empty()) {
            graph_.link_args.push_back("-Wl,-rpath," + dir.string());
            for (const Chosen &item : shared) {
                graph_.link_args.push_back(item.path.string());
            }
        }
        const fs::path link_file = dir / "link";
        if (fs::is_regular_file(link_file, ec) && !ec) {
            append_link_file(dir, link_file, loc);
        }
    }

    void resolve_one(uint32_t from_file, const fs::path &importer_dir, const ImportDecl &import) {
        const Hit hit = locate(importer_dir, import);
        if (hit.kind != HitKind::Found) {
            return;
        }
        const std::string key = hit.path.string();
        if (by_package_.contains(key)) {
            if (on_stack(key)) {
                diags_.error(import.loc, cycle_message(key));
                return;
            }
            add_edge(from_file, key, import);
            return;
        }
        if (hit.path == "<builtin>") {
            load_builtin(from_file, import);
            return;
        }
        load_directory(hit.path, from_file, import);
    }

    // `trace` and `va` are compiler intrinsics. They are not read from a .tlc file.
    void load_builtin(uint32_t from_file, const ImportDecl &import) {
        constexpr const char *kKey = "<builtin>";
        if (by_package_.contains(kKey)) {
            add_edge(from_file, kKey, import);
            return;
        }
        auto pointer_char = [](const std::string &name) {
            FunctionDecl fn;
            fn.exported = true;
            fn.name = name;
            fn.return_type = "char";
            fn.indirections = 1;
            ResultSlot slot;
            slot.type_name = "char";
            slot.indirections = 1;
            fn.results.push_back(std::move(slot));
            return fn;
        };
        SourceFile &file = sources_.add(kKey, "");
        PackageNode node;
        node.file_id = file.id();
        node.path = kKey;
        node.qualifier = "builtin";
        node.package_key = kKey;
        PackageClause clause;
        clause.name = "builtin";
        node.unit.package = clause;
        TopDecl trace;
        trace.data = pointer_char("trace");
        TopDecl va;
        va.data = pointer_char("va");
        node.unit.decls.push_back(std::move(trace));
        node.unit.decls.push_back(std::move(va));
        node.imported_from = from_file;
        node.import_spec = import.path;
        node.import_loc = import.loc;
        const std::size_t index = graph_.nodes.size();
        by_path_.emplace(kKey, index);
        by_package_.emplace(kKey, index);
        graph_.nodes.push_back(std::move(node));
        add_edge(from_file, kKey, import);
        graph_.link_args.push_back("-lc");
    }

    void walk_package(const std::string &key) {
        package_stack_.push_back(key);
        std::vector<std::size_t> indexes;
        for (std::size_t index = 0; index < graph_.nodes.size(); ++index) {
            if (graph_.nodes[index].package_key == key) {
                indexes.push_back(index);
            }
        }
        for (const std::size_t index : indexes) {
            const std::vector<ImportDecl> imports = graph_.nodes[index].unit.imports;
            const uint32_t from_file = graph_.nodes[index].file_id;
            const fs::path from_dir = fs::path(graph_.nodes[index].path).parent_path();
            for (const ImportDecl &import : imports) {
                resolve_one(from_file, from_dir, import);
            }
        }
        package_stack_.pop_back();
    }
};

} // namespace

bool resolve_packages(const ResolveRequest &request, SourceSet &sources, DiagnosticEngine &diags,
                      PackageGraph &graph) {
    graph = PackageGraph{};
    Resolver resolver(request, sources, diags, graph);
    resolver.run();
    return diags.ok();
}

} // namespace tlc
