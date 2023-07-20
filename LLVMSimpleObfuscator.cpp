
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

namespace {

class BasicBlockObfuscator {
    Module &M;
    BasicBlock &BB;

    Value *generateOpaquePredicate();

    public: 
    BasicBlockObfuscator(Module &M, BasicBlock &BB) : M(M), BB(BB) {}
    bool shouldObfuscate();
    void insertBogusControlFlow();
};

bool BasicBlockObfuscator::shouldObfuscate() {
    auto *RI = dyn_cast<ReturnInst>(BB.getTerminator());
    if (RI && RI->getReturnValue())
      return true;
    // Maybe some more conditions to be added in the future..
    return false;
}

/**
 * Insert bogus control flow.
 * 
 * Transform:
 * 
 * bb:
 *    orig_val = .... // compute original value
 *    ret orig_val
 * 
 * To:
 * 
 * bb:
 *    res.slot = alloca ...
 *    ....
 *    opaque_pred = ... // compute opaque predicate
 *    br opaque_pred assign_correct assign_fake
 * assign_fake:
 *    fake_val = .... // compute some fake value
 *    store fake_val, res.slot
 *    br new_return
 * assign_correct:
 *    store orig_val, res.slot
 *    br new_return
 * new_return:
 *    new_ret_val = load res.slot
 *    ret new_ret_val
 * 
 **/
void BasicBlockObfuscator::insertBogusControlFlow() {
    IRBuilder Builder(&BB.getParent()->getEntryBlock(),
                 BB.getParent()->getEntryBlock().begin());
    auto *RI = cast<ReturnInst>(BB.getTerminator());
    auto *RTy = RI->getReturnValue()->getType();
    // Store return value here.
    AllocaInst* RetVal = Builder.CreateAlloca(RTy, nullptr, "res.slot");

    auto *FakeBB = BasicBlock::Create(M.getContext(), "assign_fake", BB.getParent());
    auto *OriginalBB = BasicBlock::Create(M.getContext(), "assign_correct", BB.getParent());
    auto *ReturnBB = BasicBlock::Create(M.getContext(), "new_return", BB.getParent());
    
    // Emit opaque predicate and branch.
    Builder.SetInsertPoint(&BB);
    auto *OpaquePred = generateOpaquePredicate();
    BranchInst *BI = Builder.CreateCondBr(OpaquePred, OriginalBB, FakeBB);

    // Modify original BB.
    Builder.SetInsertPoint(OriginalBB);
    Builder.CreateStore(RI->getReturnValue(), RetVal);
    Builder.CreateBr(ReturnBB);
    
    // Remove original return instruction.
    RI->eraseFromParent();
    
    // Build "fake" BB with fake computation.
    Builder.SetInsertPoint(FakeBB);
    // Assign any fake value. Use LLVM's poison just as a simple random value for any type.
    Builder.CreateStore(PoisonValue::get(RTy), RetVal);
    Builder.CreateBr(ReturnBB);

    // Build new return BB
    Builder.SetInsertPoint(ReturnBB);
    auto *RV = Builder.CreateLoad(RTy, RetVal);
    Builder.CreateRet(RV);
}

Value *BasicBlockObfuscator::generateOpaquePredicate() {
    IRBuilder Builder(M.getContext());
    Builder.SetInsertPoint(&BB);

    M.getOrInsertGlobal("bbo", Builder.getInt32Ty());
    auto *GlobarInt = M.getNamedGlobal("bbo");
    GlobarInt->setLinkage(GlobalValue::CommonLinkage);
    GlobarInt->setInitializer(Builder.getInt32(0));
    auto *GlobalIntVal = Builder.CreateLoad(Builder.getInt32Ty(), GlobarInt);

    // An example for a simple opaque predicate is the equation x(x + 1) == 0 mod 2 which is true for all possible x.
    // Source: https://d-nb.info/1204236666/34
    // Proof: https://alive2.llvm.org/ce/z/0ktpG0

    // Emit: (bbo*(bbo + 1) & 1) == 0
    auto *Add1 = Builder.CreateAdd(GlobalIntVal, Builder.getInt32(1));
    auto *MulAdd1 = Builder.CreateMul(GlobalIntVal, Add1);
    auto *LHS = Builder.CreateAnd(MulAdd1, Builder.getInt32(1));
    return Builder.CreateICmpEQ(LHS, ConstantInt::getNullValue(Builder.getInt32Ty()));
}

struct SimpleObfuscatorPass : public PassInfoMixin<SimpleObfuscatorPass> {
    PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM) {
      Module &M = *F.getParent();

      std::vector<BasicBlock *> FnBBs;
      // Original basic blocks in function
      for (auto &BB : F) FnBBs.push_back(&BB);

      for (auto *BB : FnBBs) {
        BasicBlockObfuscator BBO(M, *BB);
        if (BBO.shouldObfuscate())
          BBO.insertBogusControlFlow();
      }

      return PreservedAnalyses::none();
    }
};
} // end anonymous namespace

extern "C" ::llvm::PassPluginLibraryInfo LLVM_ATTRIBUTE_WEAK
llvmGetPassPluginInfo() {
  return {
    LLVM_PLUGIN_API_VERSION, "SimpleObfuscatorPass", "v0.1",
    [](PassBuilder &PB) {
      PB.registerPipelineParsingCallback(
        [](StringRef Name, FunctionPassManager &FPM,
           ArrayRef<PassBuilder::PipelineElement>) {
          if(Name == "obfuscator"){
            FPM.addPass(SimpleObfuscatorPass());
            return true;
          }
          return false;
        }
      );
    }
  };
}