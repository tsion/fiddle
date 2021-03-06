#include "ast.h"
#include "codegen.h"
#include "types.h"
#include <llvm/Analysis/Verifier.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>

namespace fl {
namespace ast {

llvm::Value* IntExpr::codegen(FuncContext* context) const {
  return llvm::ConstantInt::get(context->module->getContext(),
                                llvm::APInt(32, val));
}

llvm::Value* VarExpr::codegen(FuncContext* context) const {
  const std::vector<llvm::Value*> v = (*context->identifierMap)[name];
  if (v.empty()) {
    // TODO(tsion): Diagnose reference to undefined name.
    return nullptr;
  }
  return v.back();
}

llvm::Value* BinOpExpr::codegen(FuncContext* context) const {
  llvm::Value* left  = lhs->codegen(context);
  llvm::Value* right = rhs->codegen(context);
  if (!left || !right) {
    return nullptr;
  }

  llvm::IRBuilder<> builder{context->currentBlock};
  if (name == "+") {
    return builder.CreateAdd(left, right, "add");
  } else if (name == "-") {
    return builder.CreateSub(left, right, "sub");
  } else if (name == "*") {
    return builder.CreateMul(left, right, "mul");
  } else if (name == "/") {
    return builder.CreateSDiv(left, right, "div");
  } else {
    return nullptr;
  }
}

llvm::Value* CallExpr::codegen(FuncContext* context) const {
  llvm::Value* func = functionExpr->codegen(context);
  std::vector<llvm::Value*> args;
  args.reserve(argumentExprs.size());
  for (const auto& argExpr : argumentExprs) {
    args.push_back(argExpr->codegen(context));
  }
  llvm::IRBuilder<> builder{context->currentBlock};
  return builder.CreateCall(func, args, "call");
}

llvm::Value* BlockExpr::codegen(FuncContext* context) const {
  // TODO(tsion): Stop defaulting to integer 0 for empty blocks once we have
  // multiple types.
  llvm::Value* val = llvm::ConstantInt::get(context->module->getContext(),
                                            llvm::APInt(32, 0));

  for (const auto& expr : exprs) {
    val = expr->codegen(context);
  }

  return val;
}

std::unique_ptr<type::Type> getType(Type* astType) {
  std::unique_ptr<type::Type> type;
  if (auto typeName = dynamic_cast<TypeName*>(astType)) {
    if (typeName->name == "i8") {
      type = make_unique<type::Int>(8, true);
    } else if (typeName->name == "i16") {
      type = make_unique<type::Int>(16, true);
    } else if (typeName->name == "i32") {
      type = make_unique<type::Int>(32, true);
    } else if (typeName->name == "i64") {
      type = make_unique<type::Int>(64, true);
    } else {
      std::cerr << "unknown type '" << typeName->name << "'\n";
      std::abort();
    }
  } else if (dynamic_cast<UnitType*>(astType)) {
    type = make_unique<type::Unit>();
  } else {
    std::cerr << "unknown type '" << *type << "'\n";
    std::abort();
  }
  return type;
}

llvm::Function* codegenProto(const FuncProto& proto, llvm::Module* module) {
  llvm::Type* returnType = getType(proto.returnType.get())->llvmType(module);

  std::vector<llvm::Type*> argTypes;
  for (const auto& argType : proto.argTypes) {
    argTypes.push_back(getType(argType.get())->llvmType(module));
  }

  return llvm::Function::Create(
      llvm::FunctionType::get(returnType, argTypes, false),
      llvm::GlobalValue::ExternalLinkage,
      proto.name,
      module);
}

void FuncDef::codegen(ModuleContext* context, llvm::Function* llfunc) const {
  usize i = 0;
  for (auto it = llfunc->arg_begin(); it != llfunc->arg_end(); ++it, ++i) {
    it->setName(proto.argNames[i]);
    context->identifierMap[proto.argNames[i]].push_back(it);
  }

  llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(
      context->module->getContext(),
      "entry",
      llfunc,
      nullptr);

  FuncContext funcContext{context->module, entryBlock, &context->identifierMap};
  llvm::Value* result = body->codegen(&funcContext);

  for (const auto& arg : proto.argNames) {
    context->identifierMap[arg].pop_back();
  }

  llvm::IRBuilder<> builder{entryBlock};
  builder.CreateRet(result);

  assert(!llvm::verifyFunction(*llfunc));
}

std::unique_ptr<llvm::Module> Module::codegen() const {
  auto llmodule = make_unique<llvm::Module>("fiddle", llvm::getGlobalContext());
  ModuleContext context(llmodule.get());

  std::unordered_map<std::string, llvm::Function*> functionMap;

  for (const auto& fn : functions) {
    llvm::Function* llfunc = codegenProto(fn->proto, llmodule.get());
    context.identifierMap[fn->proto.name].push_back(llfunc);
    functionMap[fn->proto.name] = llfunc;
  }

  for (const auto& fn : functions) {
    fn->codegen(&context, functionMap[fn->proto.name]);
  }

  assert(!llvm::verifyModule(*llmodule));

  return llmodule;
};

} // namespace ast
} // namespace fl
