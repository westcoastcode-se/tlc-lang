// DWARF line numbers and variable locations for `-g`.
#include "gen.hpp"

#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/IRBuilder.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace tlc {

namespace {

unsigned line_of(const DiagnosticEngine &diags, SourceLoc loc) {
    if (!loc.valid()) {
        return 1;
    }
    const SourceFile *file = diags.source(loc.file);
    if (file == nullptr) {
        return 1;
    }
    return static_cast<unsigned>(std::max(file->line_col(loc.offset).line, 1));
}

unsigned column_of(const DiagnosticEngine &diags, SourceLoc loc) {
    if (!loc.valid()) {
        return 1;
    }
    const SourceFile *file = diags.source(loc.file);
    if (file == nullptr) {
        return 1;
    }
    return static_cast<unsigned>(std::max(file->line_col(loc.offset).column, 1));
}

} // namespace

void CodeGen::open_debug() {
    module_.addModuleFlag(llvm::Module::Warning, "Debug Info Version", llvm::DEBUG_METADATA_VERSION);
    module_.addModuleFlag(llvm::Module::Warning, "Dwarf Version", 5);
    di_builder_ = std::make_unique<llvm::DIBuilder>(module_);
    SourceLoc entry;
    entry.file = entry_file_;
    llvm::DIFile *file = debug_file(entry);
    di_unit_ = di_builder_->createCompileUnit(llvm::DISourceLanguageName(llvm::dwarf::DW_LANG_C), file, "tlc 0.1.0",
                                              false, "", 0);
}

void CodeGen::finish_debug() {
    if (di_builder_ == nullptr) {
        return;
    }
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
    di_builder_->finalize();
}

llvm::DIFile *CodeGen::debug_file(SourceLoc loc) {
    const uint32_t id = loc.valid() ? loc.file : entry_file_;
    if (const auto found = di_files_.find(id); found != di_files_.end()) {
        return found->second;
    }
    std::string filename = "tlc";
    std::string directory = ".";
    if (const SourceFile *src = diags_.source(id)) {
        const std::filesystem::path path(src->path());
        if (path.has_filename()) {
            filename = path.filename().string();
        }
        if (path.has_parent_path() && !path.parent_path().empty()) {
            directory = path.parent_path().string();
        }
    }
    llvm::DIFile *file = di_builder_->createFile(filename, directory);
    di_files_.emplace(id, file);
    return file;
}

void CodeGen::set_loc(SourceLoc loc) {
    if (!debug_ || di_scopes_.empty() || !loc.valid()) {
        return;
    }
    auto *scope = llvm::dyn_cast<llvm::DILocalScope>(di_scopes_.back());
    if (scope == nullptr) {
        return;
    }
    builder_.SetCurrentDebugLocation(
        llvm::DILocation::get(context_, line_of(diags_, loc), column_of(diags_, loc), scope));
}

void CodeGen::debug_enter(SourceLoc loc) {
    if (!debug_ || di_builder_ == nullptr || di_scopes_.empty()) {
        return;
    }
    di_scopes_.push_back(di_builder_->createLexicalBlock(di_scopes_.back(), debug_file(loc), line_of(diags_, loc),
                                                         column_of(diags_, loc)));
}

void CodeGen::debug_leave() {
    if (!debug_ || di_scopes_.size() <= 1) {
        return;
    }
    di_scopes_.pop_back();
}

void CodeGen::debug_begin_function(const FunctionDecl &fn, llvm::Function *function) {
    if (!debug_ || di_builder_ == nullptr || di_unit_ == nullptr || function == nullptr) {
        return;
    }
    const unsigned line = line_of(diags_, fn.loc);
    llvm::DIFile *file = debug_file(fn.loc);
    std::string name = fn.name.empty() ? std::string("anon") : fn.name;
    if (!fn.method_struct.empty()) {
        name = fn.method_struct + "." + name;
    }
    auto *type = di_builder_->createSubroutineType(di_builder_->getOrCreateTypeArray({}));
    di_subprogram_ = di_builder_->createFunction(file, name, function->getName(), file, line, type, line,
                                                 llvm::DINode::FlagZero, llvm::DISubprogram::SPFlagDefinition);
    function->setSubprogram(di_subprogram_);
    di_scopes_.push_back(di_subprogram_);
    set_loc(fn.loc);
}

void CodeGen::debug_end_function() {
    di_scopes_.clear();
    di_subprogram_ = nullptr;
    builder_.SetCurrentDebugLocation(llvm::DebugLoc());
}

void CodeGen::stamp_builder(llvm::IRBuilderBase &builder) const {
    if (llvm::DebugLoc loc = builder_.getCurrentDebugLocation()) {
        builder.SetCurrentDebugLocation(loc);
    }
}

llvm::DIType *CodeGen::debug_type(const Lowered &low) {
    if (di_builder_ == nullptr || di_unit_ == nullptr) {
        return nullptr;
    }
    if (low.is_view) {
        llvm::DIType *pointer = di_builder_->createPointerType(di_builder_->createUnspecifiedType("void"), 64);
        llvm::DIType *length = di_builder_->createBasicType("i32", 32, llvm::dwarf::DW_ATE_signed);
        return di_builder_->createStructType(di_unit_, "[]", debug_file(SourceLoc{}), 0, 128, 64, llvm::DINode::FlagZero,
                                             nullptr, di_builder_->getOrCreateArray({pointer, length}));
    }
    if (low.is_array && low.array_len > 0 && low.depth == 0) {
        Lowered element = low;
        element.is_array = false;
        element.array_len = 0;
        llvm::DIType *inner = debug_type(element);
        if (inner == nullptr) {
            return nullptr;
        }
        const uint64_t elem_bits = inner->getSizeInBits();
        const uint64_t count = static_cast<uint64_t>(low.array_len);
        llvm::DISubrange *range = di_builder_->getOrCreateSubrange(0, low.array_len);
        return di_builder_->createArrayType(elem_bits * count, 0, inner, di_builder_->getOrCreateArray({range}));
    }
    if (low.depth > 0) {
        Lowered pointee = low;
        pointee.depth -= 1;
        pointee.is_array = false;
        pointee.array_len = 0;
        if (pointee.depth == 0 && pointee.elem_void && pointee.struct_id < 0) {
            llvm::DIType *base = di_basic_["void"];
            if (base == nullptr) {
                base = di_builder_->createUnspecifiedType("void");
                di_basic_["void"] = base;
            }
            return di_builder_->createPointerType(base, 64);
        }
        return di_builder_->createPointerType(debug_type(pointee), 64);
    }
    if (low.struct_id >= 0 && low.struct_id < static_cast<int>(structs_.size())) {
        if (const auto found = di_structs_.find(low.struct_id); found != di_structs_.end()) {
            return found->second;
        }
        const StructLayout &layout = structs_[static_cast<std::size_t>(low.struct_id)];
        uint64_t bits = 0;
        uint32_t align = 0;
        if (layout.type != nullptr && layout.type->isSized()) {
            const llvm::DataLayout &data = module_.getDataLayout();
            bits = data.getTypeAllocSizeInBits(layout.type);
            align = static_cast<uint32_t>(data.getABITypeAlign(layout.type).value()) * 8;
        }
        llvm::DICompositeType *type = di_builder_->createStructType(
            di_unit_, layout.name, debug_file(SourceLoc{}), 0, bits, align, llvm::DINode::FlagZero, nullptr,
            di_builder_->getOrCreateArray({}));
        di_structs_.emplace(low.struct_id, type);
        return type;
    }
    std::string name = "i32";
    uint64_t bits = 32;
    unsigned encoding = llvm::dwarf::DW_ATE_signed;
    if (low.elem_bool) {
        name = "bool";
        bits = 8;
        encoding = llvm::dwarf::DW_ATE_boolean;
    } else if (low.elem_float) {
        bits = low.elem_bits >= 64 ? 64 : 32;
        name = bits >= 64 ? "f64" : "f32";
        encoding = llvm::dwarf::DW_ATE_float;
    } else if (low.elem_char) {
        name = "u8";
        bits = 8;
        encoding = llvm::dwarf::DW_ATE_unsigned_char;
    } else if (low.elem_bits == 8) {
        name = low.elem_unsigned ? "u8" : "i8";
        bits = 8;
        encoding = low.elem_unsigned ? llvm::dwarf::DW_ATE_unsigned : llvm::dwarf::DW_ATE_signed;
    } else if (low.elem_bits == 16) {
        name = low.elem_unsigned ? "u16" : "i16";
        bits = 16;
        encoding = low.elem_unsigned ? llvm::dwarf::DW_ATE_unsigned : llvm::dwarf::DW_ATE_signed;
    } else if (low.elem_bits == 64) {
        name = low.elem_unsigned ? "u64" : "i64";
        bits = 64;
        encoding = low.elem_unsigned ? llvm::dwarf::DW_ATE_unsigned : llvm::dwarf::DW_ATE_signed;
    } else if (low.elem_unsigned) {
        name = "u32";
        encoding = llvm::dwarf::DW_ATE_unsigned;
    }
    if (const auto found = di_basic_.find(name); found != di_basic_.end()) {
        return found->second;
    }
    llvm::DIBasicType *type = di_builder_->createBasicType(name, bits, encoding);
    di_basic_.emplace(name, type);
    return type;
}

void CodeGen::debug_declare(const std::string &name, SourceLoc loc, const Binding &binding, unsigned param_index) {
    if (!debug_ || di_builder_ == nullptr || name.empty() || binding.value == nullptr || di_scopes_.empty()) {
        return;
    }
    if (!llvm::isa<llvm::AllocaInst>(binding.value) || builder_.GetInsertBlock() == nullptr) {
        return;
    }
    auto *scope = llvm::dyn_cast<llvm::DILocalScope>(di_scopes_.back());
    if (scope == nullptr) {
        return;
    }
    Lowered low;
    low.depth = binding.depth;
    low.elem_char = binding.elem_char;
    low.is_array = binding.is_array;
    low.array_len = binding.array_len;
    low.struct_id = binding.struct_id;
    low.elem_void = binding.elem_void;
    low.elem_bool = binding.elem_bool;
    low.elem_bits = binding.elem_bits;
    low.elem_unsigned = binding.elem_unsigned;
    low.elem_float = binding.elem_float;
    low.is_view = binding.is_view;
    llvm::DIType *type = debug_type(low);
    if (type == nullptr) {
        return;
    }
    llvm::DIFile *file = debug_file(loc);
    const unsigned line = line_of(diags_, loc);
    llvm::DILocalVariable *var =
        param_index == 0 ? di_builder_->createAutoVariable(scope, name, file, line, type, true)
                         : di_builder_->createParameterVariable(scope, name, param_index, file, line, type, true);
    llvm::DILocation *here = llvm::DILocation::get(context_, line, column_of(diags_, loc), scope);
    di_builder_->insertDeclare(binding.value, var, di_builder_->createExpression(), here, builder_.GetInsertPoint());
}

void CodeGen::debug_global(const std::string &name, llvm::GlobalVariable *global, SourceLoc loc, const Lowered &low,
                           bool local) {
    if (!debug_ || di_builder_ == nullptr || di_unit_ == nullptr || global == nullptr || name.empty()) {
        return;
    }
    llvm::DIType *type = debug_type(low);
    if (type == nullptr) {
        return;
    }
    llvm::DIGlobalVariableExpression *info = di_builder_->createGlobalVariableExpression(
        di_unit_, name, global->getName(), debug_file(loc), line_of(diags_, loc), type, local, true);
    global->addDebugInfo(info);
}

} // namespace tlc
