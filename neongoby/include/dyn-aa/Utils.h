#ifndef __DYN_AA_UTILS_H
#define __DYN_AA_UTILS_H

#include <stdint.h>
#include <string>

#include "llvm/IR/Value.h"
#include "llvm/Pass.h"

namespace neongoby {
struct DynAAUtils {
  static const std::string MemAllocHookName;
  static const std::string MainArgsAllocHookName;
  static const std::string TopLevelHookName;
  static const std::string EnterHookName;
  static const std::string StoreHookName;
  static const std::string CallHookName;
  static const std::string ReturnHookName;
  static const std::string GlobalsAllocHookName;
  static const std::string BasicBlockHookName;
  static const std::string MemHooksIniterName;
  static const std::string AfterForkHookName;
  static const std::string BeforeForkHookName;
  static const std::string VAStartHookName;
  static const std::string SlotsName;

  //TxProf
  static const std::string TxProfInitName;
  static const std::string TxProfBeforeCallName;
  static const std::string TxProfAfterCallName;
  static const std::string TxProfEnterName;
  static const std::string TxProfExitName;

  //TxHook
  static const std::string TxHookInitName;
  static const std::string TxHookBeforeCallName;
  static const std::string TxHookAfterCallName;
  static const std::string TxHookEnterName;
  static const std::string TxHookEnter_st_Name;
  static const std::string TxHookExitName;

  //TxHook + TSan
  static const std::string TxHookBeforeCallLockName;
  static const std::string TxHookBeforeCallUnlockName;
  static const std::string TxHookBeforeCallSignalName;
  static const std::string TxHookBeforeCallWaitName;
  static const std::string TxHookBeforeCallJoinName;
  static const std::string TxHookBeforeCallCreateName;
  static const std::string TxHookAfterCallLockName;
  static const std::string TxHookAfterCallUnlockName;
  static const std::string TxHookAfterCallSignalName;
  static const std::string TxHookAfterCallWaitName;
  static const std::string TxHookAfterCallJoinName;
  static const std::string TxHookAfterCallCreateName;


  static const std::string TxHookBeforeCallMallocName;
  static const std::string TxHookBeforeCallCallocName;
  static const std::string TxHookBeforeCallReallocName;
  static const std::string TxHookBeforeCallMemalignName;
  static const std::string TxHookBeforeCallStrdupName;
  static const std::string TxHookBeforeCallFreeName;
  static const std::string TxHookAfterCallMallocName;
  static const std::string TxHookAfterCallCallocName;
  static const std::string TxHookAfterCallReallocName;
  static const std::string TxHookAfterCallMemalignName;
  static const std::string TxHookAfterCallStrdupName;
  static const std::string TxHookAfterCallFreeName;

  /*
   * raw txbegin and txend
   */
  static const std::string TxHookTxEndName;
  static const std::string TxHookTxBeginName;

  /*
   * force slow/fast path
   */
  static const std::string TxHookSwitchToSlowPathName;
  static const std::string TxHookSwitchToFastPathName;

  static void PrintProgressBar(uint64_t Old, uint64_t Now, uint64_t Total);
  static bool PointerIsDereferenced(const llvm::Value *V);
  static void PrintValue(llvm::raw_ostream &O, const llvm::Value *V);
  static bool IsMalloc(const llvm::Function *F);
  static bool IsMallocCall(const llvm::Value *V);
  static bool IsIntraProcQuery(const llvm::Value *V1, const llvm::Value *V2);
  static bool IsReallyIntraProcQuery(const llvm::Value *V1,
                                     const llvm::Value *V2);
  static const llvm::Function *GetContainingFunction(const llvm::Value *V);
};
}

#endif
