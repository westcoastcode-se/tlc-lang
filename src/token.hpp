#pragma once

#include "source.hpp"

#include <cstdint>
#include <string>

namespace tlc {

enum class TokenKind {
    End,
    Identifier,
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    CharLiteral,

    KwAlignas,
    KwAlignof,
    KwAuto,
    KwBool,
    KwBreak,
    KwCase,
    KwChar,
    KwConst,
    KwConstexpr,
    KwContinue,
    KwDefault,
    KwDefer,
    KwDo,
    KwDouble,
    KwElse,
    KwEnum,
    KwExport,
    KwExtern,
    KwFalse,
    KwFloat,
    KwFor,
    KwFunc,
    KwGoto,
    KwIf,
    KwImport,
    KwInline,
    KwInt,
    KwLong,
    KwNull,
    KwPackage,
    KwRegister,
    KwRestrict,
    KwReturn,
    KwShort,
    KwSigned,
    KwSizeof,
    KwStatic,
    KwStaticAssert,
    KwStruct,
    KwSwitch,
    KwThreadLocal,
    KwTrue,
    KwTrait,
    KwType,
    KwTypeof,
    KwTypeofUnqual,
    KwUnion,
    KwUnsigned,
    KwVar,
    KwVoid,
    KwVolatile,
    KwWhile,
    KwUAlignas,
    KwUAlignof,
    KwAtomic,
    KwBitInt,
    KwUBool,
    KwComplex,
    KwDecimal128,
    KwDecimal32,
    KwDecimal64,
    KwGeneric,
    KwImaginary,
    KwNoreturn,
    KwUStaticAssert,
    KwUThreadLocal,

    LParen,
    RParen,
    LBrace,
    RBrace,
    LBracket,
    RBracket,
    Comma,
    Semicolon,
    Dot,
    Arrow,
    Ellipsis,
    Question,
    Colon,
    ColonColon,
    ColonEq,
    Plus,
    Minus,
    Star,
    Slash,
    Percent,
    Amp,
    Pipe,
    Caret,
    Tilde,
    Bang,
    Eq,
    EqEq,
    BangEq,
    Lt,
    Gt,
    Le,
    Ge,
    AmpAmp,
    PipePipe,
    LtLt,
    GtGt,
    PlusPlus,
    MinusMinus,
    PlusEq,
    MinusEq,
    StarEq,
    SlashEq,
    PercentEq,
    AmpEq,
    PipeEq,
    CaretEq,
    LtLtEq,
    GtGtEq,
};

struct IntSuffix {
    // 0 means the literal has no suffix and its type is int.
    int bits = 0;
    bool is_unsigned = false;
};

struct Token {
    TokenKind kind = TokenKind::End;
    SourceLoc loc{};
    uint32_t length = 0;
    std::string text;
    uint64_t int_value = 0;
    IntSuffix suffix{};
};

const char *token_kind_name(TokenKind kind);

} // namespace tlc
