#include <cassert>
#include <string>

#include "llvm/IR/Argument.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"

#include "dyn-aa/Utils.h"

using namespace std;
using namespace llvm;
using namespace neongoby;

const string DynAAUtils::MemAllocHookName = "HookMemAlloc";
const string DynAAUtils::MainArgsAllocHookName = "HookMainArgsAlloc";
const string DynAAUtils::TopLevelHookName = "HookTopLevel";
const string DynAAUtils::EnterHookName = "HookEnter";
const string DynAAUtils::StoreHookName = "HookStore";
const string DynAAUtils::CallHookName = "HookCall";
const string DynAAUtils::ReturnHookName = "HookReturn";
const string DynAAUtils::GlobalsAllocHookName = "HookGlobalsAlloc";
const string DynAAUtils::BasicBlockHookName = "HookBasicBlock";
const string DynAAUtils::MemHooksIniterName = "InitMemHooks";
const string DynAAUtils::AfterForkHookName = "HookAfterFork";
const string DynAAUtils::BeforeForkHookName = "HookBeforeFork";
const string DynAAUtils::VAStartHookName = "HookVAStart";
const string DynAAUtils::SlotsName = "ng.slots";

// TxProf
const string DynAAUtils::TxProfInitName = "TxProfInit";
const string DynAAUtils::TxProfBeforeCallName = "TxProfBeforeCall";
const string DynAAUtils::TxProfAfterCallName = "TxProfAfterCall";
const string DynAAUtils::TxProfEnterName = "TxProfEnter";
const string DynAAUtils::TxProfExitName = "TxProfExit";

// TxHook
const string DynAAUtils::TxHookInitName = "TxHookInit";
const string DynAAUtils::TxHookBeforeCallName = "TxHookBeforeCall";
const string DynAAUtils::TxHookAfterCallName = "TxHookAfterCall";
const string DynAAUtils::TxHookEnterName = "TxHookEnter";
const string DynAAUtils::TxHookEnter_st_Name = "TxHookEnter_st";
const string DynAAUtils::TxHookExitName = "TxHookExit";

// TxHook + TSan
const string DynAAUtils::TxHookBeforeCallLockName = "TxHookBeforeCallLock";
const string DynAAUtils::TxHookBeforeCallUnlockName = "TxHookBeforeCallUnlock";
const string DynAAUtils::TxHookBeforeCallSignalName = "TxHookBeforeCallSignal";
const string DynAAUtils::TxHookBeforeCallWaitName = "TxHookBeforeCallWait";
const string DynAAUtils::TxHookBeforeCallJoinName = "TxHookBeforeCallJoin";
const string DynAAUtils::TxHookBeforeCallCreateName = "TxHookBeforeCallCreate";
const string DynAAUtils::TxHookAfterCallLockName = "TxHookAfterCallLock";
const string DynAAUtils::TxHookAfterCallUnlockName = "TxHookAfterCallUnlock";
const string DynAAUtils::TxHookAfterCallSignalName = "TxHookAfterCallSignal";
const string DynAAUtils::TxHookAfterCallWaitName = "TxHookAfterCallWait";
const string DynAAUtils::TxHookAfterCallJoinName = "TxHookAfterCallJoin";
const string DynAAUtils::TxHookAfterCallCreateName = "TxHookAfterCallCreate";

const string DynAAUtils::TxHookBeforeCallMallocName = "TxHookBeforeCallMalloc";
const string DynAAUtils::TxHookBeforeCallCallocName = "TxHookBeforeCallCalloc";
const string DynAAUtils::TxHookBeforeCallReallocName = "TxHookBeforeCallRealloc";
const string DynAAUtils::TxHookBeforeCallMemalignName = "TxHookBeforeCallMemalign";
const string DynAAUtils::TxHookBeforeCallStrdupName = "TxHookBeforeCallStrdup";
const string DynAAUtils::TxHookBeforeCallFreeName = "TxHookBeforeCallFree";
const string DynAAUtils::TxHookAfterCallMallocName = "TxHookAfterCallMalloc";
const string DynAAUtils::TxHookAfterCallCallocName = "TxHookAfterCallCalloc";
const string DynAAUtils::TxHookAfterCallReallocName = "TxHookAfterCallRealloc";
const string DynAAUtils::TxHookAfterCallMemalignName = "TxHookAfterCallMemalign";
const string DynAAUtils::TxHookAfterCallStrdupName = "TxHookAfterCallStrdup";
const string DynAAUtils::TxHookAfterCallFreeName = "TxHookAfterCallFree";


const string DynAAUtils::TxHookTxEndName = "TxHookTxEnd";
const string DynAAUtils::TxHookTxBeginName = "TxHookTxBegin";

const string DynAAUtils::TxHookSwitchToSlowPathName = "TxHookSwitchToSlowPath";
const string DynAAUtils::TxHookSwitchToFastPathName = "TxHookSwitchToFastPath";



void DynAAUtils::PrintProgressBar(uint64_t Old, uint64_t Now, uint64_t Total) {
  assert(Total > 0);
  assert(Now <= Total);

  if (Now == 0) {
    errs().changeColor(raw_ostream::BLUE);
    errs() << " [0%]";
    errs().resetColor();
  } else {
    unsigned CurrentPercentage = Now * 10 / Total;
    unsigned OldPercentage = Old * 10 / Total;
    for (unsigned Percentage = OldPercentage + 1;
         Percentage <= CurrentPercentage; ++Percentage) {
      errs().changeColor(raw_ostream::BLUE);
      errs() << " [" << Percentage * 10 << "%]";
      errs().resetColor();
    }
  }
}

bool DynAAUtils::PointerIsDereferenced(const Value *V) {
  assert(V->getType()->isPointerTy());
  if (isa<Function>(V)) {
    // We always consider missing call edges important.
    return true;
  }
  {
    ImmutableCallSite CS(V);
    if (CS) {
      if (const Function *Callee = CS.getCalledFunction()) {
        if (Callee->isDeclaration()) {
          return true;
        }
      }
    }
  }
  for (Value::const_use_iterator UI = V->use_begin();
       UI != V->use_end(); ++UI) {
    if (const LoadInst *LI = dyn_cast<LoadInst>(*UI)) {
      if (LI->getPointerOperand() == V)
        return true;
    }
    if (const StoreInst *SI = dyn_cast<StoreInst>(*UI)) {
      if (SI->getPointerOperand() == V)
        return true;
    }
    ImmutableCallSite CS(*UI);
    if (CS) {
      if (CS.getCalledValue() == V) {
        // Return true if V is used as a callee.
        return true;
      }
      if (const Function *Callee = CS.getCalledFunction()) {
        if (Callee->isDeclaration()) {
          // Treat as deref'ed if used by an external function call.
          return true;
        }
      }
    }
  }
  return false;
}

void DynAAUtils::PrintValue(raw_ostream &O, const Value *V) {
  if (isa<Function>(V)) {
    O << V->getName();
  } else if (const Argument *Arg = dyn_cast<Argument>(V)) {
    O << Arg->getParent()->getName() << ":  " << *Arg;
  } else if (const Instruction *Ins = dyn_cast<Instruction>(V)) {
    O << Ins->getParent()->getParent()->getName() << ":";
    O << *Ins;
  } else {
    O << *V;
  }
}

bool DynAAUtils::IsMalloc(const Function *F) {
  StringRef Name = F->getName();
  return (Name == "malloc" ||
          Name == "calloc" ||
          Name == "valloc" ||
          Name == "realloc" ||
          Name == "memalign" ||
          Name.startswith("_Znwj") ||
          Name.startswith("_Znwm") ||
          Name.startswith("_Znaj") ||
          Name.startswith("_Znam") ||
          Name == "strdup" ||
          Name == "__strdup" ||
          Name == "getline");
}

bool DynAAUtils::IsMallocCall(const Value *V) {
  ImmutableCallSite CS(V);
  if (!CS)
    return false;

  const Function *Callee = CS.getCalledFunction();
  if (!Callee)
    return false;
  return IsMalloc(Callee);
}

bool DynAAUtils::IsIntraProcQuery(const Value *V1, const Value *V2) {
  assert(V1->getType()->isPointerTy() && V2->getType()->isPointerTy());
  const Function *F1 = GetContainingFunction(V1);
  const Function *F2 = GetContainingFunction(V2);
  return F1 == NULL || F2 == NULL || F1 == F2;
}

// FIXME: Oh my god! What a name!
bool DynAAUtils::IsReallyIntraProcQuery(const Value *V1, const Value *V2) {
  assert(V1->getType()->isPointerTy() && V2->getType()->isPointerTy());
  const Function *F1 = GetContainingFunction(V1);
  const Function *F2 = GetContainingFunction(V2);
  return F1 != NULL && F2 != NULL && F1 == F2;
}

const Function *DynAAUtils::GetContainingFunction(const Value *V) {
  if (const Instruction *Ins = dyn_cast<Instruction>(V))
    return Ins->getParent()->getParent();
  if (const Argument *Arg = dyn_cast<Argument>(V))
    return Arg->getParent();
  return NULL;
}
