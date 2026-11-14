// The language server loop.
#include <algorithm>
#include <map>
#include "lsp/lsp.hpp"

#include "diag/diag.hpp"
#include "driver/driver.hpp"
#include "lex/lexer.hpp"
#include "parse/parser.hpp"
#include "pkg/pkg.hpp"
#include "check/sema.hpp"

#include <cerrno>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <unistd.h>

#include "lsp/internal.hpp"

namespace tlc {

int run_lsp() {
    using namespace lsp_impl;
    std::vector<Doc> docs;
    std::map<std::string, std::string> sent;
    // The workspace folder from `initialize`. A package checked alone finds its imports from there too.
    std::string workspace_root;
    bool shutdown = false;
    auto find_doc = [&](const std::string &uri) -> Doc * {
        for (Doc &doc : docs) {
            if (doc.uri == uri) {
                return &doc;
            }
        }
        return nullptr;
    };
    while (true) {
        std::string body;
        const ReadStatus status = read_message(body);
        if (status == ReadStatus::End) {
            return shutdown ? 0 : 1;
        }
        if (status == ReadStatus::Skip) {
            continue;
        }
        // JSON-RPC answers a body that is not JSON, and a value that is not a request object, with a null id.
        const std::optional<Json> message = parse_json(body);
        if (!message) {
            send_json(error_response(jnull(), -32700, "parse error"));
            continue;
        }
        if (message->kind != Json::Kind::Object) {
            send_json(error_response(jnull(), -32600, "invalid request"));
            continue;
        }
        const Json *id = message->find("id");
        const bool request = id != nullptr && id->kind != Json::Kind::Null;
        const std::string method = message->str("method");
        const Json *params = message->find("params");
        const Json empty = jobj({});
        if (params == nullptr) {
            params = &empty;
        }
        if (method == "initialize") {
            if (!request) {
                continue;
            }
            std::string root_uri = params->str("rootUri");
            if (const Json *folders = params->find("workspaceFolders");
                root_uri.empty() && folders != nullptr && folders->kind == Json::Kind::Array && !folders->a.empty() &&
                folders->a.front().kind == Json::Kind::Object) {
                root_uri = folders->a.front().str("uri");
            }
            workspace_root = root_uri.empty() ? params->str("rootPath") : uri_to_path(root_uri);
            send_json(result_response(
                *id, jobj({{"capabilities",
                            jobj({{"positionEncoding", jstr("utf-16")},
                                 {"textDocumentSync", jint(2)},
                                 {"completionProvider", jobj({{"triggerCharacters", jarr({jstr(".")})}, {"resolveProvider", jbool(false)}})},
                                 {"hoverProvider", jbool(true)},
                                 {"definitionProvider", jbool(true)},
                                 {"documentSymbolProvider", jbool(true)},
                                 {"renameProvider", jbool(true)},
                                 {"documentFormattingProvider", jbool(true)},
                                 {"codeActionProvider", jbool(true)},
                                 {"signatureHelpProvider",
                                  jobj({{"triggerCharacters", jarr({jstr("("), jstr(",")})},
                                       {"retriggerCharacters", jarr({jstr(")")})}})},
                                 {"semanticTokensProvider",
                                  jobj({{"legend",
                                         jobj({{"tokenTypes",
                                                jarr({jstr("namespace"), jstr("type"), jstr("struct"), jstr("enum"),
                                                      jstr("interface"), jstr("typeParameter"), jstr("variable"),
                                                      jstr("property"), jstr("enumMember"), jstr("function"),
                                                      jstr("method")})},
                                              {"tokenModifiers",
                                               jarr({jstr("declaration"), jstr("readonly"), jstr("static")})}})},
                                       {"range", jbool(true)},
                                       {"full", jbool(true)}})}})},
                           {"serverInfo", jobj({{"name", jstr("tlc")}, {"version", jstr(kVersion)}})}})));
            continue;
        }
        if (method == "shutdown") {
            shutdown = true;
            if (request) {
                send_json(result_response(*id, jnull()));
            }
            continue;
        }
        if (method == "exit") {
            return shutdown ? 0 : 1;
        }
        if (method.empty()) {
            continue;
        }
        if (method == "initialized" || method == "workspace/didChangeConfiguration" || method.starts_with("$/")) {
            continue;
        }
        if (method == "textDocument/didOpen" || method == "textDocument/didChange") {
            const Json *doc_json = text_document(*params);
            if (doc_json == nullptr) {
                continue;
            }
            const std::string uri = doc_json->str("uri");
            std::string text;
            bool got_text = false;
            if (method == "textDocument/didOpen") {
                text = doc_json->str("text");
                got_text = true;
            } else if (const Json *changes = params->find("contentChanges");
                       changes != nullptr && changes->kind == Json::Kind::Array && !changes->a.empty()) {
                // Later ranges in this array are positions in the text after earlier changes.
                const Doc *existing = find_doc(uri);
                bool ranged = false;
                for (const Json &change : changes->a) {
                    if (change.kind == Json::Kind::Object && change.find("range") != nullptr) {
                        ranged = true;
                    }
                }
                if (existing == nullptr && ranged) {
                    continue;
                }
                text = apply_content_changes(existing == nullptr ? std::string{} : existing->text, *changes);
                got_text = true;
            }
            if (!got_text) {
                continue;
            }
            const int version = static_cast<int>(doc_json->integer("version", 0));
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                docs.push_back(Doc{});
                doc = &docs.back();
                doc->uri = uri;
            }
            doc->path = open_path_for(uri_to_path(uri));
            doc->text = std::move(text);
            doc->version = version;
            recheck(*doc, docs, workspace_root);
            // The other open files of that package, and files that import it, see the new text too.
            const std::string changed_dir = std::filesystem::path(doc->path).parent_path().string();
            const std::string changed_uri = doc->uri;
            for (Doc &other : docs) {
                if (other.uri == changed_uri) {
                    continue;
                }
                const bool sibling = std::filesystem::path(other.path).parent_path().string() == changed_dir;
                const bool importer = std::any_of(other.notes.begin(), other.notes.end(), [&](const Note &note) {
                    return note.uri == changed_uri;
                }) || std::any_of(other.syms.begin(), other.syms.end(), [&](const Sym &sym) {
                    return sym.uri == changed_uri;
                });
                if (sibling || importer) {
                    recheck(other, docs, workspace_root);
                }
            }
            sync_diagnostics(docs, sent);
            continue;
        }
        if (method == "textDocument/didClose") {
            const Json *doc_json = text_document(*params);
            if (doc_json == nullptr) {
                continue;
            }
            const std::string uri = doc_json->str("uri");
            for (std::size_t n = 0; n < docs.size(); ++n) {
                if (docs[n].uri != uri) {
                    continue;
                }
                docs.erase(docs.begin() + static_cast<std::ptrdiff_t>(n));
                sync_diagnostics(docs, sent);
                break;
            }
            continue;
        }
        if (!request) {
            continue;
        }
        if (method == "textDocument/completion" || method == "textDocument/hover" || method == "textDocument/definition" ||
            method == "textDocument/documentSymbol") {
            const Json *doc_json = text_document(*params);
            const std::string uri = doc_json == nullptr ? std::string{} : doc_json->str("uri");
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                send_json(result_response(*id, method == "textDocument/documentSymbol" ? jarr({}) : jnull()));
                continue;
            }
            if (method == "textDocument/documentSymbol") {
                send_json(result_response(*id, document_symbols(*doc)));
                continue;
            }
            const Json *position = params->find("position");
            const int line = position_part(position, "line");
            const int character = position_part(position, "character");
            const Cursor cursor = cursor_at(doc->text, line, character);
            if (method == "textDocument/completion") {
                send_json(result_response(*id, complete(*doc, cursor)));
                continue;
            }
            if (cursor.literal || cursor.word.empty()) {
                send_json(result_response(*id, jnull()));
                continue;
            }
            const Sym *sym = nullptr;
            const ExprBase expr = expression_base(*doc, cursor);
            if (const std::string init_type = designated_struct(*doc, cursor); !init_type.empty()) {
                sym = find_field(*doc, init_type, cursor.word);
            } else if (cursor.member) {
                if (!cursor.qualifier.empty()) {
                    sym = qualified_member(doc->syms, doc->uri, cursor);
                } else if (const Sym *base = lookup_bare(doc->syms, doc->uri, cursor.base, cursor.offset)) {
                    sym = refine_overloads(member_candidates(doc->syms, *base, cursor.word), doc->text, cursor, true);
                }
            } else if (expr.handled) {
                // `Point.make(1).x` is the field of the call. A missing member is not a bare name.
                sym = refine_overloads(member_candidates(doc->syms, expr.sym, cursor.word), doc->text, cursor, true);
            } else {
                sym = refine_overloads(bare_candidates(doc->syms, doc->uri, cursor.word, cursor.offset), doc->text, cursor,
                                       false);
            }
            if (sym == nullptr) {
                // `len` is the element count. It is not a field, so it has no symbol of its own.
                if (method == "textDocument/hover" && cursor.qualifier.empty() && cursor.word == "len") {
                    const Sym *base = cursor.member ? lookup_bare(doc->syms, doc->uri, cursor.base, cursor.offset)
                                                    : (expr.handled ? &expr.sym : nullptr);
                    if (base != nullptr &&
                        (base->is_tuple || slice_receiver(*doc, *base) || sized_array_receiver(*doc, *base))) {
                        const Pos start = locate(doc->text, cursor.prefix_begin);
                        const Pos end = locate(doc->text, cursor.prefix_begin + static_cast<uint32_t>(cursor.word.size()));
                        send_json(result_response(*id, hover_text("len i32", start, end)));
                        continue;
                    }
                }
                send_json(result_response(*id, jnull()));
            } else if (method == "textDocument/hover") {
                const Pos start = locate(doc->text, cursor.prefix_begin);
                const Pos end = locate(doc->text, cursor.prefix_begin + static_cast<uint32_t>(cursor.word.size()));
                send_json(result_response(*id, hover_of(*sym, start, end)));
            } else if (method == "textDocument/definition") {
                const Json loc = location_of(*sym);
                if (loc.kind == Json::Kind::Null) {
                    send_json(result_response(*id, jnull()));
                } else {
                    send_json(result_response(*id, jarr({loc})));
                }
            } else {
                send_json(result_response(*id, location_of(*sym)));
            }
            continue;
        }
        if (method == "textDocument/rename" || method == "textDocument/formatting" || method == "textDocument/codeAction" ||
            method == "textDocument/signatureHelp") {
            const Json *doc_json = text_document(*params);
            const std::string uri = doc_json == nullptr ? std::string{} : doc_json->str("uri");
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                const bool list = method == "textDocument/formatting" || method == "textDocument/codeAction";
                send_json(result_response(*id, list ? jarr({}) : jnull()));
                continue;
            }
            if (method == "textDocument/formatting") {
                send_json(result_response(*id, format_edits(*doc)));
                continue;
            }
            if (method == "textDocument/codeAction") {
                const Json *range = params->find("range");
                Pos start;
                Pos end;
                if (range != nullptr) {
                    if (const Json *from = range->find("start"); from != nullptr) {
                        start.line = position_part(from, "line");
                        start.character = position_part(from, "character");
                    }
                    if (const Json *to = range->find("end"); to != nullptr) {
                        end.line = position_part(to, "line");
                        end.character = position_part(to, "character");
                    }
                }
                send_json(result_response(*id, code_actions(*doc, start, end)));
                continue;
            }
            const Json *position = params->find("position");
            const int line = position_part(position, "line");
            const int character = position_part(position, "character");
            if (method == "textDocument/rename") {
                const Cursor cursor = cursor_at(doc->text, line, character);
                send_json(result_response(*id, rename_edits(*doc, cursor, params->str("newName"))));
                continue;
            }
            send_json(result_response(*id, signature_help(*doc, offset_at(doc->text, line, character))));
            continue;
        }
        if (method == "textDocument/semanticTokens/full" || method == "textDocument/semanticTokens/range") {
            const Json *doc_json = text_document(*params);
            const std::string uri = doc_json == nullptr ? std::string{} : doc_json->str("uri");
            Doc *doc = find_doc(uri);
            if (doc == nullptr) {
                send_json(result_response(*id, jnull()));
                continue;
            }
            const bool ranged = method == "textDocument/semanticTokens/range";
            Pos start;
            Pos end;
            if (ranged) {
                const Json *range = params->find("range");
                if (range != nullptr) {
                    if (const Json *from = range->find("start"); from != nullptr) {
                        start.line = position_part(from, "line");
                        start.character = position_part(from, "character");
                    }
                    if (const Json *to = range->find("end"); to != nullptr) {
                        end.line = position_part(to, "line");
                        end.character = position_part(to, "character");
                    }
                }
            }
            send_json(result_response(*id, semantic_tokens(*doc, ranged, start, end)));
            continue;
        }
        send_json(error_response(*id, -32601, "method not found"));
    }
}

} // namespace tlc
