// Host target, object file, and the codegen() entry point.
#include "gen.hpp"

#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Verifier.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

#include <memory>
#include <string>

namespace tlc {
namespace {

struct TargetState {
    llvm::Triple triple;
    std::unique_ptr<llvm::TargetMachine> machine;
    std::string error;
};

TargetState prepare_target() {
    TargetState state;
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();
    std::string lookup_error;
    state.triple = llvm::Triple(llvm::sys::getDefaultTargetTriple());
    const llvm::Target *target = llvm::TargetRegistry::lookupTarget(state.triple, lookup_error);
    if (!target) {
        state.error = lookup_error;
        return state;
    }
    llvm::TargetOptions options;
    state.machine.reset(target->createTargetMachine(
        state.triple, "generic", "", options, llvm::Reloc::PIC_, std::nullopt, llvm::CodeGenOptLevel::None));
    if (!state.machine) {
        state.error = "cannot create the host target machine";
    }
    return state;
}

bool write_object(llvm::Module &module, llvm::TargetMachine &machine, const std::string &path, std::string &error) {
    std::error_code ec;
    llvm::raw_fd_ostream dest(path, ec, llvm::sys::fs::OF_None);
    if (ec) {
        error = ec.message();
        return false;
    }
    llvm::legacy::PassManager pass;
    if (machine.addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        error = "object emission is not supported for the host target";
        return false;
    }
    pass.run(module);
    dest.flush();
    return true;
}

} // namespace

CodegenOutput codegen(const PackageGraph &graph, DiagnosticEngine &diags, const std::string &object_path,
                      bool testing, bool debug_info) {
    CodegenOutput output;
    TargetState target = prepare_target();
    if (!target.machine) {
        output.error = target.error;
        diags.error(SourceLoc{}, output.error);
        return output;
    }
    auto context = std::make_unique<llvm::LLVMContext>();
    auto module = std::make_unique<llvm::Module>("tlc", *context);
    module->setTargetTriple(target.triple);
    module->setDataLayout(target.machine->createDataLayout());
    CodeGen gen(*context, *module, diags);
    if (testing) {
        gen.enable_tests();
    }
    if (debug_info) {
        gen.enable_debug();
    }
    gen.emit(graph);
    std::string verify_error;
    llvm::raw_string_ostream verify_stream(verify_error);
    if (llvm::verifyModule(*module, &verify_stream)) {
        verify_stream.flush();
        diags.error(SourceLoc{}, "invalid llvm module: " + verify_error);
    }
    llvm::raw_string_ostream ir_stream(output.ir);
    module->print(ir_stream, nullptr);
    ir_stream.flush();
    if (!diags.ok()) {
        output.ok = false;
        output.error = diags.render();
        return output;
    }
    if (!object_path.empty() && !write_object(*module, *target.machine, object_path, output.error)) {
        diags.error(SourceLoc{}, output.error);
        output.ok = false;
        return output;
    }
    output.ok = true;
    return output;
}

} // namespace tlc
