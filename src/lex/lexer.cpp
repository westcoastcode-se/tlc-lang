#include "lex/lexer.hpp"

#include <utility>

namespace tlc {
namespace {

constexpr std::pair<std::string_view, TokenKind> kKeywords[] = {
    {"_Alignas", TokenKind::KwUAlignas},
    {"_Alignof", TokenKind::KwUAlignof},
    {"_Atomic", TokenKind::KwAtomic},
    {"_BitInt", TokenKind::KwBitInt},
    {"_Bool", TokenKind::KwUBool},
    {"_Complex", TokenKind::KwComplex},
    {"_Decimal128", TokenKind::KwDecimal128},
    {"_Decimal32", TokenKind::KwDecimal32},
    {"_Decimal64", TokenKind::KwDecimal64},
    {"_Generic", TokenKind::KwGeneric},
    {"_Imaginary", TokenKind::KwImaginary},
    {"_Noreturn", TokenKind::KwNoreturn},
    {"_Static_assert", TokenKind::KwUStaticAssert},
    {"_Thread_local", TokenKind::KwUThreadLocal},
    {"alignas", TokenKind::KwAlignas},
    {"assert", TokenKind::KwAssert},
    {"alignof", TokenKind::KwAlignof},
    {"auto", TokenKind::KwAuto},
    {"bool", TokenKind::KwBool},
    {"break", TokenKind::KwBreak},
    {"case", TokenKind::KwCase},
    {"comptime", TokenKind::KwComptime},
    {"const", TokenKind::KwConst},
    {"constexpr", TokenKind::KwConstexpr},
    {"continue", TokenKind::KwContinue},
    {"default", TokenKind::KwDefault},
    {"defer", TokenKind::KwDefer},
    {"delete", TokenKind::KwDelete},
    {"do", TokenKind::KwDo},
    {"else", TokenKind::KwElse},
    {"emit", TokenKind::KwEmit},
    {"enum", TokenKind::KwEnum},
    {"export", TokenKind::KwExport},
    {"extern", TokenKind::KwExtern},
    {"false", TokenKind::KwFalse},
    {"for", TokenKind::KwFor},
    {"func", TokenKind::KwFunc},
    {"goto", TokenKind::KwGoto},
    {"if", TokenKind::KwIf},
    {"import", TokenKind::KwImport},
    {"inline", TokenKind::KwInline},
    {"new", TokenKind::KwNew},
    {"null", TokenKind::KwNull},
    {"package", TokenKind::KwPackage},
    {"register", TokenKind::KwRegister},
    {"restrict", TokenKind::KwRestrict},
    {"return", TokenKind::KwReturn},
    {"sizeof", TokenKind::KwSizeof},
    {"static_assert", TokenKind::KwStaticAssert},
    {"struct", TokenKind::KwStruct},
    {"switch", TokenKind::KwSwitch},
    {"test", TokenKind::KwTest},
    {"thread_local", TokenKind::KwThreadLocal},
    {"true", TokenKind::KwTrue},
    {"trait", TokenKind::KwTrait},
    {"type", TokenKind::KwType},
    {"typeof", TokenKind::KwTypeof},
    {"typeof_unqual", TokenKind::KwTypeofUnqual},
    {"union", TokenKind::KwUnion},
    {"unsafe", TokenKind::KwUnsafe},
    {"var", TokenKind::KwVar},
    {"void", TokenKind::KwVoid},
    {"volatile", TokenKind::KwVolatile},
    {"while", TokenKind::KwWhile},
};

bool is_ident_start(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool is_ident_continue(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

int digit_value(char c) {
    const int hex = hex_value(c);
    return hex;
}

TokenKind keyword_or_ident(std::string_view spelling) {
    for (const auto &[name, kind] : kKeywords) {
        if (name == spelling) {
            return kind;
        }
    }
    return TokenKind::Identifier;
}

} // namespace

const char *token_kind_name(TokenKind kind) {
    switch (kind) {
    case TokenKind::End: return "eof";
    case TokenKind::Identifier: return "ident";
    case TokenKind::IntLiteral: return "int-lit";
    case TokenKind::FloatLiteral: return "float";
    case TokenKind::StringLiteral: return "string";
    case TokenKind::CharLiteral: return "char";
    case TokenKind::KwAlignas: return "alignas";
    case TokenKind::KwAssert: return "assert";
    case TokenKind::KwAlignof: return "alignof";
    case TokenKind::KwAuto: return "auto";
    case TokenKind::KwBool: return "bool";
    case TokenKind::KwBreak: return "break";
    case TokenKind::KwCase: return "case";
    case TokenKind::KwChar: return "char-kw";
    case TokenKind::KwConst: return "const";
    case TokenKind::KwConstexpr: return "constexpr";
    case TokenKind::KwContinue: return "continue";
    case TokenKind::KwDefault: return "default";
    case TokenKind::KwDefer: return "defer";
    case TokenKind::KwDelete: return "delete";
    case TokenKind::KwDo: return "do";
    case TokenKind::KwDouble: return "double";
    case TokenKind::KwElse: return "else";
    case TokenKind::KwEnum: return "enum";
    case TokenKind::KwExport: return "export";
    case TokenKind::KwExtern: return "extern";
    case TokenKind::KwFalse: return "false";
    case TokenKind::KwFloat: return "float-kw";
    case TokenKind::KwFor: return "for";
    case TokenKind::KwFunc: return "func";
    case TokenKind::KwGoto: return "goto";
    case TokenKind::KwIf: return "if";
    case TokenKind::KwImport: return "import";
    case TokenKind::KwInline: return "inline";
    case TokenKind::KwInt: return "int";
    case TokenKind::KwLong: return "long";
    case TokenKind::KwComptime: return "comptime";
    case TokenKind::KwEmit: return "emit";
    case TokenKind::KwNew: return "new";
    case TokenKind::KwNull: return "null";
    case TokenKind::KwPackage: return "package";
    case TokenKind::KwRegister: return "register";
    case TokenKind::KwRestrict: return "restrict";
    case TokenKind::KwReturn: return "return";
    case TokenKind::KwShort: return "short";
    case TokenKind::KwSigned: return "signed";
    case TokenKind::KwSizeof: return "sizeof";
    case TokenKind::KwStatic: return "static";
    case TokenKind::KwStaticAssert: return "static_assert";
    case TokenKind::KwStruct: return "struct";
    case TokenKind::KwSwitch: return "switch";
    case TokenKind::KwTest: return "test";
    case TokenKind::KwThreadLocal: return "thread_local";
    case TokenKind::KwTrue: return "true";
    case TokenKind::KwTrait: return "trait";
    case TokenKind::KwType: return "type";
    case TokenKind::KwTypeof: return "typeof";
    case TokenKind::KwTypeofUnqual: return "typeof_unqual";
    case TokenKind::KwUnion: return "union";
    case TokenKind::KwUnsigned: return "unsigned";
    case TokenKind::KwUnsafe: return "unsafe";
    case TokenKind::KwVar: return "var";
    case TokenKind::KwVoid: return "void";
    case TokenKind::KwVolatile: return "volatile";
    case TokenKind::KwWhile: return "while";
    case TokenKind::KwUAlignas: return "_Alignas";
    case TokenKind::KwUAlignof: return "_Alignof";
    case TokenKind::KwAtomic: return "_Atomic";
    case TokenKind::KwBitInt: return "_BitInt";
    case TokenKind::KwUBool: return "_Bool";
    case TokenKind::KwComplex: return "_Complex";
    case TokenKind::KwDecimal128: return "_Decimal128";
    case TokenKind::KwDecimal32: return "_Decimal32";
    case TokenKind::KwDecimal64: return "_Decimal64";
    case TokenKind::KwGeneric: return "_Generic";
    case TokenKind::KwImaginary: return "_Imaginary";
    case TokenKind::KwNoreturn: return "_Noreturn";
    case TokenKind::KwUStaticAssert: return "_Static_assert";
    case TokenKind::KwUThreadLocal: return "_Thread_local";
    case TokenKind::LParen: return "(";
    case TokenKind::RParen: return ")";
    case TokenKind::LBrace: return "{";
    case TokenKind::RBrace: return "}";
    case TokenKind::LBracket: return "[";
    case TokenKind::RBracket: return "]";
    case TokenKind::Comma: return ",";
    case TokenKind::Semicolon: return ";";
    case TokenKind::Dot: return ".";
    case TokenKind::Arrow: return "->";
    case TokenKind::Ellipsis: return "...";
    case TokenKind::Question: return "?";
    case TokenKind::Colon: return ":";
    case TokenKind::ColonColon: return "::";
    case TokenKind::ColonEq: return ":=";
    case TokenKind::Plus: return "+";
    case TokenKind::Minus: return "-";
    case TokenKind::Star: return "*";
    case TokenKind::Slash: return "/";
    case TokenKind::Percent: return "%";
    case TokenKind::Amp: return "&";
    case TokenKind::Pipe: return "|";
    case TokenKind::Caret: return "^";
    case TokenKind::Tilde: return "~";
    case TokenKind::Bang: return "!";
    case TokenKind::Eq: return "=";
    case TokenKind::EqEq: return "==";
    case TokenKind::BangEq: return "!=";
    case TokenKind::Lt: return "<";
    case TokenKind::Gt: return ">";
    case TokenKind::Le: return "<=";
    case TokenKind::Ge: return ">=";
    case TokenKind::AmpAmp: return "&&";
    case TokenKind::PipePipe: return "||";
    case TokenKind::LtLt: return "<<";
    case TokenKind::GtGt: return ">>";
    case TokenKind::PlusPlus: return "++";
    case TokenKind::MinusMinus: return "--";
    case TokenKind::PlusEq: return "+=";
    case TokenKind::MinusEq: return "-=";
    case TokenKind::StarEq: return "*=";
    case TokenKind::SlashEq: return "/=";
    case TokenKind::PercentEq: return "%=";
    case TokenKind::AmpEq: return "&=";
    case TokenKind::PipeEq: return "|=";
    case TokenKind::CaretEq: return "^=";
    case TokenKind::LtLtEq: return "<<=";
    case TokenKind::GtGtEq: return ">>=";
    }
    return "?";
}

Lexer::Lexer(const SourceFile &file, DiagnosticEngine &diags) : file_(file), diags_(diags) {}

bool Lexer::eof_at(uint32_t ahead) const {
    return static_cast<uint64_t>(pos_) + ahead >= file_.text().size();
}

char Lexer::peek(uint32_t ahead) const {
    if (eof_at(ahead)) {
        return '\0';
    }
    return file_.text()[pos_ + ahead];
}

char Lexer::get() {
    if (eof_at()) {
        return '\0';
    }
    return file_.text()[pos_++];
}

void Lexer::error_at(uint32_t offset, const std::string &message) {
    diags_.error(SourceLoc{file_.id(), offset}, message);
}

Token Lexer::finish(TokenKind kind, uint32_t start) const {
    Token token;
    token.kind = kind;
    token.loc = SourceLoc{file_.id(), start};
    token.length = pos_ - start;
    return token;
}

void Lexer::skip_line_comment() {
    while (!eof_at() && peek() != '\n') {
        get();
    }
}

void Lexer::skip_block_comment(uint32_t start) {
    while (!eof_at()) {
        if (peek() == '*' && peek(1) == '/') {
            get();
            get();
            return;
        }
        get();
    }
    error_at(start, "unterminated comment");
}

void Lexer::skip_directive(uint32_t start) {
    error_at(start, "preprocessor directives are not supported");
    while (!eof_at()) {
        if (peek() == '\\' && peek(1) == '\n') {
            get();
            get();
            continue;
        }
        if (peek() == '\n') {
            get();
            break;
        }
        get();
    }
    only_whitespace_since_newline_ = true;
}

void Lexer::skip_trivia() {
    while (!eof_at()) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f') {
            get();
            continue;
        }
        if (c == '\n') {
            get();
            only_whitespace_since_newline_ = true;
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            get();
            get();
            skip_line_comment();
            continue;
        }
        if (c == '/' && peek(1) == '*') {
            const uint32_t start = pos_;
            get();
            get();
            skip_block_comment(start);
            continue;
        }
        break;
    }
}

bool Lexer::looks_like_float(uint32_t start) const {
    uint32_t i = start;
    const std::string &text = file_.text();
    const uint32_t n = static_cast<uint32_t>(text.size());
    auto at = [&](uint32_t k) -> char { return k < n ? text[k] : '\0'; };
    if (at(i) == '0' && (at(i + 1) == 'x' || at(i + 1) == 'X')) {
        i += 2;
        while (i < n) {
            if (text[i] == '\'') {
                ++i;
                continue;
            }
            if (hex_value(text[i]) < 0) {
                break;
            }
            ++i;
        }
        bool dot = false;
        if (i < n && text[i] == '.' && at(i + 1) != '.') {
            dot = true;
            ++i;
            while (i < n && (hex_value(text[i]) >= 0 || text[i] == '\'')) {
                ++i;
            }
        }
        if (i < n && (text[i] == 'p' || text[i] == 'P')) {
            return true;
        }
        return dot;
    }
    if (at(i) == '0' && (at(i + 1) == 'b' || at(i + 1) == 'B')) {
        return false;
    }
    while (i < n && (is_digit(text[i]) || text[i] == '\'')) {
        ++i;
    }
    if (i < n && text[i] == '.' && at(i + 1) != '.') {
        return true;
    }
    if (i < n && (text[i] == 'e' || text[i] == 'E')) {
        return true;
    }
    return false;
}

Token Lexer::lex_float(uint32_t start) {
    const bool hex = peek() == '0' && (peek(1) == 'x' || peek(1) == 'X');
    auto consume_digits = [&](bool hexadecimal) {
        bool any = false;
        bool pending_sep = false;
        while (!eof_at()) {
            if (peek() == '\'') {
                if (!any || pending_sep) {
                    error_at(pos_, "invalid digit separator");
                }
                get();
                pending_sep = true;
                continue;
            }
            const int value = hexadecimal ? hex_value(peek()) : (is_digit(peek()) ? peek() - '0' : -1);
            if (value < 0) {
                break;
            }
            any = true;
            pending_sep = false;
            get();
        }
        if (pending_sep) {
            error_at(pos_ - 1, "trailing digit separator");
        }
        return any;
    };

    if (hex) {
        get();
        get();
        const bool before = consume_digits(true);
        bool after = false;
        if (peek() == '.' && peek(1) != '.') {
            get();
            after = consume_digits(true);
        }
        if (!before && !after) {
            error_at(start, "missing digits in float literal");
        }
        if (peek() == 'p' || peek() == 'P') {
            get();
            if (peek() == '+' || peek() == '-') {
                get();
            }
            if (!consume_digits(false)) {
                error_at(start, "exponent has no digits");
            }
        } else {
            error_at(start, "hex float requires an exponent");
        }
    } else if (peek() == '.') {
        get();
        if (!consume_digits(false)) {
            error_at(start, "missing digits in float literal");
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') {
                get();
            }
            if (!consume_digits(false)) {
                error_at(start, "exponent has no digits");
            }
        }
    } else {
        if (!consume_digits(false)) {
            error_at(start, "missing digits in float literal");
        }
        if (peek() == '.' && peek(1) != '.') {
            get();
            consume_digits(false);
        }
        if (peek() == 'e' || peek() == 'E') {
            get();
            if (peek() == '+' || peek() == '-') {
                get();
            }
            if (!consume_digits(false)) {
                error_at(start, "exponent has no digits");
            }
        }
    }
    if (peek() == 'f' || peek() == 'F') {
        get();
    }
    Token token = finish(TokenKind::FloatLiteral, start);
    token.text = std::string(file_.slice(start, pos_ - start));
    return token;
}

Token Lexer::lex_number_or_float(uint32_t start) {
    if (looks_like_float(start)) {
        return lex_float(start);
    }

    int base = 10;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        base = 16;
        get();
        get();
    } else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B')) {
        base = 2;
        get();
        get();
    } else if (peek() == '0') {
        base = 8;
    }

    bool saw_digit = false;
    bool pending_sep = false;
    bool overflow = false;
    uint64_t value = 0;
    while (!eof_at()) {
        if (peek() == '\'') {
            if (!saw_digit || pending_sep) {
                error_at(pos_, "invalid digit separator");
            }
            const int next = digit_value(peek(1));
            if (next < 0 || next >= base) {
                error_at(pos_, "trailing digit separator");
                get();
                break;
            }
            get();
            pending_sep = true;
            continue;
        }
        const int digit = base == 16 ? hex_value(peek()) : (is_digit(peek()) ? peek() - '0' : -1);
        if (digit < 0 || digit >= base) {
            break;
        }
        saw_digit = true;
        pending_sep = false;
        get();
        if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) {
            overflow = true;
        } else {
            value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
        }
    }
    if (!saw_digit) {
        error_at(start, "missing digits in integer literal");
    }
    if (base == 8 || base == 2 || base == 16) {
        const int extra = base == 16 ? hex_value(peek()) : (is_digit(peek()) ? peek() - '0' : -1);
        if (extra >= base) {
            error_at(pos_, "invalid digit in integer literal");
            while (!eof_at() && (is_digit(peek()) || peek() == '\'' || hex_value(peek()) >= 0)) {
                get();
            }
        }
    }
    if (overflow) {
        error_at(start, "integer literal overflow");
    }

    IntSuffix suffix;
    bool bad_suffix = false;
    struct Suffix {
        const char *text;
        int bits;
        bool is_unsigned;
    };
    static constexpr Suffix kSuffixes[] = {
        {"i8", 8, false},  {"u8", 8, true},  {"i16", 16, false}, {"u16", 16, true},
        {"i32", 32, false}, {"u32", 32, true}, {"i64", 64, false}, {"u64", 64, true},
    };
    bool matched = false;
    if (!eof_at() && is_ident_start(peek())) {
        for (const Suffix &candidate : kSuffixes) {
            uint32_t index = 0;
            bool same = true;
            for (; candidate.text[index] != '\0'; ++index) {
                if (eof_at(index) || peek(index) != candidate.text[index]) {
                    same = false;
                    break;
                }
            }
            if (!same || (!eof_at(index) && is_ident_continue(peek(index)))) {
                continue;
            }
            for (uint32_t step = 0; step < index; ++step) {
                get();
            }
            suffix.bits = candidate.bits;
            suffix.is_unsigned = candidate.is_unsigned;
            matched = true;
            break;
        }
        if (!matched) {
            bad_suffix = true;
            while (!eof_at() && is_ident_continue(peek())) {
                get();
            }
        }
    }
    if (bad_suffix) {
        error_at(start, "invalid integer suffix");
    }

    Token token = finish(TokenKind::IntLiteral, start);
    token.int_value = value;
    token.suffix = suffix;
    return token;
}

Token Lexer::lex_string(uint32_t start) {
    get();
    std::string decoded;
    bool closed = false;
    while (!eof_at()) {
        const char c = peek();
        if (c == '\n') {
            break;
        }
        get();
        if (c == '"') {
            closed = true;
            break;
        }
        if (c == '\\') {
            if (eof_at() || peek() == '\n') {
                error_at(start, "unterminated string");
                break;
            }
            const uint32_t esc = pos_ - 1;
            const char e = get();
            switch (e) {
            case '\\': decoded.push_back('\\'); break;
            case '\'': decoded.push_back('\''); break;
            case '"': decoded.push_back('"'); break;
            case 'n': decoded.push_back('\n'); break;
            case 't': decoded.push_back('\t'); break;
            case 'r': decoded.push_back('\r'); break;
            case 'a': decoded.push_back('\a'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            case 'v': decoded.push_back('\v'); break;
            case '?': decoded.push_back('?'); break;
            case 'x': {
                if (hex_value(peek()) < 0) {
                    error_at(esc, "invalid hex escape");
                    break;
                }
                uint32_t value = 0;
                bool big = false;
                while (hex_value(peek()) >= 0) {
                    const int digit = hex_value(get());
                    if (value > 0xFFFFFF) {
                        big = true;
                    }
                    value = (value << 4) | static_cast<uint32_t>(digit);
                }
                if (big || value > 0xFF) {
                    error_at(esc, "hex escape out of range");
                    value &= 0xFF;
                }
                decoded.push_back(static_cast<char>(value));
                break;
            }
            default:
                if (e >= '0' && e <= '7') {
                    uint32_t value = static_cast<uint32_t>(e - '0');
                    for (int n = 0; n < 2; ++n) {
                        if (peek() < '0' || peek() > '7') {
                            break;
                        }
                        value = (value << 3) | static_cast<uint32_t>(get() - '0');
                    }
                    decoded.push_back(static_cast<char>(value));
                } else {
                    error_at(esc, "unknown escape");
                    decoded.push_back(e);
                }
                break;
            }
            continue;
        }
        decoded.push_back(c);
    }
    if (!closed) {
        error_at(start, "unterminated string");
    }
    Token token = finish(TokenKind::StringLiteral, start);
    token.text = std::move(decoded);
    return token;
}

Token Lexer::lex_raw_string(uint32_t start) {
    get();
    std::string decoded;
    bool closed = false;
    while (!eof_at()) {
        const char c = get();
        if (c == '`') {
            closed = true;
            break;
        }
        // A CRLF line break is one newline in the string.
        if (c == '\r' && !eof_at() && peek() == '\n') {
            continue;
        }
        decoded.push_back(c);
    }
    if (!closed) {
        error_at(start, "unterminated string");
    }
    Token token = finish(TokenKind::StringLiteral, start);
    token.text = std::move(decoded);
    return token;
}

Token Lexer::lex_char(uint32_t start) {
    get();
    uint64_t value = 0;
    bool closed = false;
    bool any = false;
    if (!eof_at() && peek() != '\n' && peek() != '\'') {
        any = true;
        char c = get();
        if (c == '\\') {
            if (eof_at() || peek() == '\n') {
                error_at(start, "unterminated character constant");
            } else {
                const char e = get();
                switch (e) {
                case '\\': value = '\\'; break;
                case '\'': value = '\''; break;
                case '"': value = '"'; break;
                case 'n': value = '\n'; break;
                case 't': value = '\t'; break;
                case 'r': value = '\r'; break;
                case 'a': value = '\a'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'v': value = '\v'; break;
                case '?': value = '?'; break;
                case 'x': {
                    if (hex_value(peek()) < 0) {
                        error_at(start, "invalid hex escape");
                        break;
                    }
                    while (hex_value(peek()) >= 0) {
                        value = (value << 4) | static_cast<uint64_t>(hex_value(get()));
                    }
                    break;
                }
                default:
                    if (e >= '0' && e <= '7') {
                        value = static_cast<uint64_t>(e - '0');
                        for (int n = 0; n < 2; ++n) {
                            if (peek() < '0' || peek() > '7') {
                                break;
                            }
                            value = (value << 3) | static_cast<uint64_t>(get() - '0');
                        }
                    } else {
                        error_at(start, "unknown escape");
                        value = static_cast<unsigned char>(e);
                    }
                    break;
                }
            }
        } else {
            value = static_cast<unsigned char>(c);
        }
    }
    if (!eof_at() && peek() == '\'') {
        get();
        closed = true;
    }
    if (!closed) {
        error_at(start, "unterminated character constant");
    } else if (!any) {
        error_at(start, "empty character constant");
    }
    Token token = finish(TokenKind::CharLiteral, start);
    token.int_value = value;
    return token;
}

Token Lexer::lex_identifier(uint32_t start) {
    get();
    while (!eof_at() && is_ident_continue(peek())) {
        get();
    }
    Token token = finish(TokenKind::Identifier, start);
    token.text = std::string(file_.slice(start, pos_ - start));
    token.kind = keyword_or_ident(token.text);
    if (token.kind == TokenKind::Identifier &&
        (token.text == "u" || token.text == "U" || token.text == "L" || token.text == "u8") &&
        (peek() == '"' || peek() == '\'')) {
        error_at(start, "string prefix is not supported");
    }
    if (token.kind != TokenKind::Identifier) {
        token.text.clear();
    }
    return token;
}

Token Lexer::lex_token() {
    while (!eof_at()) {
    const uint32_t start = pos_;
    const char c = peek();
    if (is_ident_start(c)) {
        return lex_identifier(start);
    }
    if (is_digit(c) || (c == '.' && is_digit(peek(1)))) {
        return lex_number_or_float(start);
    }
    if (c == '"') {
        return lex_string(start);
    }
    if (c == '`') {
        return lex_raw_string(start);
    }
    if (c == '\'') {
        return lex_char(start);
    }
    if (c == '\\') {
        error_at(start, "universal character names are not supported");
        get();
        continue;
    }

    get();
    switch (c) {
    case '(': return finish(TokenKind::LParen, start);
    case ')': return finish(TokenKind::RParen, start);
    case '{': return finish(TokenKind::LBrace, start);
    case '}': return finish(TokenKind::RBrace, start);
    case '[': return finish(TokenKind::LBracket, start);
    case ']': return finish(TokenKind::RBracket, start);
    case ',': return finish(TokenKind::Comma, start);
    case ';': return finish(TokenKind::Semicolon, start);
    case '?': return finish(TokenKind::Question, start);
    case ':':
        if (peek() == ':') {
            get();
            return finish(TokenKind::ColonColon, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::ColonEq, start);
        }
        return finish(TokenKind::Colon, start);
    case '~': return finish(TokenKind::Tilde, start);
    case '.':
        if (peek() == '.' && peek(1) == '.') {
            get();
            get();
            return finish(TokenKind::Ellipsis, start);
        }
        return finish(TokenKind::Dot, start);
    case '+':
        if (peek() == '+') {
            get();
            return finish(TokenKind::PlusPlus, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::PlusEq, start);
        }
        return finish(TokenKind::Plus, start);
    case '-':
        if (peek() == '-') {
            get();
            return finish(TokenKind::MinusMinus, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::MinusEq, start);
        }
        if (peek() == '>') {
            get();
            return finish(TokenKind::Arrow, start);
        }
        return finish(TokenKind::Minus, start);
    case '*':
        if (peek() == '=') {
            get();
            return finish(TokenKind::StarEq, start);
        }
        return finish(TokenKind::Star, start);
    case '/':
        if (peek() == '=') {
            get();
            return finish(TokenKind::SlashEq, start);
        }
        return finish(TokenKind::Slash, start);
    case '%':
        if (peek() == '=') {
            get();
            return finish(TokenKind::PercentEq, start);
        }
        return finish(TokenKind::Percent, start);
    case '&':
        if (peek() == '&') {
            get();
            return finish(TokenKind::AmpAmp, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::AmpEq, start);
        }
        return finish(TokenKind::Amp, start);
    case '|':
        if (peek() == '|') {
            get();
            return finish(TokenKind::PipePipe, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::PipeEq, start);
        }
        return finish(TokenKind::Pipe, start);
    case '^':
        if (peek() == '=') {
            get();
            return finish(TokenKind::CaretEq, start);
        }
        return finish(TokenKind::Caret, start);
    case '!':
        if (peek() == '=') {
            get();
            return finish(TokenKind::BangEq, start);
        }
        return finish(TokenKind::Bang, start);
    case '=':
        if (peek() == '=') {
            get();
            return finish(TokenKind::EqEq, start);
        }
        return finish(TokenKind::Eq, start);
    case '<':
        if (peek() == '<') {
            get();
            if (peek() == '=') {
                get();
                return finish(TokenKind::LtLtEq, start);
            }
            return finish(TokenKind::LtLt, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::Le, start);
        }
        return finish(TokenKind::Lt, start);
    case '>':
        if (peek() == '>') {
            get();
            if (peek() == '=') {
                get();
                return finish(TokenKind::GtGtEq, start);
            }
            return finish(TokenKind::GtGt, start);
        }
        if (peek() == '=') {
            get();
            return finish(TokenKind::Ge, start);
        }
        return finish(TokenKind::Gt, start);
    default:
        error_at(start, "unexpected character");
        break;
    }
    }
    return finish(TokenKind::End, pos_);
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> tokens;
    while (true) {
        skip_trivia();
        if (eof_at()) {
            tokens.push_back(finish(TokenKind::End, pos_));
            break;
        }
        if (peek() == '#' && only_whitespace_since_newline_) {
            const uint32_t start = pos_;
            skip_directive(start);
            continue;
        }
        const uint32_t before = pos_;
        tokens.push_back(lex_token());
        only_whitespace_since_newline_ = false;
        if (pos_ == before) {
            error_at(pos_, "lexer made no progress");
            get();
        }
        if (tokens.back().kind == TokenKind::End) {
            break;
        }
    }
    return tokens;
}

} // namespace tlc
