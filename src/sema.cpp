#include "sema.hpp"

#include <cstdint>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace tlc {
namespace {

struct Symbol {
    std::string name;
    Type type;
    bool has_body = false;
    bool is_function = false;
    bool is_local_function = false;
    bool is_auto = false;
    bool is_package = false;
    bool is_type = false;
    bool is_constant = false;
    bool is_enum = false;
    bool is_typedef = false;
    bool is_struct_tag = false;
    int const_value = 0;
    int function_id = 0;
    uint32_t package_file = 0;
    SourceLoc loc{};
};

struct Scope {
    std::unordered_map<std::string, Symbol> symbols;
    int function_id = 0;
};

class Checker {
  public:
    explicit Checker(DiagnosticEngine &diags) : diags_(diags) {}

    void analyze(const TranslationUnit &unit);
    void analyze_graph(const PackageGraph &graph);

  private:
    struct SelectorTarget {
        enum class Kind { Failed, Package, Other };
        Kind kind = Kind::Other;
        const Symbol *symbol = nullptr;
    };

    DiagnosticEngine &diags_;
    const PackageGraph *graph_ = nullptr;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> exports_;
    struct StructInfo {
        std::string name;
        bool complete = false;
        struct Field {
            std::string name;
            Type type;
        };
        std::vector<Field> fields;
        bool forward = false;
    };

    struct EnumConst {
        int value = 0;
        SourceLoc loc{};
    };

    struct MethodInfo {
        Type type;
        bool has_body = false;
    };

    struct TraitInfo {
        std::string name;
        struct Method {
            std::string name;
            Type type;
        };
        std::vector<Method> methods;
    };

    std::vector<Scope> scopes_;
    int function_seq_ = 0;
    std::vector<StructInfo> structs_;
    std::vector<TraitInfo> traits_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::unordered_map<std::string, MethodInfo>>> methods_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> tags_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, int>> trait_names_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Type>> typedefs_;
    std::unordered_map<uint32_t, std::unordered_set<std::string>> enums_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, EnumConst>> enum_consts_;
    struct PendingStruct {
        uint32_t file_id = 0;
        const StructDecl *decl = nullptr;
        int id = -1;
    };
    std::vector<PendingStruct> pending_structs_;
    std::unordered_map<std::string, std::unordered_map<std::string, Symbol>> package_syms_;
    std::unordered_map<uint32_t, std::unordered_map<std::string, Symbol>> statics_;
    struct SwitchFrame {
        std::unordered_set<int> values;
        bool has_default = false;
    };

    uint32_t file_id_ = 0;
    Type return_type_ = type_invalid();
    int loop_depth_ = 0;
    bool checking_defer_ = false;
    std::vector<SwitchFrame> switches_;

    void error(SourceLoc loc, const std::string &message);
    void push();
    void push_function();
    void pop();
    bool captured(const Symbol &symbol) const;
    Type function_result(const FunctionDecl &fn);
    Type function_signature(const FunctionDecl &fn);
    void check_local_function(const FunctionDecl &fn);
    Symbol *lookup(const std::string &name);
    Symbol *lookup_scope(const std::string &name);
    bool declare(Symbol symbol, SourceLoc loc);
    bool remember_shared(const Symbol &symbol, SourceLoc loc);
    void note_body(const std::string &name);
    bool shared_has(const std::string &name) const;
    std::vector<uint32_t> files_in_package(uint32_t file_id) const;
    int find_tag(uint32_t file, const std::string &name) const;
    bool package_has_enum(uint32_t file, const std::string &name) const;
    bool package_enum_const(uint32_t file, const std::string &name) const;
    int package_trait(uint32_t file, const std::string &name) const;
    bool package_typedef(uint32_t file, const std::string &name, Type &out) const;
    bool exported_elsewhere(const std::string &name) const;
    bool typedef_target_ready(const TypedefDecl &item) const;
    void register_one_typedef(const TypedefDecl &item);
    bool field_waits(const FieldDecl &field) const;
    void declare_struct_tags(const TranslationUnit &unit, uint32_t file_id);
    void define_struct_fields();
    void prepare_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);
    void check_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);

    Type type_of_name(const std::string &name, SourceLoc loc, bool allow_incomplete = false);
    Type with_indirections(Type type, int count, SourceLoc loc);
    void note_struct_use(bool is_struct, const std::string &name, SourceLoc loc);
    Type object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                     bool parameter, const std::string &package = {}, bool is_struct = false, bool is_enum = false,
                     bool receiver = false);
    Type spelled_type(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                      int indirections, SourceLoc loc);
    Type lookup_tag(const std::string &name, SourceLoc loc, bool allow_incomplete);
    Type lookup_qualified_type(const std::string &package, const std::string &name, SourceLoc loc);
    bool lookup_field(const Type &object, const std::string &name, Type &out) const;
    bool dot_object(const Type &base, Type &object, bool &through_pointer) const;
    const MethodInfo *find_method(int struct_id, const std::string &name) const;
    const TraitInfo::Method *find_trait_method(int trait_id, const std::string &name) const;
    bool trait_method_matches(const Type &trait_fn, const Type &concrete) const;
    void require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc);
    void register_structs(const TranslationUnit &unit, uint32_t file_id);
    void register_enums(const TranslationUnit &unit, uint32_t file_id);
    void register_traits(const TranslationUnit &unit, uint32_t file_id);
    void register_typedefs(const TranslationUnit &unit, uint32_t file_id);
    void inject_enum_consts();
    bool name_taken(const std::string &name) const;
    Type published_object(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum);
    Type published_result(const std::string &name, int indirections, SourceLoc loc, const std::string &package,
                          bool is_struct, bool is_enum);
    bool is_null_constant(const Expr &expr) const;
    void expect(const Type &got, const Type &want, SourceLoc loc);
    void expect_value(const Type &got, const Type &want, const Expr &expr, SourceLoc loc);
    void expect_integer(const Type &got, SourceLoc loc);
    bool is_lvalue_form(const Expr &expr) const;
    Type check_raw(const Expr &expr, bool used);
    Type check_expr(const Expr &expr, bool used);
    void check_stmt(const Stmt &stmt);
    std::optional<int> fold_int(const Expr &expr);
    void check_function(const FunctionDecl &fn);
    void declare_globals(const TranslationUnit &unit);
    void check_globals(const TranslationUnit &unit);
    void publish_exports(const TranslationUnit &unit, uint32_t file_id);
    void bind_imports(const PackageNode &node);
    void analyze_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node);
    const PackageNode *find_node(uint32_t file_id) const;
    const Symbol *find_export(uint32_t file_id, const std::string &name) const;
    const PackageNode *target_of(const PackageNode &from, const ImportDecl &import) const;
    SelectorTarget resolve_selector(const Expr::Selector &selector);
};

} // namespace

Type type_invalid() { return Type{}; }

Type type_void() {
    Type type;
    type.kind = TypeKind::Void;
    return type;
}

Type type_int() {
    Type type;
    type.kind = TypeKind::Int;
    type.rank = 3;
    return type;
}

Type type_char() {
    Type type;
    type.kind = TypeKind::Char;
    type.rank = 1;
    type.plain_char = true;
    return type;
}

Type type_rank(int rank, bool is_unsigned) {
    Type type;
    type.kind = rank <= 1 ? TypeKind::Char : TypeKind::Int;
    type.rank = rank;
    type.is_unsigned = is_unsigned;
    type.plain_char = false;
    return type;
}

Type type_bool() {
    Type type;
    type.kind = TypeKind::Bool;
    return type;
}

Type type_nullptr() {
    Type type;
    type.kind = TypeKind::Nullptr;
    return type;
}

Type type_pointer(Type pointee) {
    Type type;
    type.kind = TypeKind::Pointer;
    type.pointee = std::make_shared<Type>(std::move(pointee));
    return type;
}

Type type_array(Type element, int length) {
    Type type;
    type.kind = TypeKind::Array;
    type.element = std::make_shared<Type>(std::move(element));
    type.length = length;
    return type;
}

Type type_struct(int id) {
    Type type;
    type.kind = TypeKind::Struct;
    type.struct_id = id;
    return type;
}

Type type_trait(int id) {
    Type type;
    type.kind = TypeKind::Trait;
    type.trait_id = id;
    return type;
}

bool mentions_trait(const Type &type) {
    if (type.kind == TypeKind::Trait) {
        return true;
    }
    if (type.kind == TypeKind::Pointer && type.pointee && type.pointee->kind == TypeKind::Trait) {
        return true;
    }
    if (type.kind == TypeKind::Tuple) {
        for (const Type &part : type.results) {
            if (mentions_trait(part)) {
                return true;
            }
        }
    }
    return false;
}

Type type_function(Type result, std::vector<Type> params, bool variadic) {
    Type type;
    type.kind = TypeKind::Function;
    type.result = std::make_shared<Type>(std::move(result));
    type.params = std::move(params);
    type.variadic = variadic;
    return type;
}

Type type_tuple(std::vector<Type> results) {
    Type type;
    type.kind = TypeKind::Tuple;
    type.results = std::move(results);
    return type;
}

bool same_type(const Type &lhs, const Type &rhs) {
    if (lhs.kind != rhs.kind) {
        return false;
    }
    if (lhs.kind == TypeKind::Pointer) {
        return lhs.pointee && rhs.pointee && same_type(*lhs.pointee, *rhs.pointee);
    }
    if (lhs.kind == TypeKind::Array) {
        return lhs.length == rhs.length && lhs.element && rhs.element && same_type(*lhs.element, *rhs.element);
    }
    if (lhs.kind == TypeKind::Struct) {
        return lhs.struct_id >= 0 && lhs.struct_id == rhs.struct_id;
    }
    if (lhs.kind == TypeKind::Trait) {
        return lhs.trait_id >= 0 && lhs.trait_id == rhs.trait_id;
    }
    if (lhs.kind == TypeKind::Int || lhs.kind == TypeKind::Char) {
        return lhs.rank == rhs.rank && lhs.is_unsigned == rhs.is_unsigned && lhs.plain_char == rhs.plain_char;
    }
    if (lhs.kind == TypeKind::Tuple) {
        if (lhs.results.size() != rhs.results.size()) {
            return false;
        }
        for (std::size_t i = 0; i < lhs.results.size(); ++i) {
            if (!same_type(lhs.results[i], rhs.results[i])) {
                return false;
            }
        }
        return true;
    }
    if (lhs.kind != TypeKind::Function) {
        return true;
    }
    if (lhs.params.size() != rhs.params.size()) {
        return false;
    }
    if (!lhs.result || !rhs.result || !same_type(*lhs.result, *rhs.result)) {
        return false;
    }
    if (lhs.variadic != rhs.variadic) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.params.size(); ++i) {
        if (!same_type(lhs.params[i], rhs.params[i])) {
            return false;
        }
    }
    return true;
}

namespace {

bool fill_builtin(const std::string &name, Type &out) {
    if (name == "int" || name == "signed") {
        out = type_int();
        return true;
    }
    if (name == "char") {
        out = type_char();
        return true;
    }
    if (name == "signed char") {
        out = type_rank(1, false);
        return true;
    }
    if (name == "unsigned char") {
        out = type_rank(1, true);
        return true;
    }
    if (name == "short" || name == "signed short") {
        out = type_rank(2, false);
        return true;
    }
    if (name == "unsigned short") {
        out = type_rank(2, true);
        return true;
    }
    if (name == "unsigned" || name == "unsigned int") {
        out = type_rank(3, true);
        return true;
    }
    if (name == "long" || name == "signed long") {
        out = type_rank(4, false);
        return true;
    }
    if (name == "unsigned long") {
        out = type_rank(4, true);
        return true;
    }
    if (name == "long long" || name == "signed long long") {
        out = type_rank(5, false);
        return true;
    }
    if (name == "unsigned long long") {
        out = type_rank(5, true);
        return true;
    }
    if (name == "bool") {
        out = type_bool();
        return true;
    }
    if (name == "void") {
        out = type_void();
        return true;
    }
    return false;
}

Type quiet_type(const std::string &name) {
    Type type;
    if (fill_builtin(name, type)) {
        return type;
    }
    return type_invalid();
}

bool is_integer(const Type &type) { return type.kind == TypeKind::Int || type.kind == TypeKind::Char; }

bool is_scalar(const Type &type) { return is_integer(type) || type.kind == TypeKind::Bool; }

int rank_of(const Type &type) {
    if (type.kind == TypeKind::Bool) {
        return 0;
    }
    if (type.kind == TypeKind::Char) {
        return 1;
    }
    if (type.kind == TypeKind::Int) {
        return type.rank == 0 ? 3 : type.rank;
    }
    return -1;
}

int width_of(const Type &type) {
    switch (rank_of(type)) {
    case 0: return 1;
    case 1: return 8;
    case 2: return 16;
    case 3: return 32;
    case 4: return 64;
    case 5: return 64;
    default: return 0;
    }
}

Type promote(const Type &type) {
    if (type.kind == TypeKind::Bool || type.kind == TypeKind::Char) {
        return type_int();
    }
    if (type.kind == TypeKind::Int && rank_of(type) < 3) {
        return type_int();
    }
    return type;
}

Type usual(const Type &lhs, const Type &rhs) {
    if (lhs.kind == TypeKind::Invalid) {
        return lhs;
    }
    if (rhs.kind == TypeKind::Invalid) {
        return rhs;
    }
    if (!is_integer(lhs) || !is_integer(rhs)) {
        return type_int();
    }
    if (same_type(lhs, rhs)) {
        return lhs;
    }
    const int left_rank = rank_of(lhs);
    const int right_rank = rank_of(rhs);
    if (lhs.is_unsigned == rhs.is_unsigned) {
        return type_rank(left_rank > right_rank ? left_rank : right_rank, lhs.is_unsigned);
    }
    const Type &signed_type = lhs.is_unsigned ? rhs : lhs;
    const Type &unsigned_type = lhs.is_unsigned ? lhs : rhs;
    const int signed_rank = rank_of(signed_type);
    const int unsigned_rank = rank_of(unsigned_type);
    if (unsigned_rank >= signed_rank) {
        return type_rank(unsigned_rank, true);
    }
    if (width_of(signed_type) > width_of(unsigned_type)) {
        return type_rank(signed_rank, false);
    }
    return type_rank(signed_rank, true);
}

Type quiet_indirect(Type type, int count) {
    for (int i = 0; i < count; ++i) {
        if (type.kind != TypeKind::Int && type.kind != TypeKind::Char && type.kind != TypeKind::Bool &&
            type.kind != TypeKind::Pointer && type.kind != TypeKind::Void) {
            return type_invalid();
        }
        type = type_pointer(std::move(type));
    }
    return type;
}

Type quiet_object(const std::string &name, int indirections, bool is_array, int array_len, bool parameter) {
    Type type = quiet_indirect(quiet_type(name), indirections);
    if (!is_array) {
        return type;
    }
    if (type.kind == TypeKind::Invalid || type.kind == TypeKind::Void || type.kind == TypeKind::Function) {
        return type_invalid();
    }
    if (parameter) {
        return type_pointer(std::move(type));
    }
    if (array_len <= 0) {
        return type_invalid();
    }
    return type_array(std::move(type), array_len);
}

bool lvalue_form(const Expr &expr) {
    if (std::holds_alternative<Expr::Name>(expr.data)) {
        return true;
    }
    if (std::holds_alternative<Expr::Index>(expr.data)) {
        return true;
    }
    if (const auto *unary = std::get_if<Expr::Unary>(&expr.data)) {
        return unary->op == TokenKind::Star;
    }
    if (const auto *selector = std::get_if<Expr::Selector>(&expr.data)) {
        return selector->base &&
               (std::holds_alternative<Expr::Name>(selector->base->data) || lvalue_form(*selector->base));
    }
    return false;
}

void Checker::error(SourceLoc loc, const std::string &message) { diags_.error(loc, message); }

void Checker::push() {
    const int id = scopes_.empty() ? 0 : scopes_.back().function_id;
    scopes_.emplace_back();
    scopes_.back().function_id = id;
}

void Checker::push_function() {
    scopes_.emplace_back();
    scopes_.back().function_id = ++function_seq_;
}

void Checker::pop() {
    if (!scopes_.empty()) {
        for (const auto &[name, symbol] : scopes_.back().symbols) {
            (void)name;
            if (symbol.is_local_function && !symbol.has_body) {
                error(symbol.loc, "not supported yet");
            }
        }
    }
    scopes_.pop_back();
}

bool Checker::captured(const Symbol &symbol) const {
    if (!symbol.is_auto || scopes_.empty()) {
        return false;
    }
    return symbol.function_id != scopes_.back().function_id;
}

Type Checker::function_result(const FunctionDecl &fn) {
    if (fn.results.size() > 1) {
        std::vector<Type> parts;
        parts.reserve(fn.results.size());
        for (const ResultSlot &slot : fn.results) {
            Type part = slot.is_struct || slot.is_enum || !slot.type_package.empty()
                            ? object_type(slot.type_name, slot.indirections, false, 0, fn.loc, false, slot.type_package,
                                          slot.is_struct, slot.is_enum)
                            : with_indirections(type_of_name(slot.type_name, fn.loc, slot.indirections > 0), slot.indirections,
                                                fn.loc);
            if (part.kind == TypeKind::Void) {
                error(fn.loc, "type mismatch");
            }
            parts.push_back(std::move(part));
        }
        return type_tuple(std::move(parts));
    }
    if (fn.is_struct || fn.is_enum || !fn.type_package.empty()) {
        return object_type(fn.return_type, fn.indirections, false, 0, fn.loc, false, fn.type_package, fn.is_struct, fn.is_enum);
    }
    return with_indirections(type_of_name(fn.return_type, fn.loc, fn.indirections > 0), fn.indirections, fn.loc);
}

Type Checker::function_signature(const FunctionDecl &fn) {
    std::vector<Type> params;
    params.reserve(fn.params.size());
    for (const Param &param : fn.params) {
        params.push_back(object_type(param.type_name, param.indirections, param.is_array, param.array_len, param.loc, true,
                                     param.type_package, param.is_struct, param.is_enum, param.is_receiver));
    }
    return type_function(function_result(fn), std::move(params), fn.variadic);
}

void Checker::check_local_function(const FunctionDecl &fn) {
    if (fn.name.empty() || scopes_.empty()) {
        return;
    }
    if (!fn.method_struct.empty() || fn.exported || fn.is_static || fn.is_extern || fn.variadic) {
        error(fn.loc, "not supported yet");
    }
    bool generic = false;
    for (const Param &param : fn.params) {
        if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
            package_trait(file_id_, param.type_name) >= 0) {
            generic = true;
        }
    }
    if (generic) {
        error(fn.loc, "not supported yet");
    }
    if (!fn.method_struct.empty()) {
        return;
    }
    Type signature = function_signature(fn);
    if (signature.result != nullptr && mentions_trait(*signature.result)) {
        error(fn.loc, "not supported yet");
    }
    auto &table = scopes_.back().symbols;
    const auto found = table.find(fn.name);
    if (found != table.end()) {
        Symbol &existing = found->second;
        if (!existing.is_local_function || !same_type(existing.type, signature) ||
            (existing.has_body && fn.body != nullptr)) {
            error(fn.loc, "redefinition");
        } else if (fn.body != nullptr) {
            existing.has_body = true;
        }
    } else {
        Symbol symbol;
        symbol.name = fn.name;
        symbol.type = std::move(signature);
        symbol.is_function = true;
        symbol.is_local_function = true;
        symbol.has_body = fn.body != nullptr;
        symbol.loc = fn.loc;
        declare(std::move(symbol), fn.loc);
    }
    if (fn.body != nullptr) {
        check_function(fn);
    }
}

Symbol *Checker::lookup_scope(const std::string &name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        const auto found = it->symbols.find(name);
        if (found != it->symbols.end()) {
            return &found->second;
        }
    }
    return nullptr;
}

Symbol *Checker::lookup(const std::string &name) {
    if (Symbol *local = lookup_scope(name)) {
        return local;
    }
    if (graph_ == nullptr) {
        return nullptr;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr || self->package_key.empty()) {
        return nullptr;
    }
    const auto package = package_syms_.find(self->package_key);
    if (package == package_syms_.end()) {
        return nullptr;
    }
    const auto found = package->second.find(name);
    if (found == package->second.end()) {
        return nullptr;
    }
    return &found->second;
}

bool Checker::remember_shared(const Symbol &symbol, SourceLoc loc) {
    if (graph_ == nullptr || symbol.name.empty()) {
        return true;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return true;
    }
    auto &table = package_syms_[self->package_key];
    const auto found = table.find(symbol.name);
    if (found != table.end()) {
        if (found->second.is_constant && symbol.is_constant && found->second.const_value == symbol.const_value) {
            return true;
        }
        error(loc, "redefinition");
        return false;
    }
    for (const auto &[file, symbols] : statics_) {
        if (symbols.contains(symbol.name)) {
            (void)file;
            error(loc, "redefinition");
            return false;
        }
    }
    table.emplace(symbol.name, symbol);
    return true;
}

void Checker::note_body(const std::string &name) {
    if (graph_ == nullptr) {
        return;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return;
    }
    const auto package = package_syms_.find(self->package_key);
    if (package == package_syms_.end()) {
        return;
    }
    const auto found = package->second.find(name);
    if (found != package->second.end()) {
        found->second.has_body = true;
    }
}

bool Checker::shared_has(const std::string &name) const {
    if (graph_ == nullptr) {
        return false;
    }
    const PackageNode *self = find_node(file_id_);
    if (self == nullptr) {
        return false;
    }
    const auto package = package_syms_.find(self->package_key);
    return package != package_syms_.end() && package->second.contains(name);
}

std::vector<uint32_t> Checker::files_in_package(uint32_t file_id) const {
    std::vector<uint32_t> ids;
    ids.push_back(file_id);
    const PackageNode *self = find_node(file_id);
    if (self == nullptr || self->package_key.empty() || graph_ == nullptr) {
        return ids;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.package_key == self->package_key && node.file_id != file_id) {
            ids.push_back(node.file_id);
        }
    }
    return ids;
}

int Checker::find_tag(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = tags_.find(id);
        if (found == tags_.end()) {
            continue;
        }
        const auto tag = found->second.find(name);
        if (tag != found->second.end()) {
            return tag->second;
        }
    }
    return -1;
}

bool Checker::package_has_enum(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = enums_.find(id);
        if (found != enums_.end() && found->second.contains(name)) {
            return true;
        }
    }
    return false;
}

int Checker::package_trait(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = trait_names_.find(id);
        if (found == trait_names_.end()) {
            continue;
        }
        const auto trait = found->second.find(name);
        if (trait != found->second.end()) {
            return trait->second;
        }
    }
    return -1;
}

bool Checker::package_enum_const(uint32_t file, const std::string &name) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = enum_consts_.find(id);
        if (found != enum_consts_.end() && found->second.contains(name)) {
            return true;
        }
    }
    return false;
}

bool Checker::package_typedef(uint32_t file, const std::string &name, Type &out) const {
    for (const uint32_t id : files_in_package(file)) {
        const auto found = typedefs_.find(id);
        if (found == typedefs_.end()) {
            continue;
        }
        const auto alias = found->second.find(name);
        if (alias != found->second.end()) {
            out = alias->second;
            return true;
        }
    }
    return false;
}

bool Checker::exported_elsewhere(const std::string &name) const {
    for (const uint32_t id : files_in_package(file_id_)) {
        if (id == file_id_) {
            continue;
        }
        const auto file = exports_.find(id);
        if (file != exports_.end() && file->second.contains(name)) {
            return true;
        }
    }
    return false;
}

bool Checker::declare(Symbol symbol, SourceLoc loc) {
    if (symbol.name.empty()) {
        return false;
    }
    auto &table = scopes_.back().symbols;
    if (table.contains(symbol.name)) {
        error(loc, "redefinition");
        return false;
    }
    table.emplace(symbol.name, std::move(symbol));
    return true;
}

Type Checker::type_of_name(const std::string &name, SourceLoc loc, bool allow_incomplete) {
    Type builtin;
    if (fill_builtin(name, builtin)) {
        return builtin;
    }
    if (name == "invalid") {
        return type_invalid();
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return alias;
    }
    if (find_tag(file_id_, name) >= 0) {
        return lookup_tag(name, loc, allow_incomplete);
    }
    if (const int trait = package_trait(file_id_, name); trait >= 0) {
        return type_trait(trait);
    }
    error(loc, "not supported yet");
    return type_invalid();
}

Type Checker::with_indirections(Type type, int count, SourceLoc loc) {
    if (type.kind == TypeKind::Trait) {
        if (count > 1) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        if (count == 1) {
            return type_pointer(std::move(type));
        }
        return type;
    }
    for (int i = 0; i < count; ++i) {
        if (type.kind == TypeKind::Invalid) {
            return type;
        }
        if (type.kind != TypeKind::Int && type.kind != TypeKind::Char && type.kind != TypeKind::Bool &&
            type.kind != TypeKind::Pointer && type.kind != TypeKind::Struct && type.kind != TypeKind::Void) {
            error(loc, "not supported yet");
            return type_invalid();
        }
        type = type_pointer(std::move(type));
    }
    return type;
}

Type Checker::lookup_tag(const std::string &name, SourceLoc loc, bool allow_incomplete) {
    const int id = find_tag(file_id_, name);
    if (id < 0) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    if (id < 0 || id >= static_cast<int>(structs_.size())) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    if (!allow_incomplete && !structs_[static_cast<std::size_t>(id)].complete) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    return type_struct(id);
}

Type Checker::lookup_qualified_type(const std::string &package, const std::string &name, SourceLoc loc) {
    const Symbol *symbol = lookup(package);
    if (symbol == nullptr || !symbol->is_package) {
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    const Symbol *member = find_export(symbol->package_file, name);
    if (member == nullptr) {
        error(loc, name + " is not exported");
        return type_invalid();
    }
    if (!member->is_type) {
        error(loc, "type mismatch");
        return type_invalid();
    }
    return member->type;
}

bool Checker::lookup_field(const Type &object, const std::string &name, Type &out) const {
    if (object.kind != TypeKind::Struct || object.struct_id < 0 ||
        object.struct_id >= static_cast<int>(structs_.size())) {
        return false;
    }
    for (const StructInfo::Field &field : structs_[static_cast<std::size_t>(object.struct_id)].fields) {
        if (field.name == name) {
            out = field.type;
            return true;
        }
    }
    return false;
}

bool Checker::dot_object(const Type &base, Type &object, bool &through_pointer) const {
    through_pointer = false;
    if (base.kind == TypeKind::Struct) {
        object = base;
        return true;
    }
    if (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Struct) {
        object = *base.pointee;
        through_pointer = true;
        return true;
    }
    return false;
}

const Checker::MethodInfo *Checker::find_method(int struct_id, const std::string &name) const {
    if (struct_id < 0 || struct_id >= static_cast<int>(structs_.size())) {
        return nullptr;
    }
    const std::string &struct_name = structs_[static_cast<std::size_t>(struct_id)].name;
    for (const uint32_t file : files_in_package(file_id_)) {
        const auto methods = methods_.find(file);
        if (methods == methods_.end()) {
            continue;
        }
        const auto group = methods->second.find(struct_name);
        if (group == methods->second.end()) {
            continue;
        }
        const auto method = group->second.find(name);
        if (method != group->second.end()) {
            return &method->second;
        }
    }
    return nullptr;
}

const Checker::TraitInfo::Method *Checker::find_trait_method(int trait_id, const std::string &name) const {
    if (trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        return nullptr;
    }
    for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(trait_id)].methods) {
        if (method.name == name) {
            return &method;
        }
    }
    return nullptr;
}

bool Checker::trait_method_matches(const Type &trait_fn, const Type &concrete) const {
    if (trait_fn.kind != TypeKind::Function || concrete.kind != TypeKind::Function || trait_fn.result == nullptr ||
        concrete.result == nullptr) {
        return false;
    }
    if (trait_fn.variadic || concrete.variadic || concrete.params.empty() ||
        trait_fn.params.size() + 1 != concrete.params.size()) {
        return false;
    }
    if (!same_type(*trait_fn.result, *concrete.result)) {
        return false;
    }
    const Type &receiver = concrete.params[0];
    if (receiver.kind == TypeKind::Pointer) {
        if (receiver.pointee == nullptr || receiver.pointee->kind != TypeKind::Struct) {
            return false;
        }
    } else if (receiver.kind != TypeKind::Struct) {
        return false;
    }
    for (std::size_t i = 0; i < trait_fn.params.size(); ++i) {
        if (!same_type(trait_fn.params[i], concrete.params[i + 1])) {
            return false;
        }
    }
    return true;
}

void Checker::register_traits(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<TraitDecl>(&decl.data);
        if (item == nullptr || item->name.empty()) {
            continue;
        }
        if (package_trait(file_id, item->name) >= 0 || package_enum_const(file_id, item->name) ||
            find_tag(file_id, item->name) >= 0) {
            error(item->loc, "redefinition");
            continue;
        }
        const int id = static_cast<int>(traits_.size());
        traits_.push_back(TraitInfo{item->name, {}});
        trait_names_[file_id][item->name] = id;
        for (const FunctionDecl &method : item->methods) {
            if (method.name.empty()) {
                continue;
            }
            bool duplicate = false;
            for (const TraitInfo::Method &existing : traits_[static_cast<std::size_t>(id)].methods) {
                if (existing.name == method.name) {
                    error(method.loc, "redefinition");
                    duplicate = true;
                }
            }
            if (duplicate) {
                continue;
            }
            bool wrote_receiver = false;
            for (const Param &param : method.params) {
                if (param.is_receiver) {
                    wrote_receiver = true;
                }
            }
            if (wrote_receiver || method.variadic) {
                error(method.loc, method.variadic && !wrote_receiver ? "not supported yet" : "type mismatch");
                continue;
            }
            if (method.results.size() > 1) {
                error(method.loc, "not supported yet");
                continue;
            }
            std::vector<Type> params;
            for (const Param &param : method.params) {
                params.push_back(object_type(param.type_name, param.indirections, param.is_array, param.array_len, param.loc,
                                             true, param.type_package, param.is_struct, param.is_enum));
            }
            Type result = method.is_struct || method.is_enum || !method.type_package.empty()
                              ? object_type(method.return_type, method.indirections, false, 0, method.loc, false,
                                            method.type_package, method.is_struct, method.is_enum)
                              : with_indirections(type_of_name(method.return_type, method.loc, method.indirections > 0),
                                                  method.indirections, method.loc);
            if (mentions_trait(result)) {
                error(method.loc, "not supported yet");
                continue;
            }
            traits_[static_cast<std::size_t>(id)].methods.push_back(
                TraitInfo::Method{method.name, type_function(std::move(result), std::move(params), false)});
        }
    }
}

void Checker::require_satisfies(int struct_id, int trait_id, bool through_pointer, SourceLoc loc) {
    if (struct_id < 0 || trait_id < 0 || trait_id >= static_cast<int>(traits_.size())) {
        error(loc, "type mismatch");
        return;
    }
    for (const TraitInfo::Method &method : traits_[static_cast<std::size_t>(trait_id)].methods) {
        const MethodInfo *concrete = find_method(struct_id, method.name);
        if (concrete == nullptr || !concrete->has_body) {
            error(loc, "undeclared identifier");
            continue;
        }
        const bool pointer_method = concrete->type.kind == TypeKind::Function && !concrete->type.params.empty() &&
                                    concrete->type.params[0].kind == TypeKind::Pointer;
        if (!through_pointer && pointer_method) {
            error(loc, "type mismatch");
            continue;
        }
        if (!trait_method_matches(method.type, concrete->type)) {
            error(loc, "type mismatch");
        }
    }
}

bool Checker::field_waits(const FieldDecl &field) const {
    if (field.is_array || field.indirections > 0 || !field.type_package.empty() || field.is_enum || field.type_name.empty()) {
        return false;
    }
    const int id = find_tag(file_id_, field.type_name);
    if (id < 0 || id >= static_cast<int>(structs_.size())) {
        return false;
    }
    return !structs_[static_cast<std::size_t>(id)].complete;
}

void Checker::declare_struct_tags(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    auto publish_tag = [&](const StructDecl &item, int id) {
        if (!item.exported) {
            return;
        }
        const auto same_tag = [&](const Symbol &symbol) {
            return symbol.is_struct_tag && symbol.type.kind == TypeKind::Struct && symbol.type.struct_id == id;
        };
        const auto local = exports_[file_id_].find(item.name);
        if (local != exports_[file_id_].end()) {
            if (!same_tag(local->second)) {
                error(item.loc, "redefinition");
            }
            return;
        }
        for (const uint32_t other : files_in_package(file_id_)) {
            if (other == file_id_) {
                continue;
            }
            const auto file = exports_.find(other);
            if (file == exports_.end()) {
                continue;
            }
            const auto found = file->second.find(item.name);
            if (found == file->second.end()) {
                continue;
            }
            if (!same_tag(found->second)) {
                error(item.loc, "redefinition");
            }
            return;
        }
        Symbol symbol;
        symbol.name = item.name;
        symbol.is_type = true;
        symbol.is_struct_tag = true;
        symbol.type = type_struct(id);
        exports_[file_id_].emplace(item.name, std::move(symbol));
    };
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<StructDecl>(&decl.data);
        if (item == nullptr || item->name.empty()) {
            continue;
        }
        const int existing = find_tag(file_id, item->name);
        if (item->forward) {
            if (existing >= 0) {
                publish_tag(*item, existing);
                continue;
            }
            const int id = static_cast<int>(structs_.size());
            StructInfo info;
            info.name = item->name;
            info.forward = true;
            structs_.push_back(std::move(info));
            tags_[file_id][item->name] = id;
            publish_tag(*item, id);
            continue;
        }
        if (existing >= 0) {
            StructInfo &info = structs_[static_cast<std::size_t>(existing)];
            if (!info.forward) {
                error(item->loc, "redefinition");
                continue;
            }
            info.forward = false;
            if (item->fields.empty()) {
                error(item->loc, "not supported yet");
                info.complete = true;
            } else {
                pending_structs_.push_back(PendingStruct{file_id, item, existing});
            }
            publish_tag(*item, existing);
            continue;
        }
        const int id = static_cast<int>(structs_.size());
        structs_.push_back(StructInfo{item->name, false, {}});
        tags_[file_id][item->name] = id;
        if (item->fields.empty()) {
            error(item->loc, "not supported yet");
            structs_.back().complete = true;
        } else {
            pending_structs_.push_back(PendingStruct{file_id, item, id});
        }
        publish_tag(*item, id);
    }
}

void Checker::define_struct_fields() {
    auto type_fields = [&](PendingStruct &pending, bool complain_waiting) {
        file_id_ = pending.file_id;
        StructInfo &info = structs_[static_cast<std::size_t>(pending.id)];
        for (const FieldDecl &field : pending.decl->fields) {
            if (field.name.empty()) {
                continue;
            }
            for (const StructInfo::Field &existing : info.fields) {
                if (existing.name == field.name) {
                    error(field.loc, "redefinition");
                }
            }
            if (complain_waiting && field_waits(field)) {
                error(field.loc, "not supported yet");
                info.fields.push_back(StructInfo::Field{field.name, type_invalid()});
                continue;
            }
            Type field_type = object_type(field.type_name, field.indirections, field.is_array, field.array_len, field.loc,
                                          false, field.type_package, field.is_struct, field.is_enum);
            info.fields.push_back(StructInfo::Field{field.name, std::move(field_type)});
        }
        info.complete = true;
    };
    bool progress = true;
    while (progress) {
        progress = false;
        for (PendingStruct &pending : pending_structs_) {
            if (pending.id < 0 || pending.decl == nullptr || structs_[static_cast<std::size_t>(pending.id)].complete) {
                continue;
            }
            file_id_ = pending.file_id;
            bool waits = false;
            for (const FieldDecl &field : pending.decl->fields) {
                if (field_waits(field)) {
                    waits = true;
                    break;
                }
            }
            if (waits) {
                continue;
            }
            type_fields(pending, false);
            progress = true;
        }
    }
    for (PendingStruct &pending : pending_structs_) {
        if (pending.id < 0 || pending.decl == nullptr || structs_[static_cast<std::size_t>(pending.id)].complete) {
            continue;
        }
        type_fields(pending, true);
    }
    pending_structs_.clear();
}

void Checker::register_structs(const TranslationUnit &unit, uint32_t file_id) {
    declare_struct_tags(unit, file_id);
    define_struct_fields();
}

void Checker::register_enums(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<EnumDecl>(&decl.data);
        if (item == nullptr) {
            continue;
        }
        if (item->enumerators.empty()) {
            error(item->loc, "not supported yet");
        }
        if (!item->name.empty()) {
            if (package_has_enum(file_id, item->name)) {
                error(item->loc, "redefinition");
            } else {
                enums_[file_id].insert(item->name);
            }
        }
        int next = 0;
        bool next_ok = true;
        for (const Enumerator &enumerator : item->enumerators) {
            int value = 0;
            if (enumerator.has_value) {
                value = enumerator.value;
            } else if (!next_ok) {
                error(enumerator.loc, "not supported yet");
            } else {
                value = next;
            }
            if (enumerator.name.empty()) {
                continue;
            }
            if (package_enum_const(file_id, enumerator.name)) {
                error(enumerator.loc, "redefinition");
            } else {
                enum_consts_[file_id].emplace(enumerator.name, EnumConst{value, enumerator.loc});
            }
            if (value == 2147483647) {
                next_ok = false;
            } else {
                next = value + 1;
                next_ok = true;
            }
        }
        if (item->exported && !item->name.empty()) {
            if (exports_[file_id].contains(item->name) || exported_elsewhere(item->name)) {
                error(item->loc, "redefinition");
            } else {
                Symbol symbol;
                symbol.name = item->name;
                symbol.is_type = true;
                symbol.is_enum = true;
                symbol.type = type_int();
                exports_[file_id].emplace(item->name, std::move(symbol));
            }
        }
        if (!item->exported) {
            continue;
        }
        for (const Enumerator &enumerator : item->enumerators) {
            const auto constants = enum_consts_.find(file_id);
            if (enumerator.name.empty() || constants == enum_consts_.end() ||
                !constants->second.contains(enumerator.name)) {
                continue;
            }
            if (exports_[file_id].contains(enumerator.name) || exported_elsewhere(enumerator.name)) {
                error(enumerator.loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = enumerator.name;
            symbol.type = type_int();
            symbol.is_constant = true;
            symbol.const_value = constants->second.at(enumerator.name).value;
            exports_[file_id].emplace(enumerator.name, std::move(symbol));
        }
    }
}

bool Checker::typedef_target_ready(const TypedefDecl &item) const {
    if (!item.type_package.empty() || item.is_struct || item.is_enum) {
        return true;
    }
    const std::string &name = item.type_name;
    if (name.empty() || name == "invalid") {
        return true;
    }
    Type builtin;
    if (fill_builtin(name, builtin)) {
        return true;
    }
    Type alias;
    if (package_typedef(file_id_, name, alias) || find_tag(file_id_, name) >= 0 || package_has_enum(file_id_, name) ||
        package_trait(file_id_, name) >= 0) {
        return true;
    }
    return false;
}

void Checker::register_one_typedef(const TypedefDecl &item) {
    if (item.name.empty()) {
        return;
    }
    Type existing;
    if (package_enum_const(file_id_, item.name) || package_typedef(file_id_, item.name, existing)) {
        error(item.loc, "redefinition");
        return;
    }
    if (item.type_package.empty() && item.indirections == 0 && !item.is_array && item.name == item.type_name &&
        (item.is_struct || find_tag(file_id_, item.name) >= 0)) {
        error(item.loc, "redefinition");
        return;
    }
    const Type type = object_type(item.type_name, item.indirections, item.is_array, item.array_len, item.loc, false,
                                  item.type_package, item.is_struct, item.is_enum);
    if (type.kind == TypeKind::Invalid) {
        return;
    }
    if (type.kind == TypeKind::Trait) {
        error(item.loc, "not supported yet");
        return;
    }
    typedefs_[file_id_].emplace(item.name, type);
    if (!item.exported) {
        return;
    }
    auto &table = exports_[file_id_];
    const auto found = table.find(item.name);
    if (found == table.end()) {
        if (exported_elsewhere(item.name)) {
            error(item.loc, "redefinition");
            return;
        }
        Symbol symbol;
        symbol.name = item.name;
        symbol.is_type = true;
        symbol.is_typedef = true;
        symbol.type = type;
        table.emplace(item.name, std::move(symbol));
        return;
    }
    if (found->second.is_type && same_type(found->second.type, type)) {
        found->second.is_typedef = true;
        return;
    }
    error(item.loc, "redefinition");
}

void Checker::register_typedefs(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        const auto *item = std::get_if<TypedefDecl>(&decl.data);
        if (item == nullptr) {
            continue;
        }
        register_one_typedef(*item);
    }
}

void Checker::inject_enum_consts() {
    const auto file = enum_consts_.find(file_id_);
    if (file == enum_consts_.end()) {
        return;
    }
    for (const auto &[name, info] : file->second) {
        Symbol symbol;
        symbol.name = name;
        symbol.type = type_int();
        symbol.is_constant = true;
        symbol.const_value = info.value;
        if (remember_shared(symbol, info.loc)) {
            declare(symbol, info.loc);
        }
    }
}

bool Checker::name_taken(const std::string &name) const {
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return true;
    }
    return package_enum_const(file_id_, name);
}

Type Checker::published_object(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                               bool parameter, const std::string &package, bool is_struct, bool is_enum) {
    Type alias;
    if (is_struct || is_enum || !package.empty() || package_typedef(file_id_, name, alias) || find_tag(file_id_, name) >= 0) {
        return object_type(name, indirections, is_array, array_len, loc, parameter, package, is_struct, is_enum);
    }
    return quiet_object(name, indirections, is_array, array_len, parameter);
}

Type Checker::published_result(const std::string &name, int indirections, SourceLoc loc, const std::string &package,
                               bool is_struct, bool is_enum) {
    if (is_struct || is_enum || !package.empty()) {
        return object_type(name, indirections, false, 0, loc, false, package, is_struct, is_enum);
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return with_indirections(std::move(alias), indirections, loc);
    }
    if (find_tag(file_id_, name) >= 0) {
        return object_type(name, indirections, false, 0, loc, false, package, false, false);
    }
    return quiet_indirect(quiet_type(name), indirections);
}

void Checker::note_struct_use(bool is_struct, const std::string &name, SourceLoc loc) {
    if (!is_struct || name.empty()) {
        return;
    }
    Type alias;
    if (package_typedef(file_id_, name, alias)) {
        return;
    }
    error(loc, "struct outside definition");
}

Type Checker::spelled_type(const std::string &name, const std::string &package, bool is_struct, bool is_enum,
                           int indirections, SourceLoc loc) {
    note_struct_use(is_struct, name, loc);
    if (!package.empty()) {
        return with_indirections(lookup_qualified_type(package, name, loc), indirections, loc);
    }
    if (is_struct) {
        return with_indirections(lookup_tag(name, loc, indirections > 0), indirections, loc);
    }
    if (is_enum) {
        if (name.empty() || package_has_enum(file_id_, name)) {
            return with_indirections(type_int(), indirections, loc);
        }
        error(loc, "undeclared identifier");
        return type_invalid();
    }
    return with_indirections(type_of_name(name, loc, indirections > 0), indirections, loc);
}

Type Checker::object_type(const std::string &name, int indirections, bool is_array, int array_len, SourceLoc loc,
                          bool parameter, const std::string &package, bool is_struct, bool is_enum, bool receiver) {
    if (!receiver) {
        note_struct_use(is_struct, name, loc);
    }
    Type type;
    if (!package.empty()) {
        type = with_indirections(lookup_qualified_type(package, name, loc), indirections, loc);
    } else if (is_struct) {
        type = with_indirections(lookup_tag(name, loc, indirections > 0), indirections, loc);
    } else if (is_enum) {
        if (name.empty() || package_has_enum(file_id_, name)) {
            type = with_indirections(type_int(), indirections, loc);
        } else {
            error(loc, "undeclared identifier");
            type = type_invalid();
        }
    } else {
        type = with_indirections(type_of_name(name, loc, indirections > 0), indirections, loc);
    }
    if (mentions_trait(type) && (is_array || !parameter)) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    if (!is_array) {
        if (type.kind == TypeKind::Void) {
            error(loc, "type mismatch");
            return type_invalid();
        }
        if (parameter && type.kind == TypeKind::Array && type.element) {
            return type_pointer(*type.element);
        }
        return type;
    }
    if (type.kind == TypeKind::Invalid) {
        return type;
    }
    if (type.kind == TypeKind::Void || type.kind == TypeKind::Function) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    if (parameter) {
        return type_pointer(std::move(type));
    }
    if (array_len <= 0) {
        error(loc, "not supported yet");
        return type_invalid();
    }
    return type_array(std::move(type), array_len);
}

bool Checker::is_null_constant(const Expr &expr) const {
    const auto *literal = std::get_if<Expr::Int>(&expr.data);
    return literal != nullptr && literal->value == 0;
}

bool Checker::is_lvalue_form(const Expr &expr) const { return lvalue_form(expr); }

void Checker::expect(const Type &got, const Type &want, SourceLoc loc) {
    if (got.kind == TypeKind::Invalid || want.kind == TypeKind::Invalid) {
        return;
    }
    if (!same_type(got, want)) {
        error(loc, "type mismatch");
    }
}

void Checker::expect_value(const Type &got, const Type &want, const Expr &expr, SourceLoc loc) {
    if (want.kind == TypeKind::Trait && got.kind != TypeKind::Invalid) {
        if (got.kind == TypeKind::Trait) {
            expect(got, want, loc);
            return;
        }
        if (got.kind == TypeKind::Struct) {
            require_satisfies(got.struct_id, want.trait_id, false, loc);
            return;
        }
        error(loc, "type mismatch");
        return;
    }
    if (want.kind == TypeKind::Pointer && want.pointee && want.pointee->kind == TypeKind::Trait &&
        got.kind != TypeKind::Invalid) {
        if (got.kind == TypeKind::Pointer && got.pointee && got.pointee->kind == TypeKind::Trait) {
            expect(*got.pointee, *want.pointee, loc);
            return;
        }
        if (got.kind == TypeKind::Pointer && got.pointee && got.pointee->kind == TypeKind::Struct) {
            require_satisfies(got.pointee->struct_id, want.pointee->trait_id, true, loc);
            return;
        }
        error(loc, "type mismatch");
        return;
    }
    if (want.kind == TypeKind::Pointer && got.kind == TypeKind::Nullptr) {
        return;
    }
    if (want.kind == TypeKind::Pointer && got.kind == TypeKind::Int && is_null_constant(expr)) {
        return;
    }
    if (is_scalar(got) && is_scalar(want)) {
        return;
    }
    expect(got, want, loc);
}

void Checker::expect_integer(const Type &got, SourceLoc loc) {
    if (got.kind == TypeKind::Invalid || is_integer(got)) {
        return;
    }
    error(loc, "type mismatch");
}

Type Checker::check_expr(const Expr &expr, bool used) {
    const Type type = check_raw(expr, used);
    if (type.kind == TypeKind::Array && type.element) {
        return type_pointer(*type.element);
    }
    return promote(type);
}

Type Checker::check_raw(const Expr &expr, bool used) {
    return std::visit(
        [&](const auto &node) -> Type {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Name>) {
                (void)used;
                const Symbol *symbol = lookup(node.text);
                if (!symbol) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                if (captured(*symbol)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                if (symbol->is_package) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Int>) {
                const int longs = node.suffix.long_count;
                const bool uns = node.suffix.is_unsigned;
                if (longs > 2) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (longs == 0 && !uns) {
                    if (node.value > 2147483647ULL) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return type_int();
                }
                if (longs == 0 && uns) {
                    if (node.value > 4294967295ULL) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return type_rank(3, true);
                }
                if (!uns && node.value > 9223372036854775807ULL) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return type_rank(longs >= 2 ? 5 : 4, uns);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                (void)used;
                if (node.value > 255ULL) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return type_char();
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                if (node.op == TokenKind::Star) {
                    const Type inner = check_expr(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind != TypeKind::Pointer || !inner.pointee) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    if (inner.pointee->kind == TypeKind::Void) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return *inner.pointee;
                }
                if (node.op == TokenKind::Amp) {
                    if (const auto *named = std::get_if<Expr::Name>(&node.expr->data)) {
                        if (const Symbol *symbol = lookup(named->text); symbol != nullptr && symbol->is_constant) {
                            error(node.loc, "not an lvalue");
                            return type_invalid();
                        }
                    }
                    if (const auto *selector = std::get_if<Expr::Selector>(&node.expr->data)) {
                        if (selector->base != nullptr) {
                            if (const auto *base = std::get_if<Expr::Name>(&selector->base->data)) {
                                if (const Symbol *symbol = lookup(base->text); symbol != nullptr && symbol->is_package) {
                                    const SelectorTarget target = resolve_selector(*selector);
                                    if (target.kind == SelectorTarget::Kind::Failed) {
                                        return type_invalid();
                                    }
                                    if (target.kind == SelectorTarget::Kind::Package && target.symbol != nullptr &&
                                        target.symbol->is_constant) {
                                        error(node.loc, "not an lvalue");
                                        return type_invalid();
                                    }
                                }
                            }
                        }
                    }
                    if (!is_lvalue_form(*node.expr)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.expr, true);
                        return type_invalid();
                    }
                    const Type inner = check_raw(*node.expr, true);
                    if (inner.kind == TypeKind::Invalid) {
                        return inner;
                    }
                    if (inner.kind == TypeKind::Function) {
                        error(node.loc, "not an lvalue");
                        return type_invalid();
                    }
                    if (inner.kind == TypeKind::Array) {
                        error(node.loc, "not supported yet");
                        return type_invalid();
                    }
                    return type_pointer(inner);
                }
                const Type inner = check_expr(*node.expr, true);
                if (node.op == TokenKind::Bang) {
                    expect_integer(inner, node.loc);
                    return inner.kind == TypeKind::Invalid ? inner : type_int();
                }
                expect_integer(inner, node.loc);
                return inner.kind == TypeKind::Invalid || !is_integer(inner) ? type_invalid() : inner;
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                const Type lhs = check_expr(*node.lhs, true);
                const Type rhs = check_expr(*node.rhs, true);
                if (node.op == TokenKind::EqEq || node.op == TokenKind::BangEq) {
                    const bool lhs_ptr = lhs.kind == TypeKind::Pointer || lhs.kind == TypeKind::Nullptr;
                    const bool rhs_ptr = rhs.kind == TypeKind::Pointer || rhs.kind == TypeKind::Nullptr;
                    if (lhs_ptr || rhs_ptr) {
                        if (lhs.kind == TypeKind::Invalid || rhs.kind == TypeKind::Invalid) {
                            return type_invalid();
                        }
                        if (!lhs_ptr || !rhs_ptr ||
                            (lhs.kind == TypeKind::Pointer && rhs.kind == TypeKind::Pointer && !same_type(lhs, rhs))) {
                            error(node.loc, "type mismatch");
                            return type_invalid();
                        }
                        return type_int();
                    }
                }
                expect_integer(lhs, expr_loc(*node.lhs));
                expect_integer(rhs, expr_loc(*node.rhs));
                if (!is_integer(lhs) || !is_integer(rhs)) {
                    return type_invalid();
                }
                const Type common = usual(lhs, rhs);
                switch (node.op) {
                case TokenKind::EqEq:
                case TokenKind::BangEq:
                case TokenKind::Lt:
                case TokenKind::Gt:
                case TokenKind::Le:
                case TokenKind::Ge:
                    return type_int();
                default:
                    return common;
                }
            } else if constexpr (std::is_same_v<T, Expr::Assign>) {
                if (const auto *star = std::get_if<Expr::Unary>(&node.lhs->data); star != nullptr && star->op == TokenKind::Star) {
                    const Type pointer = check_expr(*star->expr, true);
                    const Type rhs = check_expr(*node.rhs, true);
                    if (pointer.kind != TypeKind::Pointer || !pointer.pointee) {
                        if (pointer.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        return type_invalid();
                    }
                    if (pointer.pointee->kind == TypeKind::Void) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    expect_value(rhs, *pointer.pointee, *node.rhs, expr_loc(*node.rhs));
                    return *pointer.pointee;
                }
                if (const auto *index = std::get_if<Expr::Index>(&node.lhs->data)) {
                    const Type base = check_expr(*index->base, true);
                    const Type subscript = check_expr(*index->index, true);
                    expect(subscript, type_int(), expr_loc(*index->index));
                    const Type rhs = check_expr(*node.rhs, true);
                    if (base.kind != TypeKind::Pointer || !base.pointee) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        return type_invalid();
                    }
                    if (base.pointee->kind == TypeKind::Void) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    expect_value(rhs, *base.pointee, *node.rhs, expr_loc(*node.rhs));
                    return *base.pointee;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.lhs->data)) {
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (target.kind == SelectorTarget::Kind::Package) {
                        if (target.symbol->is_function || target.symbol->is_type || target.symbol->is_constant ||
                            target.symbol->type.kind == TypeKind::Array) {
                            error(node.loc, "not an lvalue");
                            check_expr(*node.rhs, true);
                            return type_invalid();
                        }
                        const Type rhs = check_expr(*node.rhs, true);
                        expect_value(rhs, target.symbol->type, *node.rhs, expr_loc(*node.rhs));
                        return target.symbol->type;
                    }
                    const Type base = check_expr(*selector->base, true);
                    if (base.kind == TypeKind::Trait ||
                        (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                        error(node.loc, base.kind == TypeKind::Trait ? "undeclared identifier" : "type mismatch");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type object;
                    bool through_pointer = false;
                    if (!dot_object(base, object, through_pointer)) {
                        if (base.kind != TypeKind::Invalid) {
                            error(node.loc, "not an lvalue");
                        }
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    if (!through_pointer && !is_lvalue_form(*selector->base)) {
                        error(node.loc, "not an lvalue");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    Type field;
                    if (!lookup_field(object, selector->field, field)) {
                        error(node.loc, "undeclared identifier");
                        check_expr(*node.rhs, true);
                        return type_invalid();
                    }
                    const Type rhs = check_expr(*node.rhs, true);
                    expect_value(rhs, field, *node.rhs, expr_loc(*node.rhs));
                    return field;
                }
                const bool name = std::holds_alternative<Expr::Name>(node.lhs->data);
                const Symbol *symbol = nullptr;
                if (name) {
                    symbol = lookup(std::get<Expr::Name>(node.lhs->data).text);
                }
                if (name && symbol != nullptr && captured(*symbol)) {
                    error(expr_loc(*node.lhs), "not supported yet");
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                if (!name || !symbol || symbol->is_function || symbol->is_package || symbol->is_constant ||
                    symbol->type.kind == TypeKind::Array) {
                    if (name && !symbol) {
                        error(expr_loc(*node.lhs), "undeclared identifier");
                    } else {
                        error(node.loc, "not an lvalue");
                    }
                    check_expr(*node.rhs, true);
                    return type_invalid();
                }
                const Type rhs = check_expr(*node.rhs, true);
                expect_value(rhs, symbol->type, *node.rhs, expr_loc(*node.rhs));
                return symbol->type;
            } else if constexpr (std::is_same_v<T, Expr::Call>) {
                const bool deferring = checking_defer_;
                if (deferring) {
                    checking_defer_ = false;
                }
                if (const auto *selector = std::get_if<Expr::Selector>(&node.callee->data)) {
                    const SelectorTarget target = resolve_selector(*selector);
                    if (target.kind == SelectorTarget::Kind::Failed) {
                        for (const auto &arg : node.args) {
                            check_expr(*arg, true);
                        }
                        return type_invalid();
                    }
                    if (target.kind != SelectorTarget::Kind::Package && selector->base != nullptr) {
                        const Type base = check_expr(*selector->base, true);
                        if (base.kind == TypeKind::Invalid) {
                            for (const auto &arg : node.args) {
                                check_expr(*arg, true);
                            }
                            return type_invalid();
                        }
                        if (base.kind == TypeKind::Trait ||
                            (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                            const bool through_pointer = base.kind != TypeKind::Trait;
                            const int trait_id = through_pointer ? base.pointee->trait_id : base.trait_id;
                            const TraitInfo::Method *method = find_trait_method(trait_id, selector->field);
                            if (method == nullptr || method->type.kind != TypeKind::Function || method->type.result == nullptr) {
                                error(node.loc, "undeclared identifier");
                                for (const auto &arg : node.args) {
                                    check_expr(*arg, true);
                                }
                                return type_invalid();
                            }
                            if (deferring) {
                                error(node.loc, "not supported yet");
                            }
                            const std::size_t arity = method->type.params.size();
                            const bool count_ok = node.args.size() == arity;
                            if (!count_ok) {
                                error(node.loc, "argument count");
                            }
                            for (std::size_t i = 0; i < node.args.size(); ++i) {
                                const Type arg = check_expr(*node.args[i], true);
                                if (count_ok && i < arity) {
                                    expect_value(arg, method->type.params[i], *node.args[i], expr_loc(*node.args[i]));
                                }
                            }
                            return *method->type.result;
                        }
                        Type object;
                        bool through_pointer = false;
                        if (dot_object(base, object, through_pointer)) {
                            if (const MethodInfo *method = find_method(object.struct_id, selector->field)) {
                                if (deferring && method->type.variadic) {
                                    error(node.loc, "not supported yet");
                                }
                                const std::size_t arity = method->type.params.size();
                                const bool count_ok = method->type.variadic ? node.args.size() + 1 >= arity
                                                                            : node.args.size() + 1 == arity;
                                if (!count_ok) {
                                    error(node.loc, "argument count");
                                }
                                if (!method->type.params.empty()) {
                                    const Type &receiver = method->type.params[0];
                                    if (receiver.kind == TypeKind::Pointer) {
                                        if (!through_pointer && !is_lvalue_form(*selector->base)) {
                                            error(node.loc, "not an lvalue");
                                        } else {
                                            const Type pointer = through_pointer ? base : type_pointer(object);
                                            expect(pointer, receiver, expr_loc(*selector->base));
                                        }
                                    } else {
                                        expect(object, receiver, expr_loc(*selector->base));
                                    }
                                }
                                for (std::size_t i = 0; i < node.args.size(); ++i) {
                                    const Type arg = check_expr(*node.args[i], true);
                                    if (count_ok && i + 1 < arity) {
                                        expect_value(arg, method->type.params[i + 1], *node.args[i], expr_loc(*node.args[i]));
                                    }
                                }
                                if (method->type.result == nullptr) {
                                    return type_invalid();
                                }
                                return *method->type.result;
                            }
                        }
                    }
                }
                const Type callee = check_expr(*node.callee, true);
                if (callee.kind == TypeKind::Invalid) {
                    for (const auto &arg : node.args) {
                        check_expr(*arg, true);
                    }
                    return type_invalid();
                }
                if (callee.kind != TypeKind::Function || !callee.result) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                if (deferring) {
                    bool generic = callee.variadic;
                    for (const Type &param : callee.params) {
                        if (mentions_trait(param)) {
                            generic = true;
                        }
                    }
                    if (generic) {
                        error(node.loc, "not supported yet");
                    }
                }
                const bool count_ok = callee.variadic ? node.args.size() >= callee.params.size()
                                                     : node.args.size() == callee.params.size();
                if (!count_ok) {
                    error(node.loc, "argument count");
                }
                for (std::size_t i = 0; i < node.args.size(); ++i) {
                    const Type arg = check_expr(*node.args[i], true);
                    if (count_ok && i < callee.params.size()) {
                        expect_value(arg, callee.params[i], *node.args[i], expr_loc(*node.args[i]));
                    }
                }
                return *callee.result;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const SelectorTarget target = resolve_selector(node);
                if (target.kind == SelectorTarget::Kind::Failed) {
                    return type_invalid();
                }
                if (target.kind == SelectorTarget::Kind::Package) {
                    if (target.symbol->is_type) {
                        error(node.loc, "type mismatch");
                        return type_invalid();
                    }
                    return target.symbol->type;
                }
                const Type base = check_expr(*node.base, true);
                if (base.kind == TypeKind::Trait ||
                    (base.kind == TypeKind::Pointer && base.pointee && base.pointee->kind == TypeKind::Trait)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                Type object;
                bool through_pointer = false;
                if (!dot_object(base, object, through_pointer)) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                Type field;
                if (!lookup_field(object, node.field, field)) {
                    error(node.loc, "undeclared identifier");
                    return type_invalid();
                }
                return field;
            } else if constexpr (std::is_same_v<T, Expr::String>) {
                (void)used;
                return type_array(type_char(), static_cast<int>(node.text.size()) + 1);
            } else if constexpr (std::is_same_v<T, Expr::Index>) {
                (void)used;
                const Type base = check_expr(*node.base, true);
                const Type subscript = check_expr(*node.index, true);
                expect(subscript, type_int(), expr_loc(*node.index));
                if (base.kind != TypeKind::Pointer || !base.pointee) {
                    if (base.kind != TypeKind::Invalid) {
                        error(node.loc, "type mismatch");
                    }
                    return type_invalid();
                }
                if (base.pointee->kind == TypeKind::Void) {
                    error(node.loc, "type mismatch");
                    return type_invalid();
                }
                return *base.pointee;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                (void)used;
                return type_bool();
            } else if constexpr (std::is_same_v<T, Expr::Nullptr>) {
                (void)used;
                return type_nullptr();
            } else if constexpr (std::is_same_v<T, Expr::Sizeof>) {
                (void)used;
                Type type = type_invalid();
                if (node.on_type) {
                    type = spelled_type(node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections,
                                        node.loc);
                } else if (node.expr) {
                    const auto *named = std::get_if<Expr::Name>(&node.expr->data);
                    Type ignored;
                    const bool alias = named != nullptr && package_typedef(file_id_, named->text, ignored);
                    const bool tag = named != nullptr && find_tag(file_id_, named->text) >= 0;
                    if (named != nullptr && lookup(named->text) == nullptr && (alias || tag)) {
                        type = type_of_name(named->text, node.loc, false);
                    } else {
                        type = check_raw(*node.expr, true);
                    }
                }
                if (type.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (type.kind == TypeKind::Void || type.kind == TypeKind::Function || type.kind == TypeKind::Tuple ||
                    mentions_trait(type)) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                return type_int();
            } else if constexpr (std::is_same_v<T, Expr::Cast>) {
                (void)used;
                const Type target =
                    spelled_type(node.type_name, node.type_package, node.is_struct, node.is_enum, node.indirections, node.loc);
                const Type source = node.expr ? check_expr(*node.expr, true) : type_invalid();
                if (target.kind == TypeKind::Invalid || source.kind == TypeKind::Invalid) {
                    return type_invalid();
                }
                if (target.kind == TypeKind::Void) {
                    error(node.loc, "not supported yet");
                    return type_invalid();
                }
                const bool source_scalar = is_scalar(source);
                const bool target_scalar = is_scalar(target);
                const bool source_int = same_type(source, type_int());
                const bool target_int = same_type(target, type_int());
                if ((target.kind == TypeKind::Pointer && source.kind == TypeKind::Pointer) ||
                    (target.kind == TypeKind::Pointer && source_int) ||
                    (target.kind == TypeKind::Pointer && source.kind == TypeKind::Nullptr) ||
                    (target_int && source.kind == TypeKind::Pointer) ||
                    (target_scalar && source_scalar)) {
                    return target;
                }
                error(node.loc, "type mismatch");
                return type_invalid();
            } else if constexpr (std::is_same_v<T, Expr::Func>) {
                (void)used;
                if (node.decl == nullptr) {
                    return type_invalid();
                }
                const FunctionDecl &fn = *node.decl;
                if (!fn.method_struct.empty() || fn.exported || fn.is_static || fn.is_extern || fn.variadic) {
                    error(fn.loc, "not supported yet");
                }
                bool generic = false;
                for (const Param &param : fn.params) {
                    if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
                        package_trait(file_id_, param.type_name) >= 0) {
                        generic = true;
                    }
                }
                Type signature = function_signature(fn);
                if (signature.result != nullptr && mentions_trait(*signature.result)) {
                    generic = true;
                }
                if (generic) {
                    error(fn.loc, "not supported yet");
                }
                if (fn.body != nullptr) {
                    check_function(fn);
                }
                return signature;
            } else {
                (void)used;
                return type_invalid();
            }
        },
        expr.data);
}

std::optional<int> Checker::fold_int(const Expr &expr) {
    auto wrap = [](int64_t value) { return static_cast<int>(static_cast<uint32_t>(value)); };
    return std::visit(
        [&](const auto &node) -> std::optional<int> {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Expr::Int>) {
                if (node.suffix.is_unsigned || node.suffix.long_count != 0 || node.value > 2147483647ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Char>) {
                if (node.value > 255ULL) {
                    return std::nullopt;
                }
                return static_cast<int>(node.value);
            } else if constexpr (std::is_same_v<T, Expr::Name>) {
                const Symbol *symbol = lookup(node.text);
                if (symbol != nullptr && symbol->is_constant) {
                    return symbol->const_value;
                }
                return std::nullopt;
            } else if constexpr (std::is_same_v<T, Expr::Selector>) {
                const auto *base = node.base == nullptr ? nullptr : std::get_if<Expr::Name>(&node.base->data);
                if (base == nullptr) {
                    return std::nullopt;
                }
                const Symbol *symbol = lookup(base->text);
                if (symbol == nullptr || !symbol->is_package) {
                    return std::nullopt;
                }
                const Symbol *member = find_export(symbol->package_file, node.field);
                if (member == nullptr || !member->is_constant) {
                    return std::nullopt;
                }
                return member->const_value;
            } else if constexpr (std::is_same_v<T, Expr::Bool>) {
                return node.value ? 1 : 0;
            } else if constexpr (std::is_same_v<T, Expr::Unary>) {
                const std::optional<int> inner = node.expr ? fold_int(*node.expr) : std::nullopt;
                if (!inner) {
                    return std::nullopt;
                }
                switch (node.op) {
                case TokenKind::Plus: return *inner;
                case TokenKind::Minus: return wrap(-static_cast<int64_t>(*inner));
                case TokenKind::Tilde: return wrap(~static_cast<uint32_t>(*inner));
                case TokenKind::Bang: return *inner == 0 ? 1 : 0;
                default: return std::nullopt;
                }
            } else if constexpr (std::is_same_v<T, Expr::Binary>) {
                const std::optional<int> lhs = node.lhs ? fold_int(*node.lhs) : std::nullopt;
                const std::optional<int> rhs = node.rhs ? fold_int(*node.rhs) : std::nullopt;
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                const int64_t a = *lhs;
                const int64_t b = *rhs;
                switch (node.op) {
                case TokenKind::Plus: return wrap(a + b);
                case TokenKind::Minus: return wrap(a - b);
                case TokenKind::Star: return wrap(a * b);
                case TokenKind::Slash: return b == 0 ? std::nullopt : std::optional<int>(wrap(a / b));
                case TokenKind::Percent: return b == 0 ? std::nullopt : std::optional<int>(wrap(a % b));
                case TokenKind::Amp: return wrap(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
                case TokenKind::Pipe: return wrap(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
                case TokenKind::Caret: return wrap(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
                case TokenKind::LtLt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return wrap(static_cast<uint32_t>(a) << static_cast<uint32_t>(b));
                case TokenKind::GtGt:
                    if (b < 0 || b >= 32) return std::nullopt;
                    return static_cast<int>(static_cast<uint32_t>(static_cast<int32_t>(a) >> static_cast<uint32_t>(b)));
                case TokenKind::EqEq: return a == b ? 1 : 0;
                case TokenKind::BangEq: return a != b ? 1 : 0;
                case TokenKind::Lt: return a < b ? 1 : 0;
                case TokenKind::Gt: return a > b ? 1 : 0;
                case TokenKind::Le: return a <= b ? 1 : 0;
                case TokenKind::Ge: return a >= b ? 1 : 0;
                case TokenKind::AmpAmp: return (*lhs != 0 && *rhs != 0) ? 1 : 0;
                case TokenKind::PipePipe: return (*lhs != 0 || *rhs != 0) ? 1 : 0;
                default: return std::nullopt;
                }
            } else {
                return std::nullopt;
            }
        },
        expr.data);
}

void Checker::check_stmt(const Stmt &stmt) {
    std::visit(
        [&](const auto &node) {
            using T = std::decay_t<decltype(node)>;
            if constexpr (std::is_same_v<T, Stmt::Block>) {
                push();
                for (const auto &child : node.statements) {
                    if (child) {
                        check_stmt(*child);
                    }
                }
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Return>) {
                auto match_return = [&](const Type &got, const Type &want, const Expr &expr) {
                    const bool null_pointer = want.kind == TypeKind::Pointer &&
                                              ((got.kind == TypeKind::Int && is_null_constant(expr)) ||
                                               got.kind == TypeKind::Nullptr);
                    const bool scalar = is_scalar(got) && is_scalar(want);
                    if (!null_pointer && !scalar &&
                        (want.kind == TypeKind::Void ||
                         (got.kind != TypeKind::Invalid && want.kind != TypeKind::Invalid && !same_type(got, want)))) {
                        error(node.loc, "return type");
                    }
                };
                std::vector<const Expr *> exprs;
                if (node.value) {
                    exprs.push_back(node.value.get());
                }
                for (const auto &extra : node.extra) {
                    if (extra) {
                        exprs.push_back(extra.get());
                    }
                }
                if (return_type_.kind == TypeKind::Tuple) {
                    if (exprs.size() != return_type_.results.size()) {
                        error(node.loc, "return type");
                    }
                    for (std::size_t i = 0; i < exprs.size(); ++i) {
                        const Type got = check_expr(*exprs[i], true);
                        if (i < return_type_.results.size()) {
                            match_return(got, return_type_.results[i], *exprs[i]);
                        }
                    }
                } else if (!node.extra.empty()) {
                    error(node.loc, "return type");
                    for (const Expr *expr : exprs) {
                        check_expr(*expr, true);
                    }
                } else if (!node.value) {
                    if (return_type_.kind != TypeKind::Void && return_type_.kind != TypeKind::Invalid) {
                        error(node.loc, "return type");
                    }
                } else {
                    match_return(check_expr(*node.value, true), return_type_, *node.value);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Expression>) {
                if (node.expr) {
                    check_expr(*node.expr, false);
                }
            } else if constexpr (std::is_same_v<T, Stmt::If>) {
                if (node.cond) {
                    expect_integer(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                if (node.then_arm) {
                    check_stmt(*node.then_arm);
                }
                if (node.else_arm) {
                    check_stmt(*node.else_arm);
                }
            } else if constexpr (std::is_same_v<T, Stmt::While>) {
                if (node.cond) {
                    expect_integer(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                ++loop_depth_;
                if (node.body) {
                    check_stmt(*node.body);
                }
                --loop_depth_;
            } else if constexpr (std::is_same_v<T, Stmt::DoWhile>) {
                ++loop_depth_;
                if (node.body) {
                    check_stmt(*node.body);
                }
                --loop_depth_;
                if (node.cond) {
                    expect_integer(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
            } else if constexpr (std::is_same_v<T, Stmt::Switch>) {
                if (node.cond) {
                    expect(check_expr(*node.cond, true), type_int(), expr_loc(*node.cond));
                }
                switches_.push_back({});
                if (node.body) {
                    check_stmt(*node.body);
                }
                switches_.pop_back();
            } else if constexpr (std::is_same_v<T, Stmt::Case>) {
                if (switches_.empty()) {
                    error(node.loc, node.is_default ? "default outside switch" : "case outside switch");
                } else if (node.is_default) {
                    if (switches_.back().has_default) {
                        error(node.loc, "redefinition");
                    } else {
                        switches_.back().has_default = true;
                    }
                } else if (node.value) {
                    const Type got = check_expr(*node.value, true);
                    if (got.kind != TypeKind::Invalid && !same_type(got, type_int())) {
                        error(expr_loc(*node.value), "type mismatch");
                    }
                    if (same_type(got, type_int())) {
                        const std::optional<int> folded = fold_int(*node.value);
                        if (!folded) {
                            error(node.loc, "not supported yet");
                        } else if (!switches_.back().values.insert(*folded).second) {
                            error(expr_loc(*node.value), "redefinition");
                        }
                    }
                }
                if (node.stmt) {
                    check_stmt(*node.stmt);
                }
            } else if constexpr (std::is_same_v<T, Stmt::For>) {
                push();
                ++loop_depth_;
                if (node.init) {
                    check_stmt(*node.init);
                }
                if (node.cond) {
                    expect_integer(check_expr(*node.cond, true), expr_loc(*node.cond));
                }
                if (node.step) {
                    check_expr(*node.step, false);
                }
                if (node.body) {
                    check_stmt(*node.body);
                }
                --loop_depth_;
                pop();
            } else if constexpr (std::is_same_v<T, Stmt::Break>) {
                if (loop_depth_ == 0 && switches_.empty()) {
                    error(node.loc, "break outside loop");
                }
            } else if constexpr (std::is_same_v<T, Stmt::Continue>) {
                if (loop_depth_ == 0) {
                    error(node.loc, "continue outside loop");
                }
            } else if constexpr (std::is_same_v<T, Stmt::Func>) {
                if (node.decl) {
                    check_local_function(*node.decl);
                }
            } else if constexpr (std::is_same_v<T, Stmt::Defer>) {
                if (node.call == nullptr || !std::holds_alternative<Expr::Call>(node.call->data)) {
                    if (node.call) {
                        check_expr(*node.call, false);
                    }
                    error(node.loc, "type mismatch");
                } else {
                    checking_defer_ = true;
                    check_expr(*node.call, false);
                    checking_defer_ = false;
                }
            } else if constexpr (std::is_same_v<T, Stmt::Bind>) {
                if (node.value == nullptr || node.names.empty()) {
                    error(node.loc, "type mismatch");
                    return;
                }
                const Type rhs = check_expr(*node.value, true);
                if (rhs.kind == TypeKind::Invalid) {
                    return;
                }
                std::vector<Type> parts;
                if (node.names.size() == 1) {
                    if (rhs.kind == TypeKind::Tuple) {
                        error(node.loc, "type mismatch");
                        return;
                    }
                    parts.push_back(rhs);
                } else if (rhs.kind != TypeKind::Tuple || rhs.results.size() != node.names.size()) {
                    error(node.loc, "type mismatch");
                    return;
                } else {
                    parts = rhs.results;
                }
                bool duplicate = false;
                for (std::size_t i = 0; i < node.names.size(); ++i) {
                    for (std::size_t j = i + 1; j < node.names.size(); ++j) {
                        if (node.names[i] == node.names[j]) {
                            error(node.name_locs[i], "redefinition");
                            duplicate = true;
                        }
                    }
                }
                if (duplicate) {
                    return;
                }
                auto in_current = [&](const std::string &name) -> Symbol * {
                    if (scopes_.empty()) {
                        return nullptr;
                    }
                    const auto found = scopes_.back().symbols.find(name);
                    if (found == scopes_.back().symbols.end()) {
                        return nullptr;
                    }
                    return &found->second;
                };
                auto assignable = [&](const Symbol &symbol, SourceLoc loc, const Type &got) {
                    if (symbol.is_function || symbol.is_package || symbol.is_constant || symbol.type.kind == TypeKind::Array) {
                        error(loc, "not an lvalue");
                        return;
                    }
                    if (captured(symbol)) {
                        error(loc, "not supported yet");
                        return;
                    }
                    expect_value(got, symbol.type, *node.value, loc);
                };
                auto declare_new = [&](std::size_t index) {
                    const SourceLoc loc = index < node.name_locs.size() ? node.name_locs[index] : node.loc;
                    Symbol symbol;
                    symbol.name = node.names[index];
                    symbol.type = parts[index];
                    symbol.is_auto = true;
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    symbol.loc = loc;
                    declare(std::move(symbol), loc);
                };
                if (node.is_var) {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (in_current(node.names[i]) != nullptr) {
                            error(loc, "redefinition");
                        } else {
                            declare_new(i);
                        }
                    }
                } else if (node.is_define) {
                    bool any_new = false;
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        if (const Symbol *existing = in_current(node.names[i])) {
                            assignable(*existing, loc, parts[i]);
                        } else {
                            any_new = true;
                        }
                    }
                    if (!any_new) {
                        error(node.loc, "type mismatch");
                    } else {
                        for (std::size_t i = 0; i < node.names.size(); ++i) {
                            if (in_current(node.names[i]) == nullptr) {
                                declare_new(i);
                            }
                        }
                    }
                } else {
                    for (std::size_t i = 0; i < node.names.size(); ++i) {
                        const SourceLoc loc = i < node.name_locs.size() ? node.name_locs[i] : node.loc;
                        const Symbol *existing = lookup(node.names[i]);
                        if (existing == nullptr) {
                            error(loc, "undeclared identifier");
                        } else {
                            assignable(*existing, loc, parts[i]);
                        }
                    }
                }
            } else if constexpr (std::is_same_v<T, Stmt::Decl>) {
                for (const VarDecl &var : node.vars) {
                    Type type = object_type(var.type_name, var.indirections, var.is_array, var.array_len, var.loc, false,
                                            var.type_package, var.is_struct, var.is_enum);
                    Symbol symbol;
                    symbol.name = var.name;
                    symbol.type = type;
                    symbol.is_auto = true;
                    symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
                    declare(std::move(symbol), var.loc);
                    if (var.is_array && var.init) {
                        error(var.loc, "not supported yet");
                    } else if (var.init) {
                        expect_value(check_expr(*var.init, true), type, *var.init, expr_loc(*var.init));
                    }
                }
            }
        },
        stmt.data);
}

void Checker::check_function(const FunctionDecl &fn) {
    push_function();
    const Type saved = return_type_;
    const int saved_loops = loop_depth_;
    auto saved_switches = std::move(switches_);
    loop_depth_ = 0;
    switches_.clear();
    return_type_ = function_result(fn);
    for (const Param &param : fn.params) {
        Type type = object_type(param.type_name, param.indirections, param.is_array, param.array_len, param.loc, true,
                                param.type_package, param.is_struct, param.is_enum, param.is_receiver);
        Symbol symbol;
        symbol.name = param.name;
        symbol.type = std::move(type);
        symbol.is_auto = true;
        symbol.function_id = scopes_.empty() ? 0 : scopes_.back().function_id;
        declare(std::move(symbol), param.loc);
    }
    if (fn.body) {
        if (const auto *block = std::get_if<Stmt::Block>(&fn.body->data)) {
            for (const auto &child : block->statements) {
                if (child) {
                    check_stmt(*child);
                }
            }
        } else {
            check_stmt(*fn.body);
        }
    }
    switches_ = std::move(saved_switches);
    loop_depth_ = saved_loops;
    return_type_ = saved;
    pop();
}

void Checker::declare_globals(const TranslationUnit &unit) {
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            std::vector<Type> params;
            for (const Param &param : fn->params) {
                params.push_back(object_type(param.type_name, param.indirections, param.is_array, param.array_len,
                                             param.loc, true, param.type_package, param.is_struct, param.is_enum,
                                             param.is_receiver));
            }
            Type result = function_result(*fn);
            Type signature = type_function(std::move(result), std::move(params), fn->variadic);
            if (signature.result != nullptr && mentions_trait(*signature.result)) {
                error(fn->loc, "not supported yet");
            }
            bool generic = false;
            for (const Type &param : signature.params) {
                if (mentions_trait(param)) {
                    generic = true;
                }
            }
            if (fn->is_extern && fn->results.size() > 1) {
                error(fn->loc, "not supported yet");
            }
            if (generic && signature.result != nullptr && signature.result->kind == TypeKind::Tuple) {
                error(fn->loc, "not supported yet");
            }
            if (generic && (!fn->method_struct.empty() || fn->exported || fn->is_extern || fn->variadic)) {
                error(fn->loc, "not supported yet");
            }
            if (!fn->method_struct.empty()) {
                if (fn->exported || fn->is_extern) {
                    error(fn->loc, "not supported yet");
                }
                if (package_trait(file_id_, fn->method_struct) >= 0) {
                    error(fn->loc, "type mismatch");
                }
                const int receiver_refs = fn->params.empty() ? -1 : fn->params.front().indirections;
                const bool receiver_ok = !fn->params.empty() && fn->params.front().is_receiver &&
                                         fn->params.front().is_struct && (receiver_refs == 0 || receiver_refs == 1) &&
                                         !fn->params.front().is_array && fn->params.front().type_package.empty() &&
                                         fn->params.front().type_name == fn->method_struct;
                if (!receiver_ok) {
                    error(fn->loc, "type mismatch");
                }
                auto &table = methods_[file_id_][fn->method_struct];
                const auto found = table.find(fn->name);
                if (found != table.end()) {
                    if (!same_type(found->second.type, signature) || (found->second.has_body && fn->body)) {
                        error(fn->loc, "redefinition");
                    } else if (fn->body) {
                        found->second.has_body = true;
                    }
                } else {
                    const int struct_id = find_tag(file_id_, fn->method_struct);
                    if (struct_id >= 0 && find_method(struct_id, fn->name) != nullptr) {
                        error(fn->loc, "redefinition");
                    } else {
                        table.emplace(fn->name, MethodInfo{std::move(signature), fn->body != nullptr});
                    }
                }
                continue;
            }
            Type alias;
            if (package_typedef(file_id_, fn->name, alias) || package_trait(file_id_, fn->name) >= 0) {
                error(fn->loc, "redefinition");
                continue;
            }
            if (Symbol *existing = lookup_scope(fn->name)) {
                if (!existing->is_function || !same_type(existing->type, signature) ||
                    (existing->has_body && fn->body)) {
                    error(fn->loc, "redefinition");
                } else if (fn->body) {
                    existing->has_body = true;
                    note_body(fn->name);
                    if (fn->is_static) {
                        const auto table = statics_.find(file_id_);
                        if (table != statics_.end()) {
                            const auto prior = table->second.find(fn->name);
                            if (prior != table->second.end()) {
                                prior->second.has_body = true;
                            }
                        }
                    }
                }
                continue;
            }
            if (shared_has(fn->name)) {
                error(fn->loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = std::move(signature);
            symbol.is_function = true;
            symbol.has_body = fn->body != nullptr;
            if (fn->is_static) {
                statics_[file_id_].insert_or_assign(fn->name, symbol);
                declare(std::move(symbol), fn->loc);
            } else if (remember_shared(symbol, fn->loc)) {
                declare(std::move(symbol), fn->loc);
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            Type alias;
            if (package_typedef(file_id_, var->name, alias) || package_trait(file_id_, var->name) >= 0) {
                error(var->loc, "redefinition");
                continue;
            }
            if (lookup_scope(var->name) != nullptr || shared_has(var->name)) {
                error(var->loc, "redefinition");
                continue;
            }
            Type type = object_type(var->type_name, var->indirections, var->is_array, var->array_len, var->loc, false,
                                    var->type_package, var->is_struct, var->is_enum);
            Symbol symbol;
            symbol.name = var->name;
            symbol.type = std::move(type);
            if (var->is_static) {
                statics_[file_id_].insert_or_assign(var->name, symbol);
                declare(std::move(symbol), var->loc);
            } else if (remember_shared(symbol, var->loc)) {
                declare(std::move(symbol), var->loc);
            }
        }
    }
}

void Checker::check_globals(const TranslationUnit &unit) {
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            check_function(*fn);
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (var->is_array && var->init) {
                error(var->loc, "not supported yet");
            } else if (var->init) {
                const Symbol *symbol = lookup(var->name);
                const Type want = symbol != nullptr ? symbol->type : type_invalid();
                if (mentions_trait(want)) {
                    error(var->loc, "not supported yet");
                    continue;
                }
                expect_value(check_expr(*var->init, true), want, *var->init, expr_loc(*var->init));
            }
        }
    }
}

void Checker::analyze(const TranslationUnit &unit) {
    register_structs(unit, 0);
    register_enums(unit, 0);
    register_traits(unit, 0);
    register_typedefs(unit, 0);
    publish_exports(unit, 0);
    analyze_unit(unit, 0, nullptr);
}

const PackageNode *Checker::find_node(uint32_t file_id) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const PackageNode &node : graph_->nodes) {
        if (node.file_id == file_id) {
            return &node;
        }
    }
    return nullptr;
}

const Symbol *Checker::find_export(uint32_t file_id, const std::string &name) const {
    for (const uint32_t id : files_in_package(file_id)) {
        const auto file = exports_.find(id);
        if (file == exports_.end()) {
            continue;
        }
        const auto symbol = file->second.find(name);
        if (symbol != file->second.end()) {
            return &symbol->second;
        }
    }
    return nullptr;
}

const PackageNode *Checker::target_of(const PackageNode &from, const ImportDecl &import) const {
    if (graph_ == nullptr) {
        return nullptr;
    }
    for (const ImportEdge &edge : graph_->edges) {
        if (edge.from_file == from.file_id && edge.spec == import.path && edge.loc.file == import.loc.file &&
            edge.loc.offset == import.loc.offset) {
            return find_node(edge.to_file);
        }
    }
    return nullptr;
}

Checker::SelectorTarget Checker::resolve_selector(const Expr::Selector &selector) {
    const auto *base = std::get_if<Expr::Name>(&selector.base->data);
    if (base == nullptr) {
        return {};
    }
    const Symbol *symbol = lookup(base->text);
    if (symbol == nullptr) {
        error(base->loc, "undeclared identifier");
        return {SelectorTarget::Kind::Failed, nullptr};
    }
    if (!symbol->is_package) {
        return {};
    }
    const Symbol *member = find_export(symbol->package_file, selector.field);
    if (member == nullptr) {
        error(selector.loc, selector.field + " is not exported");
        return {SelectorTarget::Kind::Failed, nullptr};
    }
    return {SelectorTarget::Kind::Package, member};
}

void Checker::publish_exports(const TranslationUnit &unit, uint32_t file_id) {
    file_id_ = file_id;
    for (const TopDecl &decl : unit.decls) {
        if (const auto *fn = std::get_if<FunctionDecl>(&decl.data)) {
            if (!fn->method_struct.empty()) {
                continue;
            }
            if (fn->exported && fn->is_extern) {
                error(fn->loc, "export extern");
            }
            if (!fn->exported || fn->is_static || fn->is_extern || fn->name.empty()) {
                continue;
            }
            bool generic = false;
            for (const Param &param : fn->params) {
                if (!param.is_struct && !param.is_enum && !param.is_array && param.type_package.empty() &&
                    package_trait(file_id, param.type_name) >= 0) {
                    generic = true;
                }
            }
            if (generic) {
                continue;
            }
            std::vector<Type> params;
            for (const Param &param : fn->params) {
                params.push_back(published_object(param.type_name, param.indirections, param.is_array, param.array_len,
                                                 param.loc, true, param.type_package, param.is_struct, param.is_enum));
            }
            Type result;
            if (fn->results.size() > 1) {
                std::vector<Type> parts;
                parts.reserve(fn->results.size());
                for (const ResultSlot &slot : fn->results) {
                    parts.push_back(published_result(slot.type_name, slot.indirections, fn->loc, slot.type_package,
                                                     slot.is_struct, slot.is_enum));
                }
                result = type_tuple(std::move(parts));
            } else {
                result = published_result(fn->return_type, fn->indirections, fn->loc, fn->type_package, fn->is_struct,
                                          fn->is_enum);
            }
            Symbol symbol;
            symbol.name = fn->name;
            symbol.type = type_function(std::move(result), std::move(params), fn->variadic);
            symbol.is_function = true;
            symbol.has_body = fn->body != nullptr;
            auto &table = exports_[file_id];
            const auto found = table.find(fn->name);
            if (found == table.end()) {
                if (exported_elsewhere(fn->name)) {
                    error(fn->loc, "redefinition");
                } else {
                    table.emplace(fn->name, std::move(symbol));
                }
            } else if (found->second.is_type || !found->second.is_function) {
                error(fn->loc, "redefinition");
            } else if (fn->body) {
                found->second.has_body = true;
            }
        } else if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
            if (var->exported && var->is_extern) {
                error(var->loc, "export extern");
            }
            if (!var->exported || var->is_static || var->is_extern || var->name.empty()) {
                continue;
            }
            if (exports_[file_id].contains(var->name)) {
                continue;
            }
            if (exported_elsewhere(var->name)) {
                error(var->loc, "redefinition");
                continue;
            }
            Symbol symbol;
            symbol.name = var->name;
            symbol.type = published_object(var->type_name, var->indirections, var->is_array, var->array_len, var->loc,
                                           false, var->type_package, var->is_struct, var->is_enum);
            exports_[file_id].emplace(var->name, std::move(symbol));
        }
    }
}

void Checker::bind_imports(const PackageNode &node) {
    for (const ImportDecl &import : node.unit.imports) {
        const PackageNode *target = target_of(node, import);
        if (target == nullptr) {
            continue;
        }
        if (import.form == ImportDecl::Form::Selective) {
            for (const std::string &name : import.names) {
                const Symbol *exported = find_export(target->file_id, name);
                if (exported == nullptr) {
                    error(import.loc, name + " is not exported");
                    continue;
                }
                if (exported->is_type) {
                    if (exported->is_enum) {
                        if (enums_[file_id_].contains(name) && typedefs_[file_id_].contains(name)) {
                            continue;
                        }
                        if (name_taken(name) || enums_[file_id_].contains(name) || typedefs_[file_id_].contains(name)) {
                            error(import.loc, "redefinition");
                        } else {
                            enums_[file_id_].insert(name);
                            typedefs_[file_id_][name] = type_int();
                        }
                        continue;
                    }
                    if (exported->is_typedef) {
                        const auto have = typedefs_[file_id_].find(name);
                        if (have != typedefs_[file_id_].end() && same_type(have->second, exported->type)) {
                            if (exported->is_struct_tag && exported->type.kind == TypeKind::Struct &&
                                !tags_[file_id_].contains(name)) {
                                tags_[file_id_][name] = exported->type.struct_id;
                            }
                            continue;
                        }
                        if (name_taken(name) || have != typedefs_[file_id_].end()) {
                            error(import.loc, "redefinition");
                            continue;
                        }
                        typedefs_[file_id_][name] = exported->type;
                        if (exported->is_struct_tag && exported->type.kind == TypeKind::Struct &&
                            !tags_[file_id_].contains(name)) {
                            tags_[file_id_][name] = exported->type.struct_id;
                        }
                        continue;
                    }
                    if (exported->type.kind == TypeKind::Struct) {
                        const auto local = tags_[file_id_].find(name);
                        if (local != tags_[file_id_].end() && local->second == exported->type.struct_id) {
                            continue;
                        }
                        if (name_taken(name) || find_tag(file_id_, name) >= 0) {
                            error(import.loc, "redefinition");
                        } else {
                            tags_[file_id_][name] = exported->type.struct_id;
                        }
                        continue;
                    }
                    const auto have = typedefs_[file_id_].find(name);
                    if (have != typedefs_[file_id_].end() && same_type(have->second, exported->type)) {
                        continue;
                    }
                    if (name_taken(name) || have != typedefs_[file_id_].end()) {
                        error(import.loc, "redefinition");
                    } else {
                        typedefs_[file_id_][name] = exported->type;
                    }
                    continue;
                }
                if (name_taken(name)) {
                    error(import.loc, "redefinition");
                    continue;
                }
                Symbol local = *exported;
                local.name = name;
                local.is_package = false;
                declare(std::move(local), import.loc);
            }
            continue;
        }
        const std::string binding = import.form == ImportDecl::Form::Alias ? import.alias : target->qualifier;
        if (binding.empty()) {
            continue;
        }
        if (name_taken(binding)) {
            error(import.loc, "redefinition");
            continue;
        }
        auto &table = scopes_.back().symbols;
        if (const auto found = table.find(binding); found != table.end()) {
            if (found->second.is_package) {
                error(import.loc, "duplicate package name");
            } else {
                error(import.loc, "redefinition");
            }
            continue;
        }
        Symbol symbol;
        symbol.name = binding;
        symbol.is_package = true;
        symbol.package_file = target->file_id;
        table.emplace(binding, std::move(symbol));
    }
}

void Checker::analyze_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    inject_enum_consts();
    declare_globals(unit);
    check_globals(unit);
    pop();
}

void Checker::prepare_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    inject_enum_consts();
    declare_globals(unit);
    pop();
}

void Checker::check_unit(const TranslationUnit &unit, uint32_t file_id, const PackageNode *node) {
    file_id_ = file_id;
    push();
    if (node != nullptr) {
        bind_imports(*node);
    }
    inject_enum_consts();
    const auto locals = statics_.find(file_id_);
    if (locals != statics_.end()) {
        for (const auto &[name, symbol] : locals->second) {
            (void)name;
            declare(symbol, {});
        }
    }
    check_globals(unit);
    pop();
}

void Checker::analyze_graph(const PackageGraph &graph) {
    graph_ = &graph;
    if (!graph.nodes.empty()) {
        const uint32_t entry = graph.nodes.front().file_id;
        for (const PackageNode &node : graph.nodes) {
            if (node.file_id == entry) {
                continue;
            }
            for (const TopDecl &decl : node.unit.decls) {
                const auto *fn = std::get_if<FunctionDecl>(&decl.data);
                if (fn != nullptr && fn->name == "main" && fn->body != nullptr) {
                    error(fn->loc, "multiple main");
                }
            }
        }
    }
    for (const PackageNode &node : graph.nodes) {
        declare_struct_tags(node.unit, node.file_id);
    }
    define_struct_fields();
    for (const PackageNode &node : graph.nodes) {
        register_enums(node.unit, node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        register_traits(node.unit, node.file_id);
    }
    struct PendingTypedef {
        uint32_t file_id = 0;
        const TypedefDecl *item = nullptr;
        bool done = false;
    };
    std::vector<PendingTypedef> typedefs;
    for (const PackageNode &node : graph.nodes) {
        file_id_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            const auto *item = std::get_if<TypedefDecl>(&decl.data);
            if (item == nullptr || item->name.empty()) {
                continue;
            }
            if (typedef_target_ready(*item)) {
                register_one_typedef(*item);
            } else {
                typedefs.push_back(PendingTypedef{node.file_id, item, false});
            }
        }
    }
    bool progress = true;
    while (progress) {
        progress = false;
        for (PendingTypedef &item : typedefs) {
            if (item.done || item.item == nullptr) {
                continue;
            }
            file_id_ = item.file_id;
            if (!typedef_target_ready(*item.item)) {
                continue;
            }
            register_one_typedef(*item.item);
            item.done = true;
            progress = true;
        }
    }
    for (PendingTypedef &item : typedefs) {
        if (item.done || item.item == nullptr) {
            continue;
        }
        file_id_ = item.file_id;
        register_one_typedef(*item.item);
    }
    for (const PackageNode &node : graph.nodes) {
        publish_exports(node.unit, node.file_id);
    }
    for (const PackageNode &node : graph.nodes) {
        prepare_unit(node.unit, node.file_id, &node);
    }
    for (const PackageNode &node : graph.nodes) {
        check_unit(node.unit, node.file_id, &node);
    }
    graph_ = nullptr;
}

} // namespace

void analyze(const TranslationUnit &unit, DiagnosticEngine &diags) {
    Checker checker(diags);
    checker.analyze(unit);
}

void analyze(const PackageGraph &graph, DiagnosticEngine &diags) {
    Checker checker(diags);
    checker.analyze_graph(graph);
}

} // namespace tlc
