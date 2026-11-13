// Running one comptime function by compiling it and calling the result.
#include "gen/gen.hpp"

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tlc {
namespace {

constexpr const char *kEvalName = "tlc.comptime.eval";
constexpr const char *kBodyName = "tlc.comptime.body";
constexpr const char *kPoolName = "tlc.comptime.pool";
constexpr const char *kUsedName = "tlc.comptime.used";

// One row of a comptime array. `max_shells` is `[N]T`, `[N][N]T`, or `[N][N][N]T`.
// `i8` and `i16` stop at two shells. Four shells stay out.
struct ElemShape {
    const char *name;
    int bits;
    bool is_unsigned;
    bool is_float;
    bool is_bool;
    int max_shells;
};

const ElemShape *elem_shape(const std::string &name) {
    static const ElemShape table[] = {
        {"i32", 32, false, false, false, 3}, {"i64", 64, false, false, false, 3},
        {"u32", 32, true, false, false, 3},  {"u64", 64, true, false, false, 3},
        {"i16", 16, false, false, false, 2}, {"u16", 16, true, false, false, 2},
        {"i8", 8, false, false, false, 2},   {"u8", 8, true, false, false, 2},
        {"f32", 32, false, true, false, 3},  {"f64", 64, false, true, false, 3},
        {"bool", 1, false, false, true, 3},
    };
    for (const ElemShape &item : table) {
        if (name == item.name) {
            return &item;
        }
    }
    return nullptr;
}

const char *int_elem_name(int bits, bool is_unsigned) {
    static const char *names[] = {"i64", "u64", "i32", "u32", "i16", "u16", "i8", "u8"};
    for (const char *name : names) {
        const ElemShape *item = elem_shape(name);
        if (item != nullptr && item->bits == bits && item->is_unsigned == is_unsigned) {
            return item->name;
        }
    }
    return nullptr;
}
constexpr const char *kFailName = "tlc.comptime.fail";
constexpr uint32_t kPoolBytes = 65536;

} // namespace

std::vector<PackageNode> CodeGen::take_comptime_specs(
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs) const {
    std::vector<PackageNode> held;
    std::unordered_map<uint32_t, std::size_t> index;
    for (const auto &spec : specs) {
        const FunctionDecl *fn = spec.second;
        if (fn == nullptr || fn->name.empty() || fn->body == nullptr || !fn->type_params.empty() || fn->is_macro) {
            continue;
        }
        const auto found = index.find(spec.first);
        std::size_t at = 0;
        if (found == index.end()) {
            PackageNode node;
            node.file_id = spec.first;
            if (const PackageNode *home = find_node(spec.first)) {
                node.qualifier = home->qualifier;
                node.path = home->path;
                node.package_key = home->package_key;
            }
            at = held.size();
            index.emplace(spec.first, at);
            held.push_back(std::move(node));
        } else {
            at = found->second;
        }
        TopDecl decl;
        decl.data = clone_function(*fn);
        held[at].unit.decls.push_back(std::move(decl));
    }
    return held;
}

bool CodeGen::define_comptime(const PackageGraph &graph, const FunctionDecl &fn, uint32_t file,
                              const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs, int float_bits,
                              int int_bits) {
    if (graph.nodes.empty() || fn.body == nullptr) {
        return false;
    }
    eval_ = true;
    install_eval_fault();
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_functions(node);
    }
    // A specialization queued while checking is not in the package yet. Keep the copies alive for emission.
    const std::vector<PackageNode> held_specs = take_comptime_specs(specs);
    for (const PackageNode &node : held_specs) {
        declare_functions(node);
    }
    for (const PackageNode &node : graph.nodes) {
        declare_globals(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    // A global that is not part of this call may fail to fold. The function's own emission is the gate.
    const std::size_t prepared = diags_.size();
    const PackageNode *home = find_node(file);
    if (home == nullptr) {
        graph_ = nullptr;
        return false;
    }
    current_file_ = file;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(*home);
    llvm::FunctionType *ftype = function_type(fn, file);
    // A `bool` result is `i1`. The evaluator still reads an `i32` of 0 or 1.
    // An `f32` result is a `float`. An `f64` result is a `double`. An `i64` result is that integer.
    const bool bool_result = ftype != nullptr && ftype->getReturnType()->isIntegerTy(1);
    const bool f64_result = ftype != nullptr && ftype->getReturnType()->isDoubleTy();
    const bool f32_result = ftype != nullptr && ftype->getReturnType()->isFloatTy();
    const bool i32_result = ftype != nullptr && ftype->getReturnType()->isIntegerTy(32);
    const bool i64_result = ftype != nullptr && ftype->getReturnType()->isIntegerTy(64);
    const bool float_ok = float_bits >= 64 ? f64_result : (float_bits == 32 ? f32_result : (bool_result || i32_result));
    const bool result_ok = int_bits >= 64 ? i64_result : float_ok;
    if (ftype == nullptr || ftype->getNumParams() != 0 || !result_ok) {
        pop();
        graph_ = nullptr;
        return false;
    }
    if (module_.getNamedValue(kEvalName) != nullptr ||
        (bool_result && module_.getNamedValue(kBodyName) != nullptr)) {
        pop();
        graph_ = nullptr;
        return false;
    }
    auto *function = llvm::Function::Create(ftype, llvm::GlobalValue::ExternalLinkage,
                                            bool_result ? kBodyName : kEvalName, module_);
    function->setCallingConv(llvm::CallingConv::C);
    emit_function_body(fn, function, 0);
    pop();

    // A call in the body is a declaration until that callee is emitted. Repeat while new calls appear.
    for (int guard = 0; guard < 64; ++guard) {
        struct Pending {
            uint32_t file = 0;
            const FunctionDecl *decl = nullptr;
            llvm::Function *function = nullptr;
            int depth = 0;
        };
        std::vector<Pending> pending;
        for (auto &entry : files_) {
            for (auto &symbol : entry.second.functions) {
                auto *callee = llvm::dyn_cast_or_null<llvm::Function>(symbol.second.value);
                if (callee == nullptr || !callee->isDeclaration() || callee->use_empty()) {
                    continue;
                }
                const FunctionDecl *decl = symbol.second.decl;
                if (decl == nullptr || decl->body == nullptr || symbol.second.is_generic ||
                    function_is_generic(entry.first, *decl)) {
                    continue;
                }
                pending.push_back(Pending{entry.first, decl, callee, symbol.second.depth});
            }
        }
        if (pending.empty()) {
            break;
        }
        for (const Pending &item : pending) {
            const PackageNode *node = find_node(item.file);
            if (node == nullptr || !item.function->isDeclaration()) {
                continue;
            }
            current_file_ = item.file;
            packages_.clear();
            push();
            bind_package_globals();
            bind_imports(*node);
            emit_function_body(*item.decl, item.function, item.depth);
            pop();
            if (item.function->isDeclaration()) {
                graph_ = nullptr;
                return false;
            }
        }
    }
    // An unemitted function with internal linkage is not a valid declaration. Drop the ones nothing calls.
    std::vector<llvm::Function *> drop;
    for (llvm::Function &item : module_) {
        if (!item.isDeclaration() || &item == function) {
            continue;
        }
        if (!item.use_empty()) {
            graph_ = nullptr;
            return false;
        }
        drop.push_back(&item);
    }
    for (llvm::Function *item : drop) {
        item->eraseFromParent();
    }
    if (bool_result && !function->isDeclaration()) {
        auto *wrap_ty = llvm::FunctionType::get(i32(), {}, false);
        auto *wrapper = llvm::Function::Create(wrap_ty, llvm::GlobalValue::ExternalLinkage, kEvalName, module_);
        wrapper->setCallingConv(llvm::CallingConv::C);
        auto *entry = llvm::BasicBlock::Create(context_, "entry", wrapper);
        builder_.SetInsertPoint(entry);
        llvm::CallInst *call = builder_.CreateCall(function, {});
        call->setCallingConv(llvm::CallingConv::C);
        builder_.CreateRet(builder_.CreateZExt(call, i32()));
    }
    graph_ = nullptr;
    return diags_.size() == prepared && !function->isDeclaration();
}

bool CodeGen::define_comptime_string(const PackageGraph &graph, const FunctionDecl &fn, uint32_t file,
                                     const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs) {
    if (graph.nodes.empty() || fn.body == nullptr) {
        return false;
    }
    eval_ = true;
    install_eval_fault();
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_functions(node);
    }
    const std::vector<PackageNode> held_specs = take_comptime_specs(specs);
    for (const PackageNode &node : held_specs) {
        declare_functions(node);
    }
    for (const PackageNode &node : graph.nodes) {
        declare_globals(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    const std::size_t prepared = diags_.size();
    const PackageNode *home = find_node(file);
    const auto give_up = [&]() {
        pop();
        graph_ = nullptr;
        return false;
    };
    if (home == nullptr) {
        graph_ = nullptr;
        return false;
    }
    current_file_ = file;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(*home);
    llvm::FunctionType *ftype = function_type(fn, file);
    if (ftype == nullptr || ftype->getNumParams() != 0 || ftype->getReturnType() != slice_type()) {
        return give_up();
    }
    if (module_.getNamedValue(kBodyName) != nullptr || module_.getNamedValue(kEvalName) != nullptr) {
        return give_up();
    }
    auto *body = llvm::Function::Create(ftype, llvm::GlobalValue::ExternalLinkage, kBodyName, module_);
    body->setCallingConv(llvm::CallingConv::C);
    emit_function_body(fn, body, 0);
    pop();

    for (int guard = 0; guard < 64; ++guard) {
        struct Pending {
            uint32_t file = 0;
            const FunctionDecl *decl = nullptr;
            llvm::Function *function = nullptr;
            int depth = 0;
        };
        std::vector<Pending> pending;
        for (auto &entry : files_) {
            for (auto &symbol : entry.second.functions) {
                auto *callee = llvm::dyn_cast_or_null<llvm::Function>(symbol.second.value);
                if (callee == nullptr || !callee->isDeclaration() || callee->use_empty()) {
                    continue;
                }
                const FunctionDecl *decl = symbol.second.decl;
                if (decl == nullptr || decl->body == nullptr || symbol.second.is_generic ||
                    function_is_generic(entry.first, *decl)) {
                    continue;
                }
                pending.push_back(Pending{entry.first, decl, callee, symbol.second.depth});
            }
        }
        if (pending.empty()) {
            break;
        }
        for (const Pending &item : pending) {
            const PackageNode *node = find_node(item.file);
            if (node == nullptr || !item.function->isDeclaration()) {
                continue;
            }
            current_file_ = item.file;
            packages_.clear();
            push();
            bind_package_globals();
            bind_imports(*node);
            emit_function_body(*item.decl, item.function, item.depth);
            pop();
            if (item.function->isDeclaration()) {
                graph_ = nullptr;
                return false;
            }
        }
    }
    std::vector<llvm::Function *> drop;
    for (llvm::Function &item : module_) {
        if (!item.isDeclaration() || &item == body) {
            continue;
        }
        if (!item.use_empty()) {
            graph_ = nullptr;
            return false;
        }
        drop.push_back(&item);
    }
    for (llvm::Function *item : drop) {
        item->eraseFromParent();
    }
    if (body->isDeclaration()) {
        graph_ = nullptr;
        return false;
    }
    // The returned slice may address a constant in this module. Copy it before that module is discarded.
    auto *pool_ty = llvm::ArrayType::get(i8(), kPoolBytes);
    auto *pool = new llvm::GlobalVariable(module_, pool_ty, false, llvm::GlobalValue::ExternalLinkage,
                                          llvm::ConstantAggregateZero::get(pool_ty), kPoolName);
    auto *used = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0),
                                          kUsedName);
    auto *fail = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0),
                                          kFailName);
    auto *wrapper_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {}, false);
    auto *wrapper = llvm::Function::Create(wrapper_ty, llvm::GlobalValue::ExternalLinkage, kEvalName, module_);
    wrapper->setCallingConv(llvm::CallingConv::C);
    auto *entry = llvm::BasicBlock::Create(context_, "entry", wrapper);
    builder_.SetInsertPoint(entry);
    llvm::Value *slice = builder_.CreateCall(body, {});
    llvm::Value *data = builder_.CreateExtractValue(slice, 0);
    llvm::Value *len = builder_.CreateExtractValue(slice, 1);
    auto *fail_bb = llvm::BasicBlock::Create(context_, "fail", wrapper);
    auto *null_bb = llvm::BasicBlock::Create(context_, "null", wrapper);
    auto *room_bb = llvm::BasicBlock::Create(context_, "room", wrapper);
    auto *copy_bb = llvm::BasicBlock::Create(context_, "copy", wrapper);
    auto *done_bb = llvm::BasicBlock::Create(context_, "done", wrapper);
    builder_.CreateCondBr(builder_.CreateICmpSLT(len, const_i32(0)), fail_bb, null_bb);
    builder_.SetInsertPoint(null_bb);
    llvm::Value *null_len = builder_.CreateAnd(builder_.CreateICmpEQ(data, null_ptr()),
                                               builder_.CreateICmpNE(len, const_i32(0)));
    builder_.CreateCondBr(null_len, fail_bb, room_bb);
    builder_.SetInsertPoint(room_bb);
    builder_.CreateCondBr(builder_.CreateICmpUGT(len, const_i32(static_cast<int>(kPoolBytes))), fail_bb, copy_bb);
    builder_.SetInsertPoint(copy_bb);
    builder_.CreateMemCpy(pool, llvm::Align(1), data, llvm::Align(1), builder_.CreateZExt(len, i64()));
    builder_.CreateStore(len, used);
    builder_.CreateBr(done_bb);
    builder_.SetInsertPoint(fail_bb);
    builder_.CreateStore(const_i32(1), fail);
    builder_.CreateBr(done_bb);
    builder_.SetInsertPoint(done_bb);
    builder_.CreateRetVoid();
    graph_ = nullptr;
    return diags_.size() == prepared && !wrapper->isDeclaration();
}

SourceLoc read_comptime_fault(llvm::orc::LLJIT &jit) {
    SourceLoc loc;
    auto file_or = jit.lookup("tlc.comptime.fail_file");
    auto off_or = jit.lookup("tlc.comptime.fail_off");
    if (!file_or || !off_or) {
        if (!file_or) {
            llvm::consumeError(file_or.takeError());
        }
        if (!off_or) {
            llvm::consumeError(off_or.takeError());
        }
        return loc;
    }
    const int32_t file = *file_or->toPtr<int32_t *>();
    const int32_t off = *off_or->toPtr<int32_t *>();
    if (file <= 0) {
        return loc;
    }
    loc.file = static_cast<uint32_t>(file);
    loc.offset = static_cast<uint32_t>(off);
    return loc;
}

std::optional<std::string> eval_comptime_string(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        if (!gen.define_comptime_string(graph, fn, file, specs)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<void (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    auto fail_addr = jit->lookup(kFailName);
    auto used_addr = jit->lookup(kUsedName);
    auto pool_addr = jit->lookup(kPoolName);
    if (!fail_addr || !used_addr || !pool_addr) {
        if (!fail_addr) {
            llvm::consumeError(fail_addr.takeError());
        }
        if (!used_addr) {
            llvm::consumeError(used_addr.takeError());
        }
        if (!pool_addr) {
            llvm::consumeError(pool_addr.takeError());
        }
        return std::nullopt;
    }
    call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    if (*fail_addr->toPtr<int32_t *>() != 0) {
        return std::nullopt;
    }
    const auto len = *used_addr->toPtr<int32_t *>();
    if (len < 0 || static_cast<uint32_t>(len) > kPoolBytes) {
        return std::nullopt;
    }
    const auto *pool = pool_addr->toPtr<const uint8_t *>();
    return std::string(reinterpret_cast<const char *>(pool), static_cast<std::size_t>(len));
}

std::optional<int32_t> eval_comptime_i32(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs,
    const std::vector<std::pair<uint32_t, const StructDecl *>> &structs,
    const std::vector<std::pair<uint32_t, const TypedefDecl *>> &aliases) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        gen.set_eval_structs(&structs);
        gen.set_eval_aliases(&aliases);
        if (!gen.define_comptime(graph, fn, file, specs)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<int32_t (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    const int32_t value = call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<int64_t> eval_comptime_i64(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs,
    const std::vector<std::pair<uint32_t, const StructDecl *>> &structs,
    const std::vector<std::pair<uint32_t, const TypedefDecl *>> &aliases) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        gen.set_eval_structs(&structs);
        gen.set_eval_aliases(&aliases);
        if (!gen.define_comptime(graph, fn, file, specs, 0, 64)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<int64_t (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    const int64_t value = call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<uint32_t> eval_comptime_u32(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs,
    const std::vector<std::pair<uint32_t, const StructDecl *>> &structs,
    const std::vector<std::pair<uint32_t, const TypedefDecl *>> &aliases) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        gen.set_eval_structs(&structs);
        gen.set_eval_aliases(&aliases);
        // `u32` is the same `i32` return the `i32` evaluator already emits. The bits stay unsigned.
        if (!gen.define_comptime(graph, fn, file, specs)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<uint32_t (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    const uint32_t value = call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<float> eval_comptime_f32(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs,
    const std::vector<std::pair<uint32_t, const StructDecl *>> &structs,
    const std::vector<std::pair<uint32_t, const TypedefDecl *>> &aliases) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        gen.set_eval_structs(&structs);
        gen.set_eval_aliases(&aliases);
        if (!gen.define_comptime(graph, fn, file, specs, 32)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<float (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    const float value = call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    return value;
}

std::optional<double> eval_comptime_f64(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs,
    const std::vector<std::pair<uint32_t, const StructDecl *>> &structs,
    const std::vector<std::pair<uint32_t, const TypedefDecl *>> &aliases) {
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return std::nullopt;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return std::nullopt;
    }

    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    {
        CodeGen gen(*context, *module, diags);
        gen.set_eval_structs(&structs);
        gen.set_eval_aliases(&aliases);
        if (!gen.define_comptime(graph, fn, file, specs, 64)) {
            return std::nullopt;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return std::nullopt;
    }

    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return std::nullopt;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return std::nullopt;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return std::nullopt;
    }
    auto *call = addr->toPtr<double (*)()>();
    if (call == nullptr) {
        return std::nullopt;
    }
    const double value = call();
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return std::nullopt;
        }
    }
    return value;
}

namespace {

uint64_t read_bits(const uint8_t *bytes, uint64_t size, bool little) {
    uint64_t value = 0;
    const uint64_t n = size < 8 ? size : 8;
    if (little) {
        for (uint64_t i = 0; i < n; ++i) {
            value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
        }
        return value;
    }
    for (uint64_t i = 0; i < n; ++i) {
        value = (value << 8) | bytes[i];
    }
    return value;
}

int64_t sign_extend(uint64_t value, int bits) {
    if (bits <= 0 || bits >= 64) {
        return static_cast<int64_t>(value);
    }
    const uint64_t sign = 1ULL << (bits - 1);
    const uint64_t mask = (1ULL << bits) - 1;
    value &= mask;
    if ((value & sign) != 0) {
        value |= ~mask;
    }
    return static_cast<int64_t>(value);
}

std::unique_ptr<Expr> make_int_expr(int64_t value) {
    if (value < 0) {
        auto expr = std::make_unique<Expr>();
        Expr::Unary node;
        node.op = TokenKind::Minus;
        node.expr = make_int_expr(-value);
        expr->data = std::move(node);
        return expr;
    }
    auto expr = std::make_unique<Expr>();
    Expr::Int node;
    node.value = static_cast<uint64_t>(value);
    expr->data = std::move(node);
    return expr;
}

std::unique_ptr<Expr> make_float_expr(const uint8_t *bytes, uint64_t size, bool is_f32) {
    char text[64];
    if (is_f32) {
        float value = 0;
        if (size < sizeof(value)) {
            return nullptr;
        }
        std::memcpy(&value, bytes, sizeof(value));
        std::snprintf(text, sizeof(text), "%a", static_cast<double>(value));
    } else {
        double value = 0;
        if (size < sizeof(value)) {
            return nullptr;
        }
        std::memcpy(&value, bytes, sizeof(value));
        std::snprintf(text, sizeof(text), "%a", value);
    }
    auto expr = std::make_unique<Expr>();
    Expr::Float node;
    node.text = text;
    node.is_f32 = is_f32;
    expr->data = std::move(node);
    return expr;
}

struct Decoded {
    ComptimeStructValue::Status status = ComptimeStructValue::Status::Unsupported;
    std::unique_ptr<Expr> expr;
};

Decoded decode_slot(const ComptimeSlot &slot, const uint8_t *base, uint64_t base_size, bool little,
                    const uint8_t *pool, uint64_t pool_size, const std::unordered_map<uint64_t, std::string> &functions);

Decoded decode_slot(const ComptimeSlot &slot, const uint8_t *base, uint64_t base_size, bool little,
                    const uint8_t *pool, uint64_t pool_size, const std::unordered_map<uint64_t, std::string> &functions) {
    Decoded out;
    if (base == nullptr || slot.offset > base_size || slot.size > base_size - slot.offset) {
        return out;
    }
    const uint8_t *bytes = base + slot.offset;
    const auto fail_constant = []() {
        Decoded bad;
        bad.status = ComptimeStructValue::Status::NotConstant;
        return bad;
    };
    const auto unsupported = []() { return Decoded{}; };
    switch (slot.kind) {
    case ComptimeSlot::Kind::Reject:
        return fail_constant();
    case ComptimeSlot::Kind::Int: {
        const uint64_t raw = read_bits(bytes, slot.size, little);
        // An `i64`, `u64`, or `u32` element keeps its suffix. `3000000000` does not fit in `i32`.
        if (slot.bits == 64 && !slot.is_unsigned) {
            auto expr = std::make_unique<Expr>();
            Expr::Int node;
            node.value = raw;
            node.suffix.bits = 64;
            expr->data = std::move(node);
            out.status = ComptimeStructValue::Status::Value;
            out.expr = std::move(expr);
            return out;
        }
        if (slot.bits == 64 && slot.is_unsigned) {
            auto expr = std::make_unique<Expr>();
            Expr::Int node;
            node.value = raw;
            node.suffix.bits = 64;
            node.suffix.is_unsigned = true;
            expr->data = std::move(node);
            out.status = ComptimeStructValue::Status::Value;
            out.expr = std::move(expr);
            return out;
        }
        if (slot.bits == 32 && slot.is_unsigned) {
            auto expr = std::make_unique<Expr>();
            Expr::Int node;
            node.value = raw & 0xffffffffULL;
            node.suffix.bits = 32;
            node.suffix.is_unsigned = true;
            expr->data = std::move(node);
            out.status = ComptimeStructValue::Status::Value;
            out.expr = std::move(expr);
            return out;
        }
        int64_t value = 0;
        if (slot.is_unsigned) {
            if (slot.bits > 0 && slot.bits < 64) {
                value = static_cast<int64_t>(raw & ((1ULL << slot.bits) - 1));
            } else {
                value = static_cast<int64_t>(raw);
            }
            if (value < 0 || value > 2147483647) {
                return unsupported();
            }
        } else {
            value = sign_extend(raw, slot.bits);
            if (value < -2147483647 || value > 2147483647) {
                return unsupported();
            }
        }
        out.status = ComptimeStructValue::Status::Value;
        out.expr = make_int_expr(value);
        return out;
    }
    case ComptimeSlot::Kind::Bool: {
        auto expr = std::make_unique<Expr>();
        Expr::Bool node;
        node.value = (read_bits(bytes, slot.size == 0 ? 1 : slot.size, little) & 1ULL) != 0;
        expr->data = std::move(node);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::F32:
    case ComptimeSlot::Kind::F64: {
        std::unique_ptr<Expr> expr = make_float_expr(bytes, slot.size, slot.kind == ComptimeSlot::Kind::F32);
        if (expr == nullptr) {
            return unsupported();
        }
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::Enum: {
        const uint64_t raw = read_bits(bytes, slot.size, little);
        const int64_t value = slot.is_unsigned ? static_cast<int64_t>(raw) : sign_extend(raw, slot.bits);
        const std::string *member = nullptr;
        for (const auto &item : slot.enumerators) {
            if (item.second == value) {
                member = &item.first;
                break;
            }
        }
        if (member == nullptr || slot.type_name.empty()) {
            return fail_constant();
        }
        auto expr = std::make_unique<Expr>();
        Expr::Selector node;
        auto type_name = std::make_unique<Expr>();
        Expr::Name name;
        name.text = slot.type_name;
        type_name->data = std::move(name);
        node.base = std::move(type_name);
        node.field = *member;
        expr->data = std::move(node);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::Func:
    case ComptimeSlot::Kind::Pointer: {
        const uint64_t raw = read_bits(bytes, sizeof(void *) < slot.size ? sizeof(void *) : slot.size, little);
        if (raw == 0) {
            auto expr = std::make_unique<Expr>();
            expr->data = Expr::Nullptr{};
            out.status = ComptimeStructValue::Status::Value;
            out.expr = std::move(expr);
            return out;
        }
        if (slot.kind == ComptimeSlot::Kind::Pointer) {
            return fail_constant();
        }
        const auto found = functions.find(raw);
        if (found == functions.end() || found->second.empty()) {
            return fail_constant();
        }
        auto expr = std::make_unique<Expr>();
        Expr::Name name;
        name.text = found->second;
        expr->data = std::move(name);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::SliceU8: {
        const uint64_t ptr_size = sizeof(void *);
        if (slot.len_offset > slot.size || slot.size - slot.len_offset < 4 || ptr_size > slot.size) {
            return fail_constant();
        }
        const uint64_t raw = read_bits(bytes, ptr_size, little);
        const uint64_t len_raw = read_bits(bytes + slot.len_offset, 4, little);
        const auto len = static_cast<int32_t>(len_raw);
        if (len < 0) {
            return fail_constant();
        }
        const uint64_t elem_size = slot.stride == 0 ? 1 : slot.stride;
        const uint64_t nbytes = static_cast<uint64_t>(len) * elem_size;
        const uint8_t *data = nullptr;
        if (len == 0) {
            data = pool;
        } else if (raw == 0 || pool == nullptr) {
            return fail_constant();
        } else {
            const auto addr = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pool));
            if (raw < addr || raw - addr > pool_size || nbytes > pool_size - (raw - addr)) {
                return fail_constant();
            }
            data = pool + (raw - addr);
        }
        if (slot.type_name.empty()) {
            std::string text;
            if (len != 0 && data != nullptr) {
                text.assign(reinterpret_cast<const char *>(data), static_cast<std::size_t>(len));
            }
            auto expr = std::make_unique<Expr>();
            Expr::String node;
            node.text = std::move(text);
            expr->data = std::move(node);
            out.status = ComptimeStructValue::Status::Value;
            out.expr = std::move(expr);
            return out;
        }
        auto expr = std::make_unique<Expr>();
        Expr::Init init;
        init.array_literal = true;
        init.type_name = slot.type_name;
        init.elements.reserve(static_cast<std::size_t>(len));
        for (int32_t index = 0; index < len; ++index) {
            ComptimeSlot one;
            if (slot.type_name == "f64") {
                one.kind = ComptimeSlot::Kind::F64;
            } else if (slot.type_name == "f32") {
                one.kind = ComptimeSlot::Kind::F32;
            } else if (slot.type_name == "bool") {
                one.kind = ComptimeSlot::Kind::Bool;
            } else {
                one.kind = ComptimeSlot::Kind::Int;
                one.bits = slot.bits;
                one.is_unsigned = slot.is_unsigned;
            }
            one.size = elem_size;
            const uint8_t *at = data + static_cast<uint64_t>(index) * elem_size;
            Decoded part = decode_slot(one, at, elem_size, little, pool, pool_size, functions);
            if (part.status != ComptimeStructValue::Status::Value || part.expr == nullptr) {
                return part;
            }
            init.elements.push_back(std::move(part.expr));
        }
        expr->data = std::move(init);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::Array: {
        if (slot.children.empty() || slot.stride == 0) {
            if (slot.count == 0) {
                auto expr = std::make_unique<Expr>();
                Expr::Init init;
                init.array_literal = true;
                expr->data = std::move(init);
                out.status = ComptimeStructValue::Status::Value;
                out.expr = std::move(expr);
                return out;
            }
            return unsupported();
        }
        auto expr = std::make_unique<Expr>();
        Expr::Init init;
        init.array_literal = true;
        const auto shape_name = [](const ComptimeSlot &inner) -> const char * {
            if (inner.kind == ComptimeSlot::Kind::Int) {
                return int_elem_name(inner.bits, inner.is_unsigned);
            }
            if (inner.kind == ComptimeSlot::Kind::F32) {
                return "f32";
            }
            if (inner.kind == ComptimeSlot::Kind::F64) {
                return "f64";
            }
            if (inner.kind == ComptimeSlot::Kind::Bool) {
                return "bool";
            }
            return nullptr;
        };
        const ComptimeSlot *cursor = &slot.children.front();
        std::vector<int> inner_lens;
        while (cursor->kind == ComptimeSlot::Kind::Array && cursor->children.size() == 1 && inner_lens.size() < 4) {
            inner_lens.push_back(static_cast<int>(cursor->count));
            cursor = &cursor->children.front();
        }
        const char *name = shape_name(*cursor);
        const ElemShape *shape = name == nullptr ? nullptr : elem_shape(name);
        const int shells = static_cast<int>(inner_lens.size()) + (name == nullptr ? 0 : 1);
        if (shape != nullptr && shells >= 1 && shells <= shape->max_shells) {
            init.type_name = name;
            if (!inner_lens.empty()) {
                init.elem_is_array = true;
                init.elem_array_len = inner_lens.front();
                if (inner_lens.size() > 1) {
                    init.elem_shells.reserve(inner_lens.size());
                    for (const int len : inner_lens) {
                        TypeShell shell;
                        shell.kind = TypeShell::Kind::Array;
                        shell.length = len;
                        init.elem_shells.push_back(shell);
                    }
                }
            }
        }
        init.elements.reserve(static_cast<std::size_t>(slot.count));
        for (uint64_t index = 0; index < slot.count; ++index) {
            const uint64_t at = index * slot.stride;
            if (at > slot.size || slot.children.front().size > slot.size - at) {
                return unsupported();
            }
            ComptimeSlot element = slot.children.front();
            element.offset = 0;
            Decoded one = decode_slot(element, bytes + at, slot.size - at, little, pool, pool_size, functions);
            if (one.status != ComptimeStructValue::Status::Value || one.expr == nullptr) {
                return one;
            }
            init.elements.push_back(std::move(one.expr));
        }
        expr->data = std::move(init);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::Tuple: {
        auto expr = std::make_unique<Expr>();
        Expr::Init init;
        init.is_tuple = true;
        init.elements.reserve(slot.children.size());
        for (const ComptimeSlot &child : slot.children) {
            Decoded one = decode_slot(child, bytes, slot.size, little, pool, pool_size, functions);
            if (one.status != ComptimeStructValue::Status::Value || one.expr == nullptr) {
                return one;
            }
            init.elements.push_back(std::move(one.expr));
        }
        expr->data = std::move(init);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    case ComptimeSlot::Kind::Struct: {
        auto expr = std::make_unique<Expr>();
        Expr::Init init;
        init.is_struct = true;
        init.type_name = slot.type_name;
        for (const ComptimeSlot &child : slot.children) {
            Decoded one = decode_slot(child, bytes, slot.size, little, pool, pool_size, functions);
            if (one.status != ComptimeStructValue::Status::Value || one.expr == nullptr) {
                return one;
            }
            Expr::FieldInit field;
            field.field = child.name;
            field.value = std::move(one.expr);
            init.fields.push_back(std::move(field));
        }
        expr->data = std::move(init);
        out.status = ComptimeStructValue::Status::Value;
        out.expr = std::move(expr);
        return out;
    }
    }
    return unsupported();
}

bool slot_rejected(const ComptimeSlot &slot) {
    if (slot.kind == ComptimeSlot::Kind::Reject) {
        return true;
    }
    for (const ComptimeSlot &child : slot.children) {
        if (slot_rejected(child)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool CodeGen::define_comptime_struct(const PackageGraph &graph, const FunctionDecl &fn, uint32_t file,
                                     ComptimeStructImage &image,
                                     const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs) {
    image = ComptimeStructImage{};
    if (graph.nodes.empty() || fn.body == nullptr) {
        return false;
    }
    eval_ = true;
    install_eval_fault();
    graph_ = &graph;
    entry_file_ = graph.nodes.front().file_id;
    prepare_enums();
    prepare_structs();
    prepare_traits();
    for (const PackageNode &node : graph.nodes) {
        declare_functions(node);
    }
    const std::vector<PackageNode> held_specs = take_comptime_specs(specs);
    for (const PackageNode &node : held_specs) {
        declare_functions(node);
    }
    for (const PackageNode &node : graph.nodes) {
        declare_globals(node);
    }
    for (const PackageNode &node : graph.nodes) {
        current_file_ = node.file_id;
        for (const TopDecl &decl : node.unit.decls) {
            if (const auto *var = std::get_if<VarDecl>(&decl.data)) {
                apply_initializer(*var);
            }
        }
    }
    const std::size_t prepared = diags_.size();
    const PackageNode *home = find_node(file);
    if (home == nullptr) {
        graph_ = nullptr;
        return false;
    }
    current_file_ = file;
    packages_.clear();
    push();
    bind_package_globals();
    bind_imports(*home);
    llvm::FunctionType *ftype = function_type(fn, file);
    const auto give_up = [&]() {
        pop();
        graph_ = nullptr;
        return false;
    };
    if (ftype == nullptr || ftype->getNumParams() != 0 || ftype->getReturnType() == nullptr ||
        !(ftype->getReturnType()->isStructTy() || ftype->getReturnType()->isArrayTy())) {
        return give_up();
    }
    // `(i32, i32)` is several results. `((i32, i32))` is one tuple. Both return one aggregate.
    const bool many_results = fn.results.size() > 1;
    const bool one_tuple = !many_results && fn.results.size() == 1 && fn.results.front().is_tuple &&
                           fn.results.front().indirections == 0 && !fn.results.front().is_array;
    int array_shells = 0;
    if (!many_results && fn.results.size() == 1) {
        for (const TypeShell &shell : fn.results.front().shells) {
            if (shell.kind == TypeShell::Kind::Array) {
                ++array_shells;
            }
        }
    }
    // One shell from `elem_shape`, up to `max_shells`. A slice is copied into the pool.
    // A pointer and a fourth shell stay out.
    const ElemShape *shape = fn.results.size() == 1 ? elem_shape(fn.results.front().type_name) : nullptr;
    const bool one_array = !many_results && shape != nullptr && fn.results.front().is_array &&
                           fn.results.front().array_len > 0 && fn.results.front().indirections == 0 &&
                           !fn.results.front().is_tuple && !fn.results.front().is_fn_ptr &&
                           fn.results.front().type_package.empty() && array_shells >= 1 &&
                           array_shells <= shape->max_shells;
    const bool one_slice = !many_results && shape != nullptr && fn.results.front().is_array &&
                           fn.results.front().array_len < 0 && fn.results.front().indirections == 0 &&
                           !fn.results.front().is_tuple && !fn.results.front().is_fn_ptr &&
                           fn.results.front().type_package.empty();
    const StructLayout *layout = nullptr;
    if (!many_results && !one_tuple && !one_array && !one_slice) {
        const Lowered ret = lower(file, fn.return_type, fn.type_package, fn.is_struct, fn.indirections, false, 0, false);
        if (ret.struct_id < 0 || ret.struct_id >= static_cast<int>(structs_.size()) || ret.depth != 0 || ret.is_array ||
            ret.is_slice) {
            return give_up();
        }
        layout = &structs_[static_cast<std::size_t>(ret.struct_id)];
        if (layout->is_union || layout->type == nullptr || !layout->type->isSized()) {
            image.not_constant = true;
            return give_up();
        }
    }
    const llvm::DataLayout &data_layout = module_.getDataLayout();
    llvm::Type *measured = layout != nullptr ? static_cast<llvm::Type *>(layout->type) : ftype->getReturnType();
    image.little = data_layout.isLittleEndian();
    image.size = data_layout.getTypeAllocSize(measured).getFixedValue();
    image.align = data_layout.getABITypeAlign(measured).value();
    if (image.align == 0) {
        image.align = 1;
    }
    const auto schema_field = [&](const auto &self, const FieldLayout &field) -> ComptimeSlot {
        ComptimeSlot slot;
        slot.name = field.name;
        llvm::Type *type = field.type;
        if (type == nullptr || !type->isSized()) {
            return slot;
        }
        const auto sized = [&](llvm::Type *one) {
            const llvm::TypeSize store = data_layout.getTypeStoreSize(one);
            return store.isScalable() ? 0 : store.getFixedValue();
        };
        if (auto *array = llvm::dyn_cast<llvm::ArrayType>(type)) {
            slot.kind = ComptimeSlot::Kind::Array;
            slot.count = array->getNumElements();
            slot.size = data_layout.getTypeAllocSize(array).getFixedValue();
            llvm::Type *element = array->getElementType();
            slot.stride = element != nullptr && element->isSized() ? data_layout.getTypeAllocSize(element).getFixedValue()
                                                                    : 0;
            FieldLayout inner = field;
            inner.name.clear();
            inner.type = element;
            inner.is_array = false;
            inner.array_len = 0;
            inner.is_slice = field.elem_slice || field.is_slice;
            inner.elem_slice = false;
            slot.children.push_back(self(self, inner));
            return slot;
        }
        slot.size = sized(type);
        if (field.is_tuple && field.depth == 0) {
            auto *packed = llvm::dyn_cast<llvm::StructType>(type);
            if (packed == nullptr || field.tuple_parts.empty() || !packed->isSized()) {
                return slot;
            }
            slot.kind = ComptimeSlot::Kind::Tuple;
            slot.size = data_layout.getTypeAllocSize(packed).getFixedValue();
            const llvm::StructLayout *placed = data_layout.getStructLayout(packed);
            for (std::size_t index = 0; index < field.tuple_parts.size() && index < packed->getNumElements(); ++index) {
                const Lowered part = lower_result(current_file_, field.tuple_parts[index]);
                FieldLayout child_field;
                child_field.type = part.type;
                child_field.depth = part.depth;
                child_field.elem_char = part.elem_char;
                child_field.is_array = part.is_array;
                child_field.array_len = part.array_len;
                child_field.struct_id = part.struct_id;
                child_field.elem_void = part.elem_void;
                child_field.elem_bool = part.elem_bool;
                child_field.elem_bits = part.elem_bits;
                child_field.elem_unsigned = part.elem_unsigned;
                child_field.elem_float = part.elem_float;
                child_field.enum_id = part.enum_id;
                child_field.is_slice = part.is_slice;
                child_field.elem_slice = part.elem_slice;
                child_field.shells = part.shells;
                child_field.is_tuple = part.is_tuple;
                child_field.tuple_parts = part.tuple_parts;
                ComptimeSlot child = self(self, child_field);
                child.offset = placed->getElementOffset(static_cast<unsigned>(index));
                slot.children.push_back(std::move(child));
            }
            return slot;
        }
        if (field.is_tuple) {
            return slot;
        }
        const bool slice = field.is_slice || field.elem_slice;
        if (slice) {
            const bool bytes = !field.elem_bool && !field.elem_float && field.enum_id < 0 && field.struct_id < 0 &&
                               field.depth == 0 && (field.elem_bits == 8 || field.elem_char);
            if (!bytes) {
                return slot;
            }
            auto *packed = llvm::dyn_cast<llvm::StructType>(type);
            if (packed == nullptr || packed->getNumElements() < 2) {
                return slot;
            }
            slot.kind = ComptimeSlot::Kind::SliceU8;
            slot.len_offset = data_layout.getStructLayout(packed)->getElementOffset(1);
            return slot;
        }
        if (field.struct_id >= 0 && field.depth == 0) {
            if (field.struct_id >= static_cast<int>(structs_.size()) ||
                structs_[static_cast<std::size_t>(field.struct_id)].is_union) {
                return slot;
            }
            const StructLayout &nested = structs_[static_cast<std::size_t>(field.struct_id)];
            slot.kind = ComptimeSlot::Kind::Struct;
            slot.type_name = nested.name;
            if (nested.type == nullptr || !nested.type->isSized()) {
                slot.kind = ComptimeSlot::Kind::Reject;
                return slot;
            }
            const llvm::StructLayout *placed = data_layout.getStructLayout(nested.type);
            slot.size = data_layout.getTypeAllocSize(nested.type).getFixedValue();
            for (std::size_t index = 0; index < nested.fields.size() && index < nested.type->getNumElements(); ++index) {
                ComptimeSlot child = self(self, nested.fields[index]);
                child.offset = placed->getElementOffset(static_cast<unsigned>(index));
                slot.children.push_back(std::move(child));
            }
            return slot;
        }
        if (field.enum_id >= 0 && field.depth == 0) {
            if (field.enum_id >= static_cast<int>(enums_.size())) {
                return slot;
            }
            const EnumLayout &enumeration = enums_[static_cast<std::size_t>(field.enum_id)];
            slot.kind = ComptimeSlot::Kind::Enum;
            slot.type_name = enumeration.name;
            slot.bits = enumeration.bits;
            slot.is_unsigned = enumeration.is_unsigned || enumeration.is_char;
            if (graph_ != nullptr) {
                for (const PackageNode &node : graph_->nodes) {
                    for (const TopDecl &decl : node.unit.decls) {
                        const auto *item = std::get_if<EnumDecl>(&decl.data);
                        if (item == nullptr || enum_named(node.file_id, item->name) != field.enum_id) {
                            continue;
                        }
                        for (const Enumerator &enumerator : item->enumerators) {
                            const auto member = enumeration.values.find(enumerator.name);
                            if (member != enumeration.values.end()) {
                                slot.enumerators.emplace_back(enumerator.name, member->second);
                            }
                        }
                        return slot;
                    }
                }
            }
            return slot;
        }
        if (field.elem_bool && field.depth == 0) {
            slot.kind = ComptimeSlot::Kind::Bool;
            return slot;
        }
        if (field.elem_float && field.depth == 0) {
            slot.kind = field.elem_bits >= 64 ? ComptimeSlot::Kind::F64 : ComptimeSlot::Kind::F32;
            return slot;
        }
        if (field.depth > 0 && field.fn_ty != nullptr) {
            slot.kind = ComptimeSlot::Kind::Func;
            slot.size = sized(ptr_type());
            return slot;
        }
        if (field.depth > 0) {
            slot.kind = ComptimeSlot::Kind::Pointer;
            slot.size = sized(ptr_type());
            return slot;
        }
        if (type->isIntegerTy()) {
            slot.kind = ComptimeSlot::Kind::Int;
            slot.bits = static_cast<int>(type->getIntegerBitWidth());
            slot.is_unsigned = field.elem_unsigned || field.elem_char;
            return slot;
        }
        return slot;
    };
    image.root.size = image.size;
    if (layout != nullptr) {
        image.root.kind = ComptimeSlot::Kind::Struct;
        image.root.type_name = layout->name;
        const llvm::StructLayout *placed = data_layout.getStructLayout(layout->type);
        for (std::size_t index = 0; index < layout->fields.size() && index < layout->type->getNumElements(); ++index) {
            ComptimeSlot child = schema_field(schema_field, layout->fields[index]);
            child.offset = placed->getElementOffset(static_cast<unsigned>(index));
            image.root.children.push_back(std::move(child));
        }
    } else if (one_array) {
        auto *array = llvm::dyn_cast<llvm::ArrayType>(measured);
        if (array == nullptr || !array->isSized()) {
            return give_up();
        }
        FieldLayout field;
        field.type = array;
        field.is_array = true;
        field.array_len = static_cast<int>(array->getNumElements());
        const ElemShape *elem = elem_shape(fn.results.front().type_name);
        if (elem != nullptr && elem->is_float) {
            field.elem_float = true;
            field.elem_bits = elem->bits;
        } else if (elem != nullptr && elem->is_bool) {
            field.elem_bool = true;
        } else if (elem != nullptr && elem->bits != 32) {
            // `i32` is the unmarked integer. A suffix keeps its width, and `u32` its sign.
            field.elem_bits = elem->bits;
            field.elem_unsigned = elem->is_unsigned;
        } else if (elem != nullptr && elem->is_unsigned) {
            field.elem_unsigned = true;
            field.elem_bits = 32;
        }
        image.root = schema_field(schema_field, field);
        if (image.root.kind != ComptimeSlot::Kind::Array) {
            return give_up();
        }
    } else if (one_slice) {
        auto *packed = llvm::dyn_cast<llvm::StructType>(measured);
        if (packed == nullptr || !packed->isSized() || packed->getNumElements() < 2 || shape == nullptr) {
            return give_up();
        }
        const llvm::StructLayout *placed = data_layout.getStructLayout(packed);
        image.root.kind = ComptimeSlot::Kind::SliceU8;
        image.root.type_name = shape->name;
        image.root.len_offset = placed->getElementOffset(1);
        image.root.size = data_layout.getTypeAllocSize(packed).getFixedValue();
        image.root.bits = shape->bits;
        image.root.is_unsigned = shape->is_unsigned;
        image.root.stride = shape->is_bool ? 1 : (shape->is_float ? (shape->bits >= 64 ? 8 : 4) : shape->bits / 8);
        auto *pool_ty = llvm::ArrayType::get(i8(), kPoolBytes);
        if (module_.getNamedGlobal(kPoolName) == nullptr) {
            new llvm::GlobalVariable(module_, pool_ty, false, llvm::GlobalValue::ExternalLinkage,
                                     llvm::ConstantAggregateZero::get(pool_ty), kPoolName);
        }
        if (module_.getNamedGlobal(kUsedName) == nullptr) {
            new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0), kUsedName);
        }
    } else {
        auto *packed = llvm::dyn_cast<llvm::StructType>(measured);
        if (packed == nullptr || !packed->isSized()) {
            return give_up();
        }
        image.root.kind = ComptimeSlot::Kind::Tuple;
        const llvm::StructLayout *placed = data_layout.getStructLayout(packed);
        std::vector<ResultSlot> parts = one_tuple ? fn.results.front().tuple_parts : fn.results;
        for (std::size_t index = 0; index < parts.size() && index < packed->getNumElements(); ++index) {
            const Lowered part = lower_result(file, parts[index]);
            FieldLayout child_field;
            child_field.type = part.type;
            child_field.depth = part.depth;
            child_field.elem_char = part.elem_char;
            child_field.is_array = part.is_array;
            child_field.array_len = part.array_len;
            child_field.struct_id = part.struct_id;
            child_field.elem_void = part.elem_void;
            child_field.elem_bool = part.elem_bool;
            child_field.elem_bits = part.elem_bits;
            child_field.elem_unsigned = part.elem_unsigned;
            child_field.elem_float = part.elem_float;
            child_field.enum_id = part.enum_id;
            child_field.is_slice = part.is_slice;
            child_field.elem_slice = part.elem_slice;
            child_field.shells = part.shells;
            child_field.is_tuple = part.is_tuple;
            child_field.tuple_parts = part.tuple_parts;
            ComptimeSlot child = schema_field(schema_field, child_field);
            child.offset = placed->getElementOffset(static_cast<unsigned>(index));
            image.root.children.push_back(std::move(child));
        }
    }
    if (slot_rejected(image.root)) {
        image.not_constant = true;
        return give_up();
    }
    if (module_.getNamedValue(kEvalName) != nullptr || module_.getNamedValue(kBodyName) != nullptr) {
        return give_up();
    }
    auto *body = llvm::Function::Create(ftype, llvm::GlobalValue::InternalLinkage, kBodyName, module_);
    body->setCallingConv(llvm::CallingConv::C);
    emit_function_body(fn, body, 0);
    for (int guard = 0; guard < 64; ++guard) {
        struct Pending {
            uint32_t file = 0;
            const FunctionDecl *decl = nullptr;
            llvm::Function *function = nullptr;
            int depth = 0;
        };
        std::vector<Pending> pending;
        for (auto &entry : files_) {
            for (auto &symbol : entry.second.functions) {
                auto *callee = llvm::dyn_cast_or_null<llvm::Function>(symbol.second.value);
                if (callee == nullptr || !callee->isDeclaration() || callee->use_empty()) {
                    continue;
                }
                const FunctionDecl *decl = symbol.second.decl;
                if (decl == nullptr || decl->body == nullptr || symbol.second.is_generic ||
                    function_is_generic(entry.first, *decl)) {
                    continue;
                }
                pending.push_back(Pending{entry.first, decl, callee, symbol.second.depth});
            }
        }
        if (pending.empty()) {
            break;
        }
        for (const Pending &item : pending) {
            const PackageNode *node = find_node(item.file);
            if (node == nullptr || !item.function->isDeclaration()) {
                continue;
            }
            current_file_ = item.file;
            packages_.clear();
            push();
            bind_package_globals();
            bind_imports(*node);
            emit_function_body(*item.decl, item.function, item.depth);
            pop();
            if (item.function->isDeclaration()) {
                return give_up();
            }
        }
    }
    std::vector<llvm::Function *> drop;
    for (llvm::Function &item : module_) {
        if (!item.isDeclaration() || &item == body || item.isIntrinsic()) {
            continue;
        }
        if (!item.use_empty()) {
            return give_up();
        }
        drop.push_back(&item);
    }
    for (llvm::Function *item : drop) {
        item->eraseFromParent();
    }
    for (const auto &entry : files_) {
        for (const auto &symbol : entry.second.functions) {
            auto *function = llvm::dyn_cast_or_null<llvm::Function>(symbol.second.value);
            if (function == nullptr || function->isDeclaration() || symbol.second.decl == nullptr ||
                symbol.second.decl->name.empty() || function->getName().empty()) {
                continue;
            }
            image.functions.emplace_back(function->getName().str(), symbol.second.decl->name);
        }
    }
    auto *pool_ty = llvm::ArrayType::get(i8(), kPoolBytes);
    auto *pool = module_.getNamedGlobal(kPoolName);
    if (pool == nullptr) {
        pool = new llvm::GlobalVariable(module_, pool_ty, false, llvm::GlobalValue::ExternalLinkage,
                                        llvm::ConstantAggregateZero::get(pool_ty), kPoolName);
    }
    auto *used = module_.getNamedGlobal(kUsedName);
    if (used == nullptr) {
        used = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0),
                                        kUsedName);
    }
    auto *fail = new llvm::GlobalVariable(module_, i32(), false, llvm::GlobalValue::ExternalLinkage, const_i32(0),
                                          kFailName);
    auto *wrapper_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(context_), {ptr_type()}, false);
    auto *wrapper = llvm::Function::Create(wrapper_ty, llvm::GlobalValue::ExternalLinkage, kEvalName, module_);
    wrapper->setCallingConv(llvm::CallingConv::C);
    auto *entry = llvm::BasicBlock::Create(context_, "entry", wrapper);
    builder_.SetInsertPoint(entry);
    llvm::Value *out = wrapper->getArg(0);
    llvm::Value *value = builder_.CreateCall(body, {});
    builder_.CreateStore(value, out);
    const auto relocate = [&](const auto &self, llvm::Value *base, const ComptimeSlot &slot) -> void {
        const auto at = [&](llvm::Value *ptr, uint64_t offset) {
            return builder_.CreateInBoundsGEP(i8(), ptr, llvm::ConstantInt::get(i64(), offset));
        };
        if (slot.kind == ComptimeSlot::Kind::Struct || slot.kind == ComptimeSlot::Kind::Tuple) {
            for (const ComptimeSlot &child : slot.children) {
                self(self, at(base, child.offset), child);
            }
            return;
        }
        if (slot.kind == ComptimeSlot::Kind::Array) {
            if (slot.children.empty()) {
                return;
            }
            for (uint64_t index = 0; index < slot.count; ++index) {
                self(self, at(base, index * slot.stride), slot.children.front());
            }
            return;
        }
        if (slot.kind != ComptimeSlot::Kind::SliceU8) {
            return;
        }
        llvm::Value *ptr_addr = at(base, 0);
        llvm::Value *len_addr = at(base, slot.len_offset);
        llvm::Value *data = builder_.CreateLoad(ptr_type(), ptr_addr);
        llvm::Value *len = builder_.CreateLoad(i32(), len_addr);
        auto *null_bb = llvm::BasicBlock::Create(context_, "slice.null", wrapper);
        auto *copy_bb = llvm::BasicBlock::Create(context_, "slice.copy", wrapper);
        auto *room_bb = llvm::BasicBlock::Create(context_, "slice.room", wrapper);
        auto *do_bb = llvm::BasicBlock::Create(context_, "slice.do", wrapper);
        auto *fail_bb = llvm::BasicBlock::Create(context_, "slice.fail", wrapper);
        auto *done_bb = llvm::BasicBlock::Create(context_, "slice.done", wrapper);
        builder_.CreateCondBr(builder_.CreateICmpEQ(data, null_ptr()), null_bb, copy_bb);
        builder_.SetInsertPoint(null_bb);
        builder_.CreateCondBr(builder_.CreateICmpNE(len, const_i32(0)), fail_bb, done_bb);
        builder_.SetInsertPoint(copy_bb);
        builder_.CreateCondBr(builder_.CreateICmpSLT(len, const_i32(0)), fail_bb, room_bb);
        builder_.SetInsertPoint(room_bb);
        llvm::Value *used_now = builder_.CreateLoad(i32(), used);
        llvm::Value *span = slot.stride > 1 ? builder_.CreateMul(len, const_i32(static_cast<int>(slot.stride))) : len;
        llvm::Value *next = builder_.CreateAdd(used_now, span);
        llvm::Value *overflow = builder_.CreateOr(builder_.CreateICmpULT(next, used_now),
                                                   builder_.CreateICmpUGT(next, const_i32(static_cast<int>(kPoolBytes))));
        builder_.CreateCondBr(overflow, fail_bb, do_bb);
        builder_.SetInsertPoint(do_bb);
        llvm::Value *dest = builder_.CreateInBoundsGEP(i8(), pool, builder_.CreateZExt(used_now, i64()));
        builder_.CreateMemCpy(dest, llvm::Align(1), data, llvm::Align(1), builder_.CreateZExt(span, i64()));
        builder_.CreateStore(dest, ptr_addr);
        builder_.CreateStore(next, used);
        builder_.CreateBr(done_bb);
        builder_.SetInsertPoint(fail_bb);
        builder_.CreateStore(const_i32(1), fail);
        builder_.CreateBr(done_bb);
        builder_.SetInsertPoint(done_bb);
    };
    relocate(relocate, out, image.root);
    builder_.CreateRetVoid();
    pop();
    graph_ = nullptr;
    return diags_.size() == prepared && !body->isDeclaration() && !wrapper->isDeclaration();
}

ComptimeStructValue eval_comptime_struct(
    const PackageGraph &graph, const FunctionDecl &fn, uint32_t file, SourceLoc *fault,
    const std::vector<std::pair<uint32_t, const FunctionDecl *>> &specs) {
    ComptimeStructValue out;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    auto machine_or = llvm::orc::JITTargetMachineBuilder::detectHost();
    if (!machine_or) {
        llvm::consumeError(machine_or.takeError());
        return out;
    }
    llvm::orc::JITTargetMachineBuilder machine = std::move(*machine_or);
    machine.setCPU("generic");
    machine.setRelocationModel(llvm::Reloc::PIC_);
    machine.getOptions().EmulatedTLS = false;
    auto layout_or = machine.getDefaultDataLayoutForTarget();
    if (!layout_or) {
        llvm::consumeError(layout_or.takeError());
        return out;
    }
    SourceSet sources;
    DiagnosticEngine diags(sources);
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc.comptime", *context);
    module->setTargetTriple(machine.getTargetTriple());
    module->setDataLayout(*layout_or);
    ComptimeStructImage image;
    {
        CodeGen gen(*context, *module, diags);
        if (!gen.define_comptime_struct(graph, fn, file, image, specs)) {
            if (image.not_constant) {
                out.status = ComptimeStructValue::Status::NotConstant;
            }
            return out;
        }
    }
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        return out;
    }
    llvm::orc::LLJITBuilder builder;
    builder.setJITTargetMachineBuilder(std::move(machine));
    auto jit_or = builder.create();
    if (!jit_or) {
        llvm::consumeError(jit_or.takeError());
        return out;
    }
    std::unique_ptr<llvm::orc::LLJIT> jit = std::move(*jit_or);
    if (auto err = jit->addIRModule(llvm::orc::ThreadSafeModule(std::move(module), std::move(context)))) {
        llvm::consumeError(std::move(err));
        return out;
    }
    auto addr = jit->lookup(kEvalName);
    if (!addr) {
        llvm::consumeError(addr.takeError());
        return out;
    }
    auto *call = addr->toPtr<void (*)(void *)>();
    if (call == nullptr) {
        return out;
    }
    auto fail_addr = jit->lookup(kFailName);
    auto pool_addr = jit->lookup(kPoolName);
    if (!fail_addr || !pool_addr) {
        if (!fail_addr) {
            llvm::consumeError(fail_addr.takeError());
        }
        if (!pool_addr) {
            llvm::consumeError(pool_addr.takeError());
        }
        return out;
    }
    std::unordered_map<uint64_t, std::string> functions;
    for (const auto &item : image.functions) {
        auto symbol = jit->lookup(item.first);
        if (!symbol) {
            llvm::consumeError(symbol.takeError());
            continue;
        }
        functions.emplace(symbol->getValue(), item.second);
    }
    const uint64_t align = image.align == 0 ? 1 : image.align;
    std::vector<uint8_t> raw(static_cast<std::size_t>(image.size + align), 0);
    const auto start = reinterpret_cast<uintptr_t>(raw.data());
    const auto aligned = (start + static_cast<uintptr_t>(align - 1)) & ~static_cast<uintptr_t>(align - 1);
    auto *buffer = reinterpret_cast<uint8_t *>(aligned);
    call(buffer);
    if (fault != nullptr) {
        *fault = read_comptime_fault(*jit);
        if (fault->valid()) {
            return out;
        }
    }
    if (*fail_addr->toPtr<int32_t *>() != 0) {
        out.status = ComptimeStructValue::Status::NotConstant;
        return out;
    }
    Decoded decoded = decode_slot(image.root, buffer, image.size, image.little, pool_addr->toPtr<uint8_t *>(), kPoolBytes,
                                  functions);
    out.status = decoded.status;
    out.value = std::move(decoded.expr);
    return out;
}

llvm::Value *CodeGen::retain_eval_slice(llvm::Value *packed, uint64_t elem_size) {
    if (!eval_ || packed == nullptr || elem_size == 0 || elem_size > 16 || packed->getType() != slice_type()) {
        return packed;
    }
    auto *pool = module_.getNamedGlobal(kPoolName);
    auto *used = module_.getNamedGlobal(kUsedName);
    llvm::BasicBlock *here = builder_.GetInsertBlock();
    if (pool == nullptr || used == nullptr || here == nullptr) {
        return packed;
    }
    auto *function = here->getParent();
    auto *copy_bb = llvm::BasicBlock::Create(context_, "slice.keep", function);
    auto *skip_bb = llvm::BasicBlock::Create(context_, "slice.skip", function);
    auto *done_bb = llvm::BasicBlock::Create(context_, "slice.done", function);
    llvm::Value *data = builder_.CreateExtractValue(packed, 0);
    llvm::Value *len = builder_.CreateExtractValue(packed, 1);
    llvm::Value *span = builder_.CreateMul(len, const_i32(static_cast<int>(elem_size)));
    llvm::Value *used_now = builder_.CreateLoad(i32(), used);
    llvm::Value *next = builder_.CreateAdd(used_now, span);
    llvm::Value *overflow = builder_.CreateOr(builder_.CreateICmpULT(next, used_now),
                                               builder_.CreateICmpUGT(next, const_i32(static_cast<int>(kPoolBytes))));
    llvm::Value *bad = builder_.CreateOr(overflow, builder_.CreateICmpSLT(len, const_i32(0)));
    builder_.CreateCondBr(bad, skip_bb, copy_bb);
    builder_.SetInsertPoint(copy_bb);
    llvm::Value *dest = builder_.CreateInBoundsGEP(i8(), pool, builder_.CreateZExt(used_now, i64()));
    builder_.CreateMemCpy(dest, llvm::Align(1), data, llvm::Align(1), builder_.CreateZExt(span, i64()));
    builder_.CreateStore(next, used);
    llvm::Value *kept = builder_.CreateInsertValue(packed, dest, 0);
    builder_.CreateBr(done_bb);
    builder_.SetInsertPoint(skip_bb);
    builder_.CreateBr(done_bb);
    builder_.SetInsertPoint(done_bb);
    auto *phi = builder_.CreatePHI(slice_type(), 2);
    phi->addIncoming(kept, copy_bb);
    phi->addIncoming(packed, skip_bb);
    return phi;
}

} // namespace tlc
