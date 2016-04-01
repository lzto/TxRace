// vim: sw=2

#define DEBUG_TYPE "dyn-aa"

#include <string>

/*
 * used by Tx Reduction
 */
#include <map>
#include <list>
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Support/CFG.h"

#include "llvm/Transforms/Scalar.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Transforms/Scalar.h"
//////////////////////

#include "llvm/IR/Module.h"
#include "llvm/Pass.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Transforms/Utils/BuildLibCalls.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/DebugInfo.h"

#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/ValueMapper.h"

/////////////// TSAN ///////////////
#include "llvm/Transforms/Instrumentation.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/Analysis/CaptureTracking.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/ModuleUtils.h"
#include "llvm/Transforms/Utils/SpecialCaseList.h"

///////////////////////////////////

#include "rcs/typedefs.h"
#include "rcs/IDAssigner.h"

#include "dyn-aa/Passes.h"
#include "dyn-aa/Utils.h"

//////////////////////////////////////////
/*
 * clone function
 */
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/CodeGen/SelectionDAGNodes.h"

#define _DUPLICATED_FUNCTION_SUFFIX_ ("_TxPreservedOrigFunction")

//////////////////////////////////////////
/*
 * this is switch for force slow path
 */
#define USE_DONOT_BOTHER_TX_IF_IT_IS_SHORT 0

///////////////////////////////////////////
// calculate tsan free function and fix MTSCC for these tsan free region
#define USE_REMOVE_USELESS_TX

using namespace llvm;
using namespace std;
using namespace rcs;

#define GV_RUNTX_NAME "runTx"


namespace neongoby {
struct TxInstrumenter: public ModulePass {
  static char ID;

  TxInstrumenter();
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnModule(Module &M);

private:
  unsigned int instrumentInstructionIfNecessary(Instruction *I);
  void removeTxFromTsanFreeRegion(BasicBlock *BB);
  int doNotBotherTxIfItIsShort(BasicBlock *BB);
  void instrumentCallSite(CallSite CS);
  void instrumentEntry(Function &F);
  void instrumentReturnInst(Instruction *I);


  void instrumentCallSiteBefore(CallSite CS);
  void instrumentCallSiteAfter(CallSite CS);
  int callSiteCanBeHooked(CallSite CS);

  BasicBlock::iterator CalcNextLoc(Instruction *I);

  BasicBlock* getBasicBlockByName(Function* F, string basicBlockName);

  void setupScalarTypes(Module &M);
  void setupHooks(Module &M);

  void processLog(const std::string &LogFileName);
  void processMustHook(const std::string &FileName);

  // TxProf
  Function *TxProfInit;
  Function *TxProfBeforeCall;
  Function *TxProfAfterCall;
  Function *TxProfEnter;
  Function *TxProfExit;
  // TxHook
  Function *TxHookInit;
  Function *TxHookBeforeCall;
  Function *TxHookAfterCall;
  Function *TxHookEnter;
  Function *TxHookEnter_st;
  Function *TxHookExit;

  Function *TxHookTxEnd;
  Function *TxHookTxBegin;

  Function *TxHookSwitchToSlowPath;
  Function *TxHookSwitchToFastPath;


  // the main function
  Function *Main;
  // types
  IntegerType *CharType, *LongType, *IntType, *Int1Type;
  PointerType *CharStarType;
  Type *VoidType;


  //
  DenseSet<unsigned /*InsID*/> Libsys_InsID;
  DenseSet<unsigned /*FuncID*/> Entry_FuncID;
  DenseSet<unsigned /*FuncID*/> Entry_st_FuncID;
  DenseSet<unsigned /*FuncID*/> Blacklist_FuncID;
  DenseSet<unsigned /*InsID*/> Useless_b_InsID;
  DenseSet<unsigned /*InsID*/> Useless_a_InsID;
  DenseSet<unsigned /*InsID*/> Useful_b_InsID;
  DenseSet<unsigned /*InsID*/> Useful_a_InsID;

  std::set<std::string> MustHook_FuncName;

  /*
   * if a function's attribute is OT and it is cloned(preserved)
   * its name is added into clonePreserved_FuncName
   * when there is a call site inside a single thread region,
   * the name of the callee is replaced with the cloned(preserved) function
   */

  std::set<std::string> clonePreserved_FuncName;
  //TODO: if cloned function is identical to original function, revert it to the original state
  std::set<Function*> cloneTgtFunctions;
  std::set<Function*> cloneSrcFunctions;

  //////////////////

  void duplicateFunction(Function* F);
  void processSTFunction(Function *F);

/////////////// TSAN ///////////////
  bool __TSAN_runOnFunction(Function &F);
  bool __TSAN_doInitialization(Module &M);

  void initializeCallbacks(Module &M);
  bool instrumentLoadOrStore(Instruction *I, const DataLayout &DL);
  bool instrumentAtomic(Instruction *I, const DataLayout &DL);
  bool instrumentMemIntrinsic(Instruction *I);
  void chooseInstructionsToInstrument(SmallVectorImpl<Instruction*> &Local,
                                      SmallVectorImpl<Instruction*> &All,
                                      const DataLayout &DL);
  bool addrPointsToConstantData(Value *Addr);
  int getMemoryAccessFuncIndex(Value *Addr, const DataLayout &DL);

//  DataLayout *TD;
  Type *IntptrTy;
  SmallString<64> BlacklistFile;
  OwningPtr<SpecialCaseList> BL;
  IntegerType *OrdTy;
  // Callbacks to run-time library are computed in doInitialization.
  Function *TsanFuncEntry;
  Function *TsanFuncExit;
  // Accesses sizes are powers of two: 1, 2, 4, 8, 16.
  static const size_t kNumberOfAccessSizes = 5;
  Function *TsanRead[kNumberOfAccessSizes];
  Function *TsanWrite[kNumberOfAccessSizes];
  Function *TsanUnalignedRead[kNumberOfAccessSizes];
  Function *TsanUnalignedWrite[kNumberOfAccessSizes];
  Function *TsanAtomicLoad[kNumberOfAccessSizes];
  Function *TsanAtomicStore[kNumberOfAccessSizes];
  Function *TsanAtomicRMW[AtomicRMWInst::LAST_BINOP + 1][kNumberOfAccessSizes];
  Function *TsanAtomicCAS[kNumberOfAccessSizes];
  Function *TsanAtomicThreadFence;
  Function *TsanAtomicSignalFence;
  Function *TsanVptrUpdate;
  Function *TsanVptrLoad;
  Function *MemmoveFn, *MemcpyFn, *MemsetFn;


////////////////////////////////////
  /*
   * Tx Loop Cut Function
   */
  Function *Tx_cut_loop;

  void processLoopCutInfoFile(const std::string &filepath);

  std::map<int, int> LoopThreshold;

  bool hasLoopCutInfoFile = false;

////////////////////////////////////
  /*
   * Tx Reduction
   */

  //States
  enum TRThreadState
  {
    ST = 0, //Single Thread
    MT,//Multithread
    OT,//Mixed state, either single or multithread
  };

  //Instruction Type, i.e. condition
  enum TRInstructionType
  {
    S = 0, //Instruction indicates single thread
    M,//Instruction indicates multithread
    A,//Any Instruction except S and M
  };

  //////////////////////////////////
  /*
   * used by SCC to calculate SCC thread state
   */
  //Basic block state
  enum TRBBState
  {
    BST = 0,//Single-thread
    BMT,//Multi-thread
    BUT//Undefined thread state
  };

  /* [TRSCCMachine]
   * --------------------------------------------
   * Current      Cond      |    Transition
   * --------------------------------------------
   *    BST        BST       |        BST
   *    BST        BMT       |        BMT
   *    BST        BUT       |        BST
   *    BMT        BST       |        BMT
   *    BMT        BMT       |        BMT
   *    BMT        BUT       |        BMT
   *    BUT        BST       |        BST
   *    BUT        BMT       |        BMT
   *    BUT        BUT       |        BUT
   */
  //Current,Cond
  TRBBState TRSCCMachine[3][3]
  {
    {BST, BMT, BST},
    {BMT, BMT, BMT},
    {BST, BMT, BUT}
  };
  /*
   * Calculated Result is stored in bbTailState
   * map from BB Name to TRThreadState
   */
  std::map<string, TRBBState> bbTailState;
  /*
   * whether this bb need txend at the beginning
   */
  std::map<string, bool> bbTailMiscState;

  ///////////////////////////////////////
  /*
   * Function Attribute Table Stores whether a function is called in
   * Multithread context or only in Single thread context
   */
  std::map<string, TRThreadState> functionAttributeTable;

  /*
   * Function Thread State Table Stores whether a function respawna new threads or not
   */
  std::map<string, TRThreadState> functionThreadStateTable;

  TRThreadState getTRThreadStateForFunction(string functionName);

  /*
   * intermediate result, saved for further use
   */
  std::map<string, std::map<string, std::list<std::string>*>> FuncSCCres;
  std::map<string, std::list<std::list<std::string>*>> FuncSCCRawres;


  void ConstructFunctionAttributeTable(Module &M);

  /*
   * calculate tsan free function and tsan free MT SCC,
   * if MTSCC is tsan free
   *         then exclude it from TX
   * if MTSCC is not tsan free and contains big tsan free function
   *         then exclude tsan free function
   */
  //temporily store result from __TSAN_runOnFunction()
  std::set<Function*> tsanFreeFunctions;
  std::set<Function*> tsanTouchedFunctions;
  //verified functions are stored here
  std::set<Function*> verifiedTsanFreeFunctions;
  std::set<Function*> falseTsanFreeFunctions;

  void CalculateTsanFreeFunctionsAndFixMTSCC(Module &M);

/////////////////////////////////////
};
}

using namespace neongoby;

char TxInstrumenter::ID = 0;

static RegisterPass<TxInstrumenter> X("tx-instrument",
                                      "Instrument TX begin/ends",
                                      false, false);

static cl::opt<bool> isProfile("profile", cl::desc("TX Profile"));

static cl::opt<bool> isTSan("tx-tsan", cl::desc("Thread Sanitizer"));

static cl::opt<bool> useLoopCut("ulc", cl::desc("Use Loop Cut"));

static cl::opt<string> LoopCutInfoFile("ltf", cl::desc("Loop threshold file"));

static cl::opt<string> ProfileLogFileName("log", cl::desc("Profile log file name"));

static cl::opt<string> MustHookFileName("musthook", cl::desc("A list of library functions must be hooked"));

static cl::opt<string> TxHookInitFuncName("init", cl::desc("A function to instrument TxProfInit/TxHookInit (if not main)"));

/////////////// TSAN ///////////////
static cl::opt<std::string>  ClBlacklistFile("tx-tsan-blacklist",
    cl::desc("Blacklist file"), cl::Hidden);
static cl::opt<bool>  ClInstrumentMemoryAccesses(
  "tx-tsan-instrument-memory-accesses", cl::init(true),
  cl::desc("Instrument memory accesses"), cl::Hidden);
static cl::opt<bool>  ClInstrumentFuncEntryExit(
  "tx-tsan-instrument-func-entry-exit", cl::init(false/*true*/),
  cl::desc("Instrument function entry and exit"), cl::Hidden);
static cl::opt<bool>  ClInstrumentAtomics(
  "tx-tsan-instrument-atomics", cl::init(true),
  cl::desc("Instrument atomics"), cl::Hidden);
static cl::opt<bool>  ClInstrumentMemIntrinsics(
  "tx-tsan-instrument-memintrinsics", cl::init(true),
  cl::desc("Instrument memintrinsics (memset/memcpy/memmove)"), cl::Hidden);

STATISTIC(NumInstrumentedReads, "Number of instrumented reads");
STATISTIC(NumInstrumentedWrites, "Number of instrumented writes");
STATISTIC(NumOmittedReadsBeforeWrite,
          "Number of reads ignored due to following writes");
STATISTIC(NumAccessesWithBadSize, "Number of accesses with bad size");
STATISTIC(NumInstrumentedVtableWrites, "Number of vtable ptr writes");
STATISTIC(NumInstrumentedVtableReads, "Number of vtable ptr reads");
STATISTIC(NumOmittedReadsFromConstantGlobals,
          "Number of reads from constant globals");
STATISTIC(NumOmittedReadsFromVtable, "Number of vtable reads");
STATISTIC(NumEliminatedNotsanTxAnotation, "Number of tsan free TxAnotation Eliminated");
STATISTIC(NumSTFuncSkipped, "Number of Single thread function skipped");
STATISTIC(NumOTFuncDuplicated, "Number of OT functions duplicated");
STATISTIC(NumForcedSlowPath, "Number of Forced-Slow Path BB");
////////////////////////////////////

/*
static cl::opt<bool> HookAllPointers("hook-all-pointers",
                                     cl::desc("Hook all pointers"));
static cl::opt<bool> Diagnose("diagnose",
                              cl::desc("Instrument for test case reduction and "
                                       "trace slicing"));
static cl::list<string> OfflineWhiteList(
    "offline-white-list", cl::desc("Functions which should be hooked"));
*/

ModulePass *neongoby::createTxInstrumenterPass() {
  return new TxInstrumenter();
}

void TxInstrumenter::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<DataLayout>();
  AU.addRequired<IDAssigner>();
  AU.addRequired<TargetLibraryInfo>();
  AU.addRequired<LoopInfo>();
}

TxInstrumenter::TxInstrumenter(): ModulePass(ID) {

  TxProfInit = NULL;
  TxProfBeforeCall = NULL;
  TxProfAfterCall = NULL;
  TxProfEnter = NULL;
  TxProfExit = NULL;

  TxHookInit = NULL;
  TxHookBeforeCall = NULL;
  TxHookAfterCall = NULL;
  TxHookEnter = NULL;
  TxHookEnter_st = NULL;
  TxHookExit = NULL;

  TxHookTxEnd = NULL;
  TxHookTxBegin = NULL;

  TxHookSwitchToFastPath = NULL;
  TxHookSwitchToSlowPath = NULL;

  Main = NULL;
  CharType = LongType = IntType = Int1Type = NULL;
  CharStarType = NULL;
  VoidType = NULL;

  Tx_cut_loop = NULL;

}
/*
 * Helper function, get a basicblock for a function by name
 */
BasicBlock* TxInstrumenter::getBasicBlockByName(Function* F, string basicBlockName)
{
  for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
  {
    if (BB->getName() == basicBlockName)
      return (BasicBlock*)BB;
  }
  return NULL;
}

BasicBlock::iterator TxInstrumenter::CalcNextLoc(Instruction *I) {

  BasicBlock::iterator Loc;

  ////////////////////////////////////////////////////////////////////////////////
  // TODO: [DY]
  // I found that pbzip2 resulted in assertion violation at assert(!I->isTerminator());
  // I don't understand the following logic, but simply took it from neongoby's
  // instrumentPointerInstruction function. It looks working fine.

  // calculate the next Loc
  if (isa<PHINode>(I)) {
    // Cannot insert hooks right after a PHI, because PHINodes have to be
    // grouped together.
    Loc = I->getParent()->getFirstNonPHI();
  } else if (!I->isTerminator()) {
    Loc = I;
    ++Loc;
  } else {
    assert(isa<InvokeInst>(I));
    InvokeInst *II = cast<InvokeInst>(I);
    BasicBlock *NormalDest = II->getNormalDest();
    // It's not always OK to insert HookTopLevel simply at the beginning of the
    // normal destination, because the normal destionation may be shared by
    // multiple InvokeInsts. In that case, we will create a critical edge block,
    // and add the HookTopLevel over there.
    if (NormalDest->getUniquePredecessor()) {
      Loc = NormalDest->getFirstNonPHI();
    } else {
      BasicBlock *CritEdge = BasicBlock::Create(I->getContext(),
                             "crit_edge",
                             I->getParent()->getParent());
      Loc = BranchInst::Create(NormalDest, CritEdge);
      // Now that CritEdge becomes the new predecessor of NormalDest, replace
      // all phi uses of I->getParent() with CritEdge.
      for (auto J = NormalDest->begin();
           NormalDest->getFirstNonPHI() != J;
           ++J) {
        PHINode *Phi = cast<PHINode>(J);
        int i;
        while ((i = Phi->getBasicBlockIndex(I->getParent())) >= 0)
          Phi->setIncomingBlock(i, CritEdge);
      }
      II->setNormalDest(CritEdge);
    }
  }
  ////////////////////////////////////////////////////////////////////////////////

  return Loc;
}

/*
 * call can be hooked or not?
 * return hook type
 * -1: can not be hooked
 */
int TxInstrumenter::callSiteCanBeHooked(CallSite CS)
{
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned InsID = IDA.getInstructionID(CS.getInstruction());
  if (InsID == IDAssigner::InvalidID)
    return false;

  Function *Callee = CS.getCalledFunction();
  if (!Callee)
    return -1;// can not be hooked

  StringRef CalleeName = Callee->getName();

  if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end())
  {
    return 1;
    //return 4;//MustHook
  } else
  {
    if (Libsys_InsID.find(InsID) != Libsys_InsID.end())
    {
      return 1;
      //return 5;//not profiled functions
    }
  }
  return -1;
}

void TxInstrumenter::instrumentCallSiteBefore(CallSite CS) {
  Instruction *I = CS.getInstruction();
  //assert(!I->isTerminator());

  // get id
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned InsID = IDA.getInstructionID(CS.getInstruction());
  assert(InsID != IDAssigner::InvalidID);

  if (isProfile) {
    // args
    vector<Value *> Args;
    Args.push_back(ConstantInt::get(IntType, InsID));

    // instrument BeforeCall
    BasicBlock::iterator Loc = I;
    CallInst::Create(TxProfBeforeCall, Args, "", Loc);
  }
  else if (isTSan) {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    BasicBlock::iterator Loc = I;

    // instrument BeforeCall
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end())
    {
      CallInst::Create(TxHookBeforeCall, "", Loc);
    }
    else
    {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end())
      {
        CallInst::Create(TxHookBeforeCall, "", Loc);
      }
    }
  }
  else {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    // TODO: NEED TO INSTRUMENT REDUNDANT READS FOR FUNCTION ARGUMENTS
    //int NumCallingArgs = CS.arg_size();

    // instrument BeforeCall
    BasicBlock::iterator Loc = I;

    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end())
    {
      //errs() << "[MUSTHOOK-Before]" << CalleeName << "\n";
      CallInst::Create(TxHookBeforeCall, "", Loc);
    } else
    {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end())
      {
        CallInst::Create(TxHookBeforeCall, "", Loc);
      }
    }
  }
}


void TxInstrumenter::instrumentCallSiteAfter(CallSite CS) {
  Instruction *I = CS.getInstruction();
  //assert(!I->isTerminator());

  // get id
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned InsID = IDA.getInstructionID(CS.getInstruction());
  assert(InsID != IDAssigner::InvalidID);

  if (isProfile) {
    // args
    vector<Value *> Args;
    Args.push_back(ConstantInt::get(IntType, InsID));

    // instrument BeforeCall
    BasicBlock::iterator Loc = I;
    CallInst::Create(TxProfBeforeCall, Args, "", Loc);

    // calculate the next Loc
    Loc = CalcNextLoc(I);

    // instrument AfterCall
    CallInst::Create(TxProfAfterCall, Args, "", Loc);
  }
  else if (isTSan) {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    BasicBlock::iterator Loc = I;

    // calculate the next Loc
    Loc = CalcNextLoc(I);

    // instrument AfterCall
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end())
    {
      CallInst::Create(TxHookAfterCall, "", Loc);
    }
    else
    {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookAfterCall, "", Loc);
      }
    }

  }
  else {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    // TODO: NEED TO INSTRUMENT REDUNDANT READS FOR FUNCTION ARGUMENTS
    //int NumCallingArgs = CS.arg_size();

    // calculate the next Loc
    BasicBlock::iterator Loc = CalcNextLoc(I);

    // instrument AfterCall
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end()) {
      //errs() << "[MUSTHOOK-After]" << CalleeName << "\n";
      CallInst::Create(TxHookAfterCall, "", Loc);
    }
    else {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookAfterCall, "", Loc);
      }
    }
  }
}



void TxInstrumenter::instrumentCallSite(CallSite CS) {
  Instruction *I = CS.getInstruction();
  //assert(!I->isTerminator());

  // get id
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned InsID = IDA.getInstructionID(CS.getInstruction());
  assert(InsID != IDAssigner::InvalidID);

  if (isProfile) {
    // args
    vector<Value *> Args;
    Args.push_back(ConstantInt::get(IntType, InsID));

    // instrument BeforeCall
    BasicBlock::iterator Loc = I;
    CallInst::Create(TxProfBeforeCall, Args, "", Loc);

    // calculate the next Loc
    Loc = CalcNextLoc(I);

    // instrument AfterCall
    CallInst::Create(TxProfAfterCall, Args, "", Loc);
  }
  else if (isTSan) {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    BasicBlock::iterator Loc = I;

    // instrument BeforeCall
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end()) {
      CallInst::Create(TxHookBeforeCall, "", Loc);
    }
    else
    {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookBeforeCall, "", Loc);
      }
    }

    // calculate the next Loc
    Loc = CalcNextLoc(I);

    // instrument AfterCall
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end()) {
      CallInst::Create(TxHookAfterCall, "", Loc);
    }
    else {

      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookAfterCall, "", Loc);
      }
    }

  }
  else {

    Function *Callee = CS.getCalledFunction();
    if (!Callee)
      return;
    StringRef CalleeName = Callee->getName();

    // TODO: NEED TO INSTRUMENT REDUNDANT READS FOR FUNCTION ARGUMENTS
    //int NumCallingArgs = CS.arg_size();

    // instrument BeforeCall
    BasicBlock::iterator Loc = I;
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end()) {
      //errs() << "[MUSTHOOK-Before]" << CalleeName << "\n";
      CallInst::Create(TxHookBeforeCall, "", Loc);
    }
    else {
      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookBeforeCall, "", Loc);
      }
    }

    // calculate the next Loc
    Loc = CalcNextLoc(I);

    // instrument AfterCall
    // MustHook
    if (MustHook_FuncName.find(CalleeName.str()) != MustHook_FuncName.end()) {
      //errs() << "[MUSTHOOK-After]" << CalleeName << "\n";
      CallInst::Create(TxHookAfterCall, "", Loc);
    }
    else {

      // Do not Instruement if not profiled
      if (Libsys_InsID.find(InsID) != Libsys_InsID.end()) {
        CallInst::Create(TxHookAfterCall, "", Loc);
      }
    }
  }
}

void TxInstrumenter::instrumentEntry(Function &F) {
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned FuncID = IDA.getFunctionID(&F);
  // Skip the functions added by us.
  if (FuncID != IDAssigner::InvalidID) {
    if (isProfile) {
      // instrument Enter
      CallInst::Create(TxProfEnter,
                       ConstantInt::get(IntType, FuncID),
                       "",
                       F.begin()->getFirstInsertionPt());
    }
    else {

      // Do not Instruement if not profiled
      if (Entry_FuncID.find(FuncID) == Entry_FuncID.end()) {
        return;
      }


      /*
       * we do not start tx at this entry point where it is single thread,
       * till we encounter multi thread region
       */
      if (Entry_st_FuncID.find(FuncID) != Entry_st_FuncID.end())
      {
        printf("This is main function , insert TxHookEnter_st\n");
        CallInst::Create(TxHookEnter_st,
                         ConstantInt::get(IntType, FuncID),
                         "",
                         F.begin()->getFirstInsertionPt());

        return;
      }

      // instrument Enter
      CallInst::Create(TxHookEnter,
                       ConstantInt::get(IntType, FuncID),
                       "",
                       F.begin()->getFirstInsertionPt());
    }
  }
}

void TxInstrumenter::instrumentReturnInst(Instruction *I) {
  IDAssigner &IDA = getAnalysis<IDAssigner>();
  unsigned InsID = IDA.getInstructionID(I);
  assert(InsID != IDAssigner::InvalidID);
  unsigned FuncID = IDA.getFunctionID(I->getParent()->getParent());
  assert(FuncID != IDAssigner::InvalidID);

  if (isProfile) {
    // instrument Exit
    vector<Value *> Args;
    Args.push_back(ConstantInt::get(IntType, FuncID));
    CallInst::Create(TxProfExit, Args, "", I);
  }
  else {

    // Do not Instruement if not profiled
    if (Entry_FuncID.find(FuncID) == Entry_FuncID.end()) {
      return;
    }

    // instrument Exit
    vector<Value *> Args;
    Args.push_back(ConstantInt::get(IntType, FuncID));
    CallInst::Create(TxHookExit, Args, "", I);
  }
}

/*
 * conservatively remove tx from tsan free region in BB
 */
void TxInstrumenter::removeTxFromTsanFreeRegion(BasicBlock *BB)
{
  /*
   * states
   * ----------------------
   * State, Inst, NState, Op
   * ----------------------
   * S    , txha, T     , add Ins to list
   * S    , txhb, S     , add Ins to list
   * S    , tsan, S     , nop //error actually, tsan should not exists outside of tx region
   * S    , misc, S     , nop
   * T    , txha, T     , add Inst to list
   * T    , txhb, T     , add Inst to list
   * T    , tsan, S     , remove instruction [1,n-2] in list, where index(list)=[0,n-1]
   * T    , misc, T     , nop
   */
  enum rtfrState
  {
    S = 0,
    T = 1
  };
  enum rtfrInstType
  {
    txha,
    txhb,
    tsan,
    misc
  };
  enum rtfrOp
  {
    op_nop,
    op_add,
    op_flush,
    op_err,
  };
  rtfrState smstable[2][4] =
  {
    {
      T,//S, txha
      S,//S, txhb
      S,//S, tsan
      S,//S, misc
    },
    {
      T,//T, txha
      T,//T, txhb
      S,//T, tsan
      T//T, misc
    }
  };
  rtfrOp smotable[2][4] =
  {
    {
      op_add,//S, txha
      op_add,//S, txhb
      op_err,//S, tsan
      op_nop,//S, misc
    },
    {
      op_add,//T, txha
      op_add,//T, txhb
      op_flush,//T, tsan
      op_nop//T, misc
    }
  };
  rtfrState state = S;
  rtfrInstType type;
  vector<Instruction*> ilist;
  for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
  {
    type = misc;
    CallSite CS(I);
    if (CS)
    {
      Function *Callee = CS.getCalledFunction();
      if (Callee)
      {
        StringRef CalleeName = Callee->getName();
        //errs()<<"Found "<<CalleeName.str()<<"\n";
        if (CalleeName.startswith("TxHookBefore"))
        {
          type = txhb;
        } else if (CalleeName.startswith("TxHookAfter"))
        {
          type = txha;
        } else if (CalleeName.startswith("__wrap_tsan_") || (CalleeName.startswith("__tsan_")))
        {
          type = tsan;
        }
      }
    }
    //errs()<<"SM: "<<state<<"["<<type<<"]"<<" to ";
    rtfrOp action = smotable[state][type];
    state = smstable[state][type];
    //errs()<<""<<state<<" action "<<action<<"\n";
    switch (action)
    {
    case (op_nop):
    {
      break;
    }
    case (op_err):
    {
      break;
    }
    case (op_add):
    {
      ilist.push_back(I);
      break;
    }
    case (op_flush):
    {
      /*
       * iterator and remove instructions in the list from BB
       */
      for (unsigned i = 1; i < (ilist.size() - 1); i++)
      {
        ilist[i]->eraseFromParent();
        NumEliminatedNotsanTxAnotation++;
      }
      ilist.clear();
      break;
    }
    }
  }
  if (ilist.size() != 0)
  {
    for (unsigned i = 1; i < (ilist.size() - 1); i++)
    {
      ilist[i]->eraseFromParent();
      NumEliminatedNotsanTxAnotation++;
    }
    ilist.clear();
  }
  //errs()<<"----------\n";
}

int TxInstrumenter::doNotBotherTxIfItIsShort(BasicBlock *BB)
{
  /*
   * states
   * ----------------------
   * State, Inst, NState, Op
   * ----------------------
   * S    , txha,  S     , op_nop
   * S    , txhb,  A     , op_rec  record instruction
   * S    , tsan,  S     , op_nop
   * S    , misc,  S     , op_nop
   * A    , txha,  B     , op_rec  record instruction
   * A    , txhb,  A     , op_rec
   * A    , tsan,  A     , op_nop
   * A    , misc,  A     , op_nop
   * B    , txha,  B     , op_rec
   * B    , txhb,  C     , op_rec
   * B    , tsan,  B     , op_cnt_tsan
   * B    , misc,  B     , op_cnt_misc
   * C    , txha,  E     , op_rec
   * C    , txhb,  C     , op_rec
   * C    , tsan,  C     , op_nop
   * C    , misc,  C     , op_nop
   * E    , *    ,  S     , op_flush
   */
  enum dnbtxState
  {
    S,
    A,
    B,
    C,
    E
  };
  enum dnbtxInstType
  {
    txha,
    txhb,
    tsan,
    misc
  };
  enum dnbtxOp
  {
    op_rec,
    op_cnt_tsan,
    op_cnt_misc,
    op_nop,
    op_flush
  };

  dnbtxState dnbtxMachineS [5][4] =
  {
    {S, A, S, S},
    {B, A, A, A},
    {B, C, B, B},
    {E, C, C, C},
    {S, S, S, S},
  };

  dnbtxOp dnbtxMachineO [5][4] =
  {
    {op_nop, op_rec, op_nop, op_nop},
    {op_rec, op_rec, op_nop, op_nop},
    {op_rec, op_rec, op_cnt_tsan, op_cnt_misc},
    {op_rec, op_rec, op_nop, op_nop},
    {op_flush, op_flush, op_flush, op_flush},
  };

  int savedTx = 0;

  dnbtxState state = S;
  dnbtxInstType type;
  vector<Instruction*> ilist;

  int tsan_ins_cnt = 0;
  int misc_ins_cnt = 0;

  for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
  {
    type = misc;
    CallSite CS(I);
    if (CS)
    {
      Function *Callee = CS.getCalledFunction();
      if (Callee)
      {
        StringRef CalleeName = Callee->getName();
        //errs()<<"Found "<<CalleeName.str()<<"\n";
        if (CalleeName.startswith("TxHookBefore"))
        {
          type = txhb;
        } else if (CalleeName.startswith("TxHookAfter"))
        {
          type = txha;
        } else if (CalleeName.startswith("__wrap_tsan_") || (CalleeName.startswith("__tsan_")))
        {
          type = tsan;
        }
      }
    }
    dnbtxOp op = dnbtxMachineO[state][type];
    state = dnbtxMachineS[state][type];
    switch (op)
    {
    case (op_rec):
      ilist.push_back(I);
      break;
    case (op_cnt_tsan):
      tsan_ins_cnt++;
      break;
    case (op_cnt_misc):
      misc_ins_cnt++;
      break;
    case (op_nop):
      break;
    }
    if (state == E)
    {
      /*
       * FIXME: adjust threshold, currently it is 5
       */
      if ((ilist.size() != 0) && (tsan_ins_cnt < 20))
      {
        NumForcedSlowPath++;
        savedTx++;
        Instruction * firstIns = CalcNextLoc(ilist[0]);
        Instruction * lastIns = CalcNextLoc(ilist[ilist.size() - 1]);

        for (unsigned i = 0; i < ilist.size(); i++)
        {
          ilist[i]->eraseFromParent();
        }
        CallInst::Create(TxHookSwitchToSlowPath, "", firstIns);
        CallInst::Create(TxHookSwitchToFastPath, "", lastIns);
      }
      ilist.clear();
      state = S;
      tsan_ins_cnt = 0;
      misc_ins_cnt = 0;
    }
  }
  return savedTx;
}

unsigned int TxInstrumenter::instrumentInstructionIfNecessary(Instruction *I) {

  // Skip those instructions added by us.
  IDAssigner &IDA = getAnalysis<IDAssigner>();

  if (IDA.getValueID(I) == IDAssigner::InvalidID)
    return 0;

  // Instrument returns and resume.
  if (isa<ReturnInst>(I) || isa<ResumeInst>(I)) {
    instrumentReturnInst(I);
    return 1;
  }
  CallSite CS(I);
  if (!CS)
    return 0;
  if (isProfile)
  {
    instrumentCallSite(CS);
    return 1;
  }

  int hooktype = callSiteCanBeHooked(CS);
  if (hooktype == -1)
  {
    return 0;
  }
  instrumentCallSiteBefore(CS);
  instrumentCallSiteAfter(CS);
  return 1;
}


static Function *checkInterfaceFunction(Constant * FuncOrBitcast) {
  if (Function *F = dyn_cast<Function>(FuncOrBitcast))
    return F;
  FuncOrBitcast->dump();
  report_fatal_error("TxInstrumenter interface function redefined");
}


void TxInstrumenter::setupHooks(Module &M) {
  // No existing functions have the same name.
  assert(M.getFunction(DynAAUtils::TxProfInitName) == NULL);
  assert(M.getFunction(DynAAUtils::TxProfBeforeCallName) == NULL);
  assert(M.getFunction(DynAAUtils::TxProfAfterCallName) == NULL);
  assert(M.getFunction(DynAAUtils::TxProfEnterName) == NULL);

  assert(M.getFunction(DynAAUtils::TxHookInitName) == NULL);
  assert(M.getFunction(DynAAUtils::TxHookBeforeCallName) == NULL);
  assert(M.getFunction(DynAAUtils::TxHookAfterCallName) == NULL);
  assert(M.getFunction(DynAAUtils::TxHookEnterName) == NULL);

  vector<Type *> ArgTypes;

  ///////////////////////////////////////////////////////////////////////////////////////////
  // TxProf

  // Setup TxProfInit
  FunctionType *TxProfInitType = FunctionType::get(VoidType, false);
  TxProfInit = Function::Create(TxProfInitType,
                                GlobalValue::ExternalLinkage,
                                DynAAUtils::TxProfInitName,
                                &M);

  // Setup TxProfBeforeCall
  ArgTypes.clear();
  ArgTypes.push_back(IntType);
  FunctionType *TxProfBeforeCallType = FunctionType::get(VoidType, ArgTypes, false);
  TxProfBeforeCall = Function::Create(TxProfBeforeCallType,
                                      GlobalValue::ExternalLinkage,
                                      DynAAUtils::TxProfBeforeCallName,
                                      &M);

  // Setup TxProfAfterCall
  ArgTypes.clear();
  ArgTypes.push_back(IntType);
  FunctionType *TxProfAfterCallType = FunctionType::get(VoidType, ArgTypes, false);
  TxProfAfterCall = Function::Create(TxProfAfterCallType,
                                     GlobalValue::ExternalLinkage,
                                     DynAAUtils::TxProfAfterCallName,
                                     &M);

  // Setup TxProfEnter
  FunctionType *TxProfEnterType = FunctionType::get(VoidType, IntType, false);
  TxProfEnter = Function::Create(TxProfEnterType,
                                 GlobalValue::ExternalLinkage,
                                 DynAAUtils::TxProfEnterName,
                                 &M);

  // Setup TxProfExit
  FunctionType *TxProfExitType = FunctionType::get(VoidType, IntType, false);
  TxProfExit = Function::Create(TxProfExitType,
                                GlobalValue::ExternalLinkage,
                                DynAAUtils::TxProfExitName,
                                &M);

  ///////////////////////////////////////////////////////////////////////////////////////////
  // TxHook
  // Setup TxHookInit
  FunctionType *TxHookInitType = FunctionType::get(VoidType, false);
  TxHookInit = Function::Create(TxHookInitType,
                                GlobalValue::ExternalLinkage,
                                DynAAUtils::TxHookInitName,
                                &M);

  // Setup TxHookBeforeCall
  FunctionType *TxHookBeforeCallType = FunctionType::get(VoidType, false);
  TxHookBeforeCall = Function::Create(TxHookBeforeCallType,
                                      GlobalValue::ExternalLinkage,
                                      DynAAUtils::TxHookBeforeCallName,
                                      &M);

  // Setup TxHookAfterCall
  FunctionType *TxHookAfterCallType = FunctionType::get(VoidType, false);
  TxHookAfterCall = Function::Create(TxHookAfterCallType,
                                     GlobalValue::ExternalLinkage,
                                     DynAAUtils::TxHookAfterCallName,
                                     &M);

  // Setup TxHookEnter
  FunctionType *TxHookEnterType = FunctionType::get(VoidType, IntType, false);
  TxHookEnter = Function::Create(TxHookEnterType,
                                 GlobalValue::ExternalLinkage,
                                 DynAAUtils::TxHookEnterName,
                                 &M);

  // Setup TxHookEnter_st
  FunctionType *TxHookEnter_st_Type = FunctionType::get(VoidType, IntType, false);
  TxHookEnter_st = Function::Create(TxHookEnter_st_Type,
                                    GlobalValue::ExternalLinkage,
                                    DynAAUtils::TxHookEnter_st_Name,
                                    &M);

  // Setup TxHookExit
  FunctionType *TxHookExitType = FunctionType::get(VoidType, IntType, false);
  TxHookExit = Function::Create(TxHookExitType,
                                GlobalValue::ExternalLinkage,
                                DynAAUtils::TxHookExitName,
                                &M);

  /*
   * raw txend and txbegin
   */
  FunctionType *TxHookTxEndType = FunctionType::get(VoidType, /* No ArgTypes,*/ false);
  TxHookTxEnd = Function::Create(TxHookTxEndType,
                                 GlobalValue::ExternalLinkage,
                                 DynAAUtils::TxHookTxEndName,
                                 &M);
  FunctionType *TxHookTxBeginType = FunctionType::get(VoidType, /* No ArgTypes,*/ false);
  TxHookTxBegin = Function::Create(TxHookTxBeginType,
                                   GlobalValue::ExternalLinkage,
                                   DynAAUtils::TxHookTxBeginName,
                                   &M);

  /*
   * force switch to fast/slow path
   */
  FunctionType *TxHookSwitchToSlowPathType = FunctionType::get(VoidType, /* No ArgTypes,*/ false);
  TxHookSwitchToSlowPath = Function::Create(TxHookSwitchToSlowPathType,
                           GlobalValue::ExternalLinkage,
                           DynAAUtils::TxHookSwitchToSlowPathName,
                           &M);

  FunctionType *TxHookSwitchToFastPathType = FunctionType::get(VoidType, /* No ArgTypes,*/ false);
  TxHookSwitchToFastPath = Function::Create(TxHookSwitchToFastPathType,
                           GlobalValue::ExternalLinkage,
                           DynAAUtils::TxHookSwitchToFastPathName,
                           &M);
  /*
   * Tx Cut Loop
   */
  IRBuilder<> IRB(M.getContext());
  Tx_cut_loop = checkInterfaceFunction(
                  M.getOrInsertFunction("__tx_cut_loop",
                                        VoidType,
                                        IntType,
                                        IntType,
                                        nullptr));

}

void TxInstrumenter::setupScalarTypes(Module &M) {
  VoidType = Type::getVoidTy(M.getContext());
  CharType = Type::getInt8Ty(M.getContext());
  CharStarType = PointerType::getUnqual(CharType);
  LongType = Type::getIntNTy(M.getContext(), __WORDSIZE);
  IntType = Type::getInt32Ty(M.getContext());
  Int1Type = Type::getInt1Ty(M.getContext());
}


void TxInstrumenter::processLog(const std::string &LogFileName) {
  assert(LogFileName.size() && "Didn't specify the log file.");

  //open file
  FILE *LogFile = fopen(LogFileName.c_str(), "r");
  assert(LogFile && "The log file doesn't exist.");
  errs().changeColor(raw_ostream::BLUE);
  errs() << "Processing log " << LogFileName << " ...\n";
  errs().resetColor();

  //parse and process
  char line[256] = {0,};
  while (fgets( line, sizeof(line) - 1, LogFile) != NULL)
  {
    int id = 0;
    if (sscanf(line, "X:%d", &id) == 1) {
      Blacklist_FuncID.insert(id);
      //errs() << "Blacklist_FuncID=" << id << "\n";
    }
    else if (sscanf(line, "E:%d", &id) == 1) {
      Entry_FuncID.insert(id);
      //errs() << "Entry_FuncID=" << id << "\n";
    }
    else if (sscanf(line, "ES:%d", &id) == 1) {
      Entry_st_FuncID.insert(id);
    }
    else if (sscanf(line, "S:%d", &id) == 1) {
      Libsys_InsID.insert(id);
      //errs() << "Libsys_InsID=" << id << "\n";
    } else if (sscanf(line, "OA:%d", &id) == 1) {
      Useless_a_InsID.insert(id);
    } else if (sscanf(line, "OB:%d", &id) == 1) {
      Useless_b_InsID.insert(id);
    } else if (sscanf(line, "UA:%d", &id) == 1) {
      Useful_a_InsID.insert(id);
    } else if (sscanf(line, "UB:%d", &id) == 1) {
      Useful_b_InsID.insert(id);
    }
    else {
      errs() << "[ERROR] could not parse: " << line << "\n";
      assert(false && "Could not parse!");
    }
  }
  // close file
  fclose(LogFile);
}

void TxInstrumenter::processMustHook(const std::string &FileName) {

  //open file
  FILE *File = fopen(FileName.c_str(), "r");
  if (File) {

    errs().changeColor(raw_ostream::BLUE);
    errs() << "Processing MustHook " << FileName << " ...\n";
    errs().resetColor();

    //parse and process
    char line[256] = {0,};
    while (fgets( line, sizeof(line) - 1, File) != NULL)
    {
      if (line[0] != '#') {
        string funcname(line, strlen(line) - 1); // truncate '\n'
        MustHook_FuncName.insert(funcname);
        //errs() << "MustHook=" << funcname << "\n";
      }
    }
    // close file
    fclose(File);
  }
}

void TxInstrumenter::processLoopCutInfoFile(const std::string &filepath)
{
  FILE *fp = fopen(filepath.c_str(), "r");
  if (fp)
  {
    errs().changeColor(raw_ostream::YELLOW);
    errs() << "Processing Loop Threshold Info File:" << filepath << "\n";
    errs().resetColor();
    int insid;
    int loopthrd;
    while (fscanf(fp, "%d %d\n", &insid, &loopthrd) != EOF)
    {
      LoopThreshold[insid] = loopthrd;
    }
    hasLoopCutInfoFile = true;
    fclose(fp);
  }
}

/*
 * Recursively duplicate function and modify callsite
 */
void TxInstrumenter::duplicateFunction(Function* F)
{
  /*
   * first, search to see whether this function is already cloned(preserved),
   * if not preserved, clone this function
   */
  if (F->isDeclaration())
    return;
  if (F->size() == 0)
  {
    return;
  }
  if (F->getLinkage() == LLVMAvailableExternallyLinkage)
  {
    return;
  }

  NumOTFuncDuplicated++;

  string clonedFuncName = F->getName();
  clonedFuncName = clonedFuncName + _DUPLICATED_FUNCTION_SUFFIX_;
  if (clonePreserved_FuncName.find(clonedFuncName) == clonePreserved_FuncName.end())
  {
    ValueToValueMapTy VMap;
    Function* preservedFunc = CloneFunction(F, VMap, false);
    preservedFunc->setName(clonedFuncName);
    F->getParent()->getFunctionList().push_back(preservedFunc);

    /*
     * Save Function to work list
     */
    cloneSrcFunctions.insert(F);
    cloneTgtFunctions.insert(preservedFunc);
    //////

    errs() << "clone function " << F->getName() << " into " << clonedFuncName << " \n";
    //preservedFunc->setLinkage(GlobalValue::InternalLinkage);

    clonePreserved_FuncName.insert(clonedFuncName);

    /*
     * recursively check called functions, and clone and replace
     */
    errs() << "Examine cloned function\n";
    for (Function::iterator BB = preservedFunc->begin(); BB != preservedFunc->end(); ++BB)
    {

      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      {
        IDAssigner &IDA = getAnalysis<IDAssigner>();
        unsigned FuncID = IDA.getFunctionID(F);
        if (FuncID == IDAssigner::InvalidID)
        {
          continue;
        }
        string CalleeName;
        Function* Callee;
        /*
         * then duplicate the function and set callee to be newly duplicated function
         */
        CallSite CS(I);
        if (CS)
        {
          if ((Callee = CS.getCalledFunction()) != NULL)
          {
            /*
            * skip llvm instrict
            */

            if ((CalleeName = Callee->getName()) == "llvm.dbg.declare")
            {
              continue;
            }

            if (Callee->isDeclaration())
              continue;
            if (Callee->size() == 0)
            {
              continue;
            }


          } else
          {
            continue;
          }
        } else
        {
          continue;
        }


        /*
         * only replace OT functions
         */
        std::map<string, TRThreadState>::iterator  xit = functionThreadStateTable.find(CalleeName);
        //function as caller
        if (xit == functionThreadStateTable.end())
        {
          //function is not profiled
          continue;
        }
        xit = functionAttributeTable.find(CalleeName);
        if (xit != functionAttributeTable.end())
        {
          if ( xit->second != OT)
          {
            continue;
          }
        } else
        {
          continue;
        }
        duplicateFunction(Callee);
        string duplicatedFunctionName = CalleeName + _DUPLICATED_FUNCTION_SUFFIX_;
        CS.setCalledFunction(F->getParent()->getFunction(duplicatedFunctionName));
        errs() << "         in function" << F->getName() << " replace callee " << duplicatedFunctionName << "\n";
      }
    }
  }
}

/*
 * scan all call site in function F, if there is OT function call
 * then duplicate that OT function and replace that callsite
 * and examine cloned OT function recursively
 */
void TxInstrumenter::processSTFunction(Function *F)
{
  errs() << "  Examine ST function " << F->getName() << " for OT\n";
  for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
  {
    for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
    {
      CallSite CS(I);
      if (!CS)
        continue;
      Function *callee = CS.getCalledFunction();
      if (!callee)
        continue;
      if (callee->isDeclaration())
        continue;
      if (callee->size() == 0)
        continue;
      std::map<string, TRThreadState>::iterator xit = functionAttributeTable.find(callee->getName());
      if (xit == functionAttributeTable.end())
      {
        continue;
      }
      errs() << "       callee in ST = " << callee->getName() << " is " << xit->second << "\n";
      /*
       * no way a ST function call a MT function
       * other ST callee will take care of themself
       */
      if (xit->second != OT)
        continue;
      duplicateFunction(callee);
      string duplicatedFunctionName = callee->getName();
      duplicatedFunctionName += string(_DUPLICATED_FUNCTION_SUFFIX_);
      CS.setCalledFunction(F->getParent()->getFunction(duplicatedFunctionName));
      errs() << "     in ST Function " << F->getName() << " replace callee " << duplicatedFunctionName << "\n";
    }
  }
}

bool TxInstrumenter::runOnModule(Module & M)
{
  // Setup scalar types.
  setupScalarTypes(M);

  // Find the main function.
  Main = M.getFunction("main");
  assert(Main && !Main->isDeclaration() && !Main->hasLocalLinkage());


  // Parse Log File
  if (!isProfile) {
    processLog(ProfileLogFileName);
    if (MustHookFileName != "") processMustHook(MustHookFileName);
  }

  // Setup hook function declarations.
  setupHooks(M);

  // TSAN
  if (isTSan) __TSAN_doInitialization(M);

  if (!isProfile)
  {
    /*
     * construct function attribute table
     */
    ConstructFunctionAttributeTable(M);
    // Hook and tsan instrument
    for (Module::iterator F = M.begin(); F != M.end(); ++F)
    {
      if (F->isDeclaration())
        continue;

      // Blacklist
      IDAssigner &IDA = getAnalysis<IDAssigner>();
      unsigned FuncID = IDA.getFunctionID(F);
      if (FuncID == IDAssigner::InvalidID)
      {
        continue;
      }
      if (Blacklist_FuncID.find(FuncID) != Blacklist_FuncID.end())
      {
        errs().changeColor(raw_ostream::RED);
        errs() << "DON'T INSTRUMENT " << F->getName() << "\n";
        errs().resetColor();
        continue;
      }

      //Don't instrument cloned preserved function
      if (clonePreserved_FuncName.find(F->getName()) != clonePreserved_FuncName.end())
      {
        ///////////////////////////////////////////////////////
        //clone preserved function is guranteed to be tsan free?
        if (isTSan)
        {
          tsanFreeFunctions.insert(F);
        }
        ///////////////////////////////////////////////////////
        continue;
      }

      //Don't touch must hook function
      if (MustHook_FuncName.find(F->getName()) != MustHook_FuncName.end())
      {
        continue;
      }


      /*
       * if this function is proved to be only used in single thread context,
       * don't instrument
       */

      TRThreadState funcTS = MT;
      TRThreadState funcATS = MT;

      std::map<string, TRThreadState>::iterator xit = functionThreadStateTable.find(F->getName());

      //function as caller
      if (xit != functionThreadStateTable.end())
      {
        funcTS = xit->second;
      }

      //function as callee
      xit = functionAttributeTable.find(F->getName());
      if (xit != functionAttributeTable.end())
      {
        funcATS = xit->second;
      }

      if ((funcTS == ST) && (funcATS == ST))
      {
        errs() << "skip ST function:" << F->getName() << "\n";
        NumSTFuncSkipped++;
        /*
         * even this function is ST, it may call OT function
         * in this case we need to fix callsite
         * ----
         * iterate through this ST function and recursively find out all call site
         * which will invoke OT functions
         * and clone that OT function and replace corresponding Callsite
         */
        processSTFunction(F);
        ///////////////////////////////////////////////////////
        //ST function is guranteed to be tsan free?
        if (isTSan)
        {
          tsanFreeFunctions.insert(F);
        }
        ///////////////////////////////////////////////////////
        continue;
      } else if (funcATS == OT)
      {
        /*
         * and if this function is defined, then
         * clone this function into new uninstrumented function before instrumenting it
         * the newly cloned function has suffix "_TxPreservedOrigFunction"
         */
        duplicateFunction(F);
      }

      //errs() << "Instrument function:" << F->getName() << "\n";

      // Exit, BeforeCall, AfterCall
      for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
      {
#if 1
        /*
         * Consult bbTailState to see whether this BasicBlock need instrument
         * FIXME: for BST basic block with predecessor state BMT,
         *         we need to add txend to the beginning this BST block
         *         otherwise it will abort
         */
        string bbStoreName = F->getName();
        bbStoreName += "_";
        bbStoreName += BB->getName();
        TRBBState finalSCCState = bbTailState[bbStoreName];
        switch (finalSCCState)
        {
        case (BMT):
          break;
        case (BST):
          /*
           * this BB need extra txend at the beginning
           */
          if (bbTailMiscState[bbStoreName])
          {
            BasicBlock::iterator I;
            I = BB->getFirstInsertionPt();
            CallInst::Create(TxHookTxEnd, "", I);
            llvm::DebugLoc::DebugLoc dbgloc = SDLoc(I, 1).getDebugLoc();
            errs() << "ST After MT, "
                   << bbStoreName
                   << " Create txend call at "
                   << DIScope(dbgloc.getScope(I->getContext())).getFilename()
                   << ":"
                   << dbgloc.getLine()
                   << "\n";
          }
          continue;
          break;
        case (BUT):
          //no possible
          continue;
          break;
        }
#endif

        for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
        {
          unsigned int iiar = instrumentInstructionIfNecessary(I);
        }
      }
      // Entry
      instrumentEntry(*F);
      /*
       * TSAN memory ops
       * in this section, remove
       */
      if (isTSan)
      {
        if (F->isIntrinsic())
        {
          errs() << "Skip tsan on intrinsic:" << F->getName() << "\n";
          continue;
        }
        if (__TSAN_runOnFunction(*F))
        {
          tsanTouchedFunctions.insert(F);
        } else
        {
          tsanFreeFunctions.insert(F);
        }
        //errs() << " instrumented?\n" << __TSAN_runOnFunction(*F) << "\n";
        /*
         * For each basic block, remove duplicate TxHookXXX,
         * if there are no __wrap_tsan_xxx between TxHookBefore and TxHookAfter
         */
        for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
        {
          removeTxFromTsanFreeRegion(BB);
        }
        /*
         * For each basic block, if there the tx region is too short
         * then there is no need to bother transaction memory
         * just force slow path
         */
#if USE_DONOT_BOTHER_TX_IF_IT_IS_SHORT
        for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
        {
          while (doNotBotherTxIfItIsShort(BB));
        }
#endif
      }
    }
#ifdef USE_REMOVE_USELESS_TX
    CalculateTsanFreeFunctionsAndFixMTSCC(M);
#endif
  } else //TxProf
  {
    errs() << "This is TxProf\n";
    // Hook
    for (Module::iterator F = M.begin(); F != M.end(); ++F)
    {
      if (F->isDeclaration())
        continue;

      for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
      {
        for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
        {
          unsigned int iiar = instrumentInstructionIfNecessary(I);
        }
      }
      // Entry
      instrumentEntry(*F);
    }
  }
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  /*
   * Replace OT Function call in ST region to cloned preserved Function
   * TODO: also instrument ST basic blocks, for now, only instrument ST function
   */
  if (!isProfile)
  {
    errs() << "=========OT Function Treatment=======" << "\n";
    for (Module::iterator F = M.begin(); F != M.end(); ++F)
    {
      if (F->isDeclaration())
        continue;

      if (clonePreserved_FuncName.find(F->getName()) != clonePreserved_FuncName.end())
      {
        //skip clone preserved functions
        continue;
      }

      std::map<string, TRThreadState>::iterator xit = functionThreadStateTable.find(F->getName());

      //function as caller
      if (xit == functionThreadStateTable.end())
      {
        //function is not profiled
        continue;
      }

      xit = functionAttributeTable.find(F->getName());
      if (xit == functionAttributeTable.end())
      {
        continue;
      }
      switch (xit->second)
      {
      case (ST):
        //go and replace
        break;
      case (MT):
        //TODO:need to inspect ST region
        continue;
        break;
      case (OT):
        //go and replace
        break;
      }

      string CalleeName;

      for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
      {
        //errs()<<"+"<<F->getName()<<"\n";
        for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
        {
          IDAssigner &IDA = getAnalysis<IDAssigner>();
          unsigned InsID = IDA.getInstructionID(I);
          if (InsID == IDAssigner::InvalidID)
            continue;
          CallSite CS(I);
          if (CS)
          {
            if (CS.getCalledValue() == NULL)
              continue;
            Function* Callee = CS.getCalledFunction();
            if (Callee)
            {
              CalleeName = Callee->getName();
            } else
            {
              continue;
            }
          } else
          {
            continue;
          }
          /*
           * search cloned preserved function name list to see if it is cloned
           * if there exists a cloned version, call the clone preserved version instead of instrumented version
           */
          string speculateFuncName = CalleeName + "_TxPreservedOrigFunction";
          if (clonePreserved_FuncName.find(speculateFuncName) != clonePreserved_FuncName.end())
          {
            /*
             * if current function call context is MT, i.e. useful, then do not replace
             */
            if (Useful_b_InsID.find(InsID) != Useful_b_InsID.end())
            {
              //errs() << "+" << F->getName() << "\n";
              //errs() << "    skip function " << CalleeName << " because current is MT\n";
              continue;
            }
            llvm::DebugLoc::DebugLoc dbgloc = SDLoc(I, 1).getDebugLoc();
            errs() << "+" << F->getName() << "\n";
            errs() << "    replace function " << CalleeName << " with " << speculateFuncName << "  line " << dbgloc.getLine() << "\n";
            CS.setCalledFunction(M.getFunction(speculateFuncName));
          }
        }
      }
    }
    errs() << "=============================" << "\n";
    /*
     * Loop cut
     * insert loop cut function into loop of MT functions
     * if LoopCutInfoFile is specified, fill in threshold using values in file
     */
    if (useLoopCut)
    {
      errs() << "-----------Loop Instrument----------\n";

      processLoopCutInfoFile(LoopCutInfoFile);

      for (Module::iterator F = M.begin(); F != M.end(); ++F)
      {
        if (F->isDeclaration())
          continue;
        if (clonePreserved_FuncName.find(F->getName()) != clonePreserved_FuncName.end())
        {
          //skip clone preserved functions
          continue;
        }
        std::map<string, TRThreadState>::iterator xit = functionThreadStateTable.find(F->getName());
        //function as caller
        if (xit == functionThreadStateTable.end())
        {
          //function is not profiled
          continue;
        }
        switch (xit->second)
        {
        case (ST):
          continue;
        case (MT):
          goto loopanalysis;
          break;//never reach
        default:
          break;
        }
        xit = functionAttributeTable.find(F->getName());
        if (xit == functionAttributeTable.end())
        {
          continue;
        }
        switch (xit->second)
        {
        case (ST):
          continue;
          break;
        case (MT):
          goto loopanalysis;
          break;//never reach
        case (OT):
          goto loopanalysis;
          break;//never reach
        }

loopanalysis:
        /*
         * extract all loops inside this Function
         * and store all depth 1 loops into allLoops
         */
        errs() << "Function: " << F->getName() << "\n";
        LoopInfo& LI = getAnalysis<LoopInfo>(*F);
        IDAssigner &IDA = getAnalysis<IDAssigner>();
        std::vector<Loop*> allLoops;
        for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
        {
          /*
           * only for BMT BB
           */
          string bbStoreName = F->getName();
          bbStoreName += "_";
          bbStoreName += BB->getName();
          if (bbTailState[bbStoreName] != BMT)
          {
            continue;
          }

          Loop *l = LI.getLoopFor(BB);
          if (l != NULL)
          {
            if (l->getLoopDepth() == 1)
            {
              if (std::find(allLoops.begin(), allLoops.end(), l) == allLoops.end())
              {
                //errs() << "Got one loop \n" << *l << "\n";
                allLoops.push_back(l);
              }
            }
          }
        }
        /*
         * create call in header BB of each loop
         */
        for (std::vector<Loop*>::iterator lit = allLoops.begin(); lit != allLoops.end(); ++lit)
        {
          BasicBlock *bb = (*lit)->getHeader();
          //Instruction *insertpoint = bb->getFirstInsertionPt();
          Instruction *insertpoint = bb->getFirstNonPHI();

          llvm::DebugLoc::DebugLoc dbgloc = SDLoc(insertpoint, 1).getDebugLoc();

          unsigned int insid = IDA.getInstructionID(insertpoint);
          if (insid == IDAssigner::InvalidID)
          {
            //why??
            continue;
          }

          int loopthrd = 65535;
          if (LoopThreshold.find(insid)->second)
          {
            loopthrd = LoopThreshold[insid];
          } else
          {
            if (hasLoopCutInfoFile)
            {
              //just skip if current insert point is not listed in file
              errs() << " skip txcut call at : "
                     << F->getName()
                     << " "
                     << DIScope(dbgloc.getScope(insertpoint->getContext())).getFilename()
                     << ":"
                     << dbgloc.getLine()
                     << "\n";
              continue;
            }
          }

          errs() << " Create txcut call at : "
                 << F->getName()
                 << " "
                 << DIScope(dbgloc.getScope(insertpoint->getContext())).getFilename()
                 << ":"
                 << dbgloc.getLine()
                 << "\n";


          Value* Args[] = {
            ConstantInt::get(IntType, insid),
            ConstantInt::get(IntType, loopthrd),
          };
          ArrayRef<Value*> aref(Args);
          CallInst::Create(Tx_cut_loop,
                           aref,
                           "",
                           insertpoint);
          errs() << insid << " " << loopthrd << "\n";
          LoopThreshold[insid] = loopthrd;
        }
      }
#if 1
      /*
       * only for debug
       */
      errs() << "-----ltrd.txt-----\n";
      for (std::map<int, int>::iterator it = LoopThreshold.begin(); it != LoopThreshold.end(); ++it)
        errs() << it->first << " " << it->second << '\n';
      errs() << "---***----\n";
#endif
      errs() << "-----------*----------\n";
    }
  }
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// STATS
  if (isTSan) {
    errs().changeColor(raw_ostream::BLUE);
    errs() << "[TSAN] NumInstrumentedReads: " << NumInstrumentedReads << "\n"
           << "[TSAN] NumInstrumentedWrites: " << NumInstrumentedWrites << "\n"
           << "[TSAN] NumOmittedReadsBeforeWrite: " << NumOmittedReadsBeforeWrite << "\n"
           << "[TSAN] NumEliminatedNotsanTxAnotation: " << NumEliminatedNotsanTxAnotation << "\n"
           << "[TSAN] NumSTFuncSkipped:" << NumSTFuncSkipped << "\n"
           << "[TSAN] NumOTFuncDuplicated:" << NumOTFuncDuplicated << "\n"
           << "[TSAN] NumForcedSlowPath:" << NumForcedSlowPath << "\n";
    errs().resetColor();
  }

  Instruction *OldEntry = NULL;

// Call init at the very beginning
  if (TxHookInitFuncName != "") {
    Function *InitFunc = M.getFunction(TxHookInitFuncName.c_str());
    assert(InitFunc && !InitFunc->isDeclaration()/*&& !InitFunc->hasLocalLinkage()*/);
    OldEntry = InitFunc->begin()->getFirstNonPHI();
  }
  else {
    OldEntry = Main->begin()->getFirstNonPHI();
  }

  if (isProfile) {
    CallInst::Create(TxProfInit, "", OldEntry);
  }
  else {
    CallInst::Create(TxHookInit, "", OldEntry);
  }

  return true;
}

void TxInstrumenter::initializeCallbacks(Module & M) {
  IRBuilder<> IRB(M.getContext());
  // Initialize the callbacks.
  TsanFuncEntry = checkInterfaceFunction(M.getOrInsertFunction(
      "__wrap_tsan_func_entry", IRB.getVoidTy(), IRB.getInt8PtrTy(), NULL));
  TsanFuncExit = checkInterfaceFunction(M.getOrInsertFunction(
                                          "__wrap_tsan_func_exit", IRB.getVoidTy(), NULL));
  OrdTy = IRB.getInt32Ty();
  for (size_t i = 0; i < kNumberOfAccessSizes; ++i) {
    const size_t ByteSize = 1 << i;
    const size_t BitSize = ByteSize * 8;
    SmallString<32> ReadName("__wrap_tsan_read" + itostr(ByteSize));
    TsanRead[i] = checkInterfaceFunction(M.getOrInsertFunction(
                                           ReadName, IRB.getVoidTy(), IRB.getInt8PtrTy(), NULL));

    SmallString<32> WriteName("__wrap_tsan_write" + itostr(ByteSize));
    TsanWrite[i] = checkInterfaceFunction(M.getOrInsertFunction(
                                            WriteName, IRB.getVoidTy(), IRB.getInt8PtrTy(), NULL));


    SmallString<64> UnalignedReadName("__wrap_tsan_unaligned_read" +
                                      itostr(ByteSize));
    TsanUnalignedRead[i] =
      checkInterfaceFunction(M.getOrInsertFunction(
                               UnalignedReadName, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));

    SmallString<64> UnalignedWriteName("__wrap_tsan_unaligned_write" +
                                       itostr(ByteSize));
    TsanUnalignedWrite[i] =
      checkInterfaceFunction(M.getOrInsertFunction(
                               UnalignedWriteName, IRB.getVoidTy(), IRB.getInt8PtrTy(), nullptr));



    Type *Ty = Type::getIntNTy(M.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    SmallString<32> AtomicLoadName("__tsan_atomic" + itostr(BitSize) +
                                   "_load");
    TsanAtomicLoad[i] = checkInterfaceFunction(M.getOrInsertFunction(
                          AtomicLoadName, Ty, PtrTy, OrdTy, NULL));

    SmallString<32> AtomicStoreName("__tsan_atomic" + itostr(BitSize) +
                                    "_store");
    TsanAtomicStore[i] = checkInterfaceFunction(M.getOrInsertFunction(
                           AtomicStoreName, IRB.getVoidTy(), PtrTy, Ty, OrdTy,
                           NULL));

    for (int op = AtomicRMWInst::FIRST_BINOP;
         op <= AtomicRMWInst::LAST_BINOP; ++op) {
      TsanAtomicRMW[op][i] = NULL;
      const char *NamePart = NULL;
      if (op == AtomicRMWInst::Xchg)
        NamePart = "_exchange";
      else if (op == AtomicRMWInst::Add)
        NamePart = "_fetch_add";
      else if (op == AtomicRMWInst::Sub)
        NamePart = "_fetch_sub";
      else if (op == AtomicRMWInst::And)
        NamePart = "_fetch_and";
      else if (op == AtomicRMWInst::Or)
        NamePart = "_fetch_or";
      else if (op == AtomicRMWInst::Xor)
        NamePart = "_fetch_xor";
      else if (op == AtomicRMWInst::Nand)
        NamePart = "_fetch_nand";
      else
        continue;
      SmallString<32> RMWName("__tsan_atomic" + itostr(BitSize) + NamePart);
      TsanAtomicRMW[op][i] = checkInterfaceFunction(M.getOrInsertFunction(
                               RMWName, Ty, PtrTy, Ty, OrdTy, NULL));
    }

    SmallString<32> AtomicCASName("__tsan_atomic" + itostr(BitSize) +
                                  "_compare_exchange_val");
    TsanAtomicCAS[i] = checkInterfaceFunction(M.getOrInsertFunction(
                         AtomicCASName, Ty, PtrTy, Ty, Ty, OrdTy, OrdTy, NULL));
  }
  TsanVptrUpdate = checkInterfaceFunction(M.getOrInsertFunction(
      "__wrap_tsan_vptr_update", IRB.getVoidTy(), IRB.getInt8PtrTy(),
      IRB.getInt8PtrTy(), NULL));
  TsanVptrLoad = checkInterfaceFunction(M.getOrInsertFunction(
                                          "__wrap_tsan_vptr_read", IRB.getVoidTy(), IRB.getInt8PtrTy(), NULL));
  TsanAtomicThreadFence = checkInterfaceFunction(M.getOrInsertFunction(
                            "__tsan_atomic_thread_fence", IRB.getVoidTy(), OrdTy, NULL));
  TsanAtomicSignalFence = checkInterfaceFunction(M.getOrInsertFunction(
                            "__tsan_atomic_signal_fence", IRB.getVoidTy(), OrdTy, NULL));

  MemmoveFn = checkInterfaceFunction(M.getOrInsertFunction(
                                       "memmove", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                                       IRB.getInt8PtrTy(), IntptrTy, NULL));
  MemcpyFn = checkInterfaceFunction(M.getOrInsertFunction(
                                      "memcpy", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                                      IntptrTy, NULL));
  MemsetFn = checkInterfaceFunction(M.getOrInsertFunction(
                                      "memset", IRB.getInt8PtrTy(), IRB.getInt8PtrTy(), IRB.getInt32Ty(),
                                      IntptrTy, NULL));
}

bool TxInstrumenter::__TSAN_doInitialization(Module & M) {
  const DataLayout * DL = getAnalysisIfAvailable<DataLayout>();
  if (!DL)
    return false;
  BL.reset(SpecialCaseList::createOrDie(BlacklistFile));

  // Always insert a call to __tsan_init into the module's CTORs.
  IRBuilder<> IRB(M.getContext());
  IntptrTy = IRB.getIntPtrTy(DL);
  Value *TsanInit = M.getOrInsertFunction("__wrap_tsan_init",
                                          IRB.getVoidTy(), NULL);
  appendToGlobalCtors(M, cast<Function>(TsanInit), 0);

  return true;
}

static bool isVtableAccess(Instruction * I) {
  if (MDNode *Tag = I->getMetadata(LLVMContext::MD_tbaa))
    return Tag->isTBAAVtableAccess();
  return false;
}

bool TxInstrumenter::addrPointsToConstantData(Value * Addr) {
  // If this is a GEP, just analyze its pointer operand.
  if (GetElementPtrInst *GEP = dyn_cast<GetElementPtrInst>(Addr))
    Addr = GEP->getPointerOperand();

  if (GlobalVariable *GV = dyn_cast<GlobalVariable>(Addr)) {
    if (GV->isConstant()) {
      // Reads from constant globals can not race with any writes.
      NumOmittedReadsFromConstantGlobals++;
      return true;
    }
  } else if (LoadInst *L = dyn_cast<LoadInst>(Addr)) {
    if (isVtableAccess(L)) {
      // Reads from a vtable pointer can not race with any writes.
      NumOmittedReadsFromVtable++;
      return true;
    }
  }
  return false;
}

// Instrumenting some of the accesses may be proven redundant.
// Currently handled:
//  - read-before-write (within same BB, no calls between)
//  - not captured variables
//
// We do not handle some of the patterns that should not survive
// after the classic compiler optimizations.
// E.g. two reads from the same temp should be eliminated by CSE,
// two writes should be eliminated by DSE, etc.
//
// 'Local' is a vector of insns within the same BB (no calls between).
// 'All' is a vector of insns that will be instrumented.
void TxInstrumenter::chooseInstructionsToInstrument(
  SmallVectorImpl<Instruction*> &Local,
  SmallVectorImpl<Instruction*> &All,
  const DataLayout & DL) {
  SmallSet<Value*, 8> WriteTargets;
  // Iterate from the end.
  for (SmallVectorImpl<Instruction*>::reverse_iterator It = Local.rbegin(),
       E = Local.rend(); It != E; ++It) {
    Instruction *I = *It;
    if (StoreInst *Store = dyn_cast<StoreInst>(I)) {
      WriteTargets.insert(Store->getPointerOperand());
    } else {
      LoadInst *Load = cast<LoadInst>(I);
      Value *Addr = Load->getPointerOperand();
      if (WriteTargets.count(Addr)) {
        // We will write to this temp, so no reason to analyze the read.
        NumOmittedReadsBeforeWrite++;
        continue;
      }
      if (addrPointsToConstantData(Addr)) {
        // Addr points to some constant data -- it can not race with any writes.
        continue;
      }
    }

    Value *Addr = isa<StoreInst>(*I)
                  ? cast<StoreInst>(I)->getPointerOperand()
                  : cast<LoadInst>(I)->getPointerOperand();
    if (isa<AllocaInst>(GetUnderlyingObject(Addr, &DL)) &&
        !PointerMayBeCaptured(Addr, true, true)) {
      // The variable is addressable but not captured, so it cannot be
      // referenced from a different thread and participate in a data race
      // (see llvm/Analysis/CaptureTracking.h for details).
      continue;
    }

    All.push_back(I);
  }
  Local.clear();
}

static bool isAtomic(Instruction * I) {
  if (LoadInst *LI = dyn_cast<LoadInst>(I))
    return LI->isAtomic() && LI->getSynchScope() == CrossThread;
  if (StoreInst *SI = dyn_cast<StoreInst>(I))
    return SI->isAtomic() && SI->getSynchScope() == CrossThread;
  if (isa<AtomicRMWInst>(I))
    return true;
  if (isa<AtomicCmpXchgInst>(I))
    return true;
  if (isa<FenceInst>(I))
    return true;
  return false;
}

/*
 * no need to instrument shared variable for single thread region
 */
bool TxInstrumenter::__TSAN_runOnFunction(Function & F)
{
  if (BL->isIn(F)) return false;
  initializeCallbacks(*F.getParent());
  SmallVector<Instruction*, 8> RetVec;
  SmallVector<Instruction*, 8> AllLoadsAndStores;
  SmallVector<Instruction*, 8> LocalLoadsAndStores;
  SmallVector<Instruction*, 8> AtomicAccesses;
  SmallVector<Instruction*, 8> MemIntrinCalls;
  bool Res = false;
  bool HasCalls = false;

  const DataLayout DL = DataLayout(F.getParent()->getDataLayout());

  /*
   * is bb MT or ST?
   */
  bool ismt;

  errs() << "[TSAN] runOnFunction: " << F.getName() << "\n";

  // Traverse all instructions, collect loads/stores/returns, check for calls.
  for (Function::iterator FI = F.begin(), FE = F.end();
       FI != FE;
       ++FI)
  {
    BasicBlock &BB = *FI;

#if 1
    /*
     * Consult bbTailState to see whether this BasicBlock need instrument
     */
    string bbStoreName = F.getName();
    bbStoreName += "_";
    bbStoreName += BB.getName();
    TRBBState finalSCCState = bbTailState[bbStoreName];
    if (finalSCCState != BMT)
    {
      //errs() << "Skip tsan on " << bbStoreName << "\n";
      continue;
    }
#endif

#if 1
    ///////////////////////////////////////////////////////////////////////////////
    /*
     * default multi thread flag is set to true,
     * scan whole BB till we find the first useless/useful TxHookBefore
     */
    ismt = true;
    for (BasicBlock::iterator I = BB.begin(); I != BB.end(); ++I)
    {
      IDAssigner &IDA = getAnalysis<IDAssigner>();
      unsigned InsID = IDA.getInstructionID(I);
      if (InsID == IDAssigner::InvalidID)
        continue;
      if (!CallSite(I))
        continue;
      //errs() << "    exanmine insID " << InsID << " for func head\n";
      /*
      * beginning of this basic block is single thread
      */
      if (Useless_b_InsID.find(InsID) != Useless_b_InsID.end())
      {
        if (Useful_b_InsID.find(InsID) != Useful_b_InsID.end())
        {
          //OT
          ismt = true;
          break;
        }
        ismt = false;
        break;
      }
    }
    //errs() << "    Begin as MT? " << ismt << "\n";
    //errs() <<"=========B=======\n"<<BB<<"=============oo=================\n";
    ///////////////////////////////////////////////////////////////////////////////
#endif
    for (BasicBlock::iterator BI = BB.begin(), BE = BB.end(); BI != BE; ++BI)
    {
#if 1
      IDAssigner &IDA = getAnalysis<IDAssigner>();
      unsigned InsID = IDA.getInstructionID(BI);
      if (InsID == IDAssigner::InvalidID)
        continue;
      if (CallSite(BI))
      {
        ismt = (Useful_a_InsID.find(InsID) != Useful_a_InsID.end());
        //errs() << "    Hit MT?" << ismt << "\n";
      }
      /*
       * only instrument multi thread region
       */
      if (ismt)
      {
#endif
        if (isAtomic(BI))
          AtomicAccesses.push_back(BI);
        else if (isa<LoadInst>(BI) || isa<StoreInst>(BI))
          LocalLoadsAndStores.push_back(BI);
        else if (isa<ReturnInst>(BI))
          RetVec.push_back(BI);
        else if (isa<CallInst>(BI) || isa<InvokeInst>(BI)) {
          if (isa<MemIntrinsic>(BI))
            MemIntrinCalls.push_back(BI);
          HasCalls = true;
          chooseInstructionsToInstrument(LocalLoadsAndStores, AllLoadsAndStores, DL);
        }
#if 1
      }
#endif
    }
    chooseInstructionsToInstrument(LocalLoadsAndStores, AllLoadsAndStores, DL);
  }

  //errs() << "[TSAN]   Atomic:" << AtomicAccesses.size()
  //       << ", Load/Store:" << AllLoadsAndStores.size()
  //       << ", Return:" << RetVec.size()
  //       << ", MemIntrin:" << MemIntrinCalls.size()
  //       << "\n";

  // We have collected all loads and stores.
  // FIXME: many of these accesses do not need to be checked for races
  // (e.g. variables that do not escape, etc).

  // Instrument memory accesses.
  if (ClInstrumentMemoryAccesses /*&& F.hasFnAttribute(Attribute::SanitizeThread)*/)
    for (size_t i = 0, n = AllLoadsAndStores.size(); i < n; ++i) {
      Res |= instrumentLoadOrStore(AllLoadsAndStores[i], DL);
    }

  // Instrument atomic memory accesses.
  if (ClInstrumentAtomics)
    for (size_t i = 0, n = AtomicAccesses.size(); i < n; ++i) {
      Res |= instrumentAtomic(AtomicAccesses[i], DL);
    }

  if (ClInstrumentMemIntrinsics)
    for (size_t i = 0, n = MemIntrinCalls.size(); i < n; ++i) {
      Res |= instrumentMemIntrinsic(MemIntrinCalls[i]);
    }

  // TODO:REMOVE ME
  // Instrument function entry/exit points if there were instrumented accesses.
  if ((Res || HasCalls) && ClInstrumentFuncEntryExit)
  {
    IRBuilder<> IRB(F.getEntryBlock().getFirstNonPHI());
    Value *ReturnAddress = IRB.CreateCall(
                             Intrinsic::getDeclaration(F.getParent(), Intrinsic::returnaddress),
                             IRB.getInt32(0));
    IRB.CreateCall(TsanFuncEntry, ReturnAddress);
    for (size_t i = 0, n = RetVec.size(); i < n; ++i) {
      IRBuilder<> IRBRet(RetVec[i]);
      IRBRet.CreateCall(TsanFuncExit);
    }
    Res = true;
  }
  /////////////////////////////////////////////////////////////////////////////////////////////////
  return Res;
}

bool TxInstrumenter::instrumentLoadOrStore(Instruction * I, const DataLayout & DL) {
  IRBuilder<> IRB(I);
  bool IsWrite = isa<StoreInst>(*I);
  Value *Addr = IsWrite
                ? cast<StoreInst>(I)->getPointerOperand()
                : cast<LoadInst>(I)->getPointerOperand();
  int Idx = getMemoryAccessFuncIndex(Addr, DL);
  if (Idx < 0)
    return false;
  if (IsWrite && isVtableAccess(I)) {
    DEBUG(dbgs() << "  VPTR : " << *I << "\n");
    Value *StoredValue = cast<StoreInst>(I)->getValueOperand();
    // StoredValue does not necessary have a pointer type.
    if (isa<IntegerType>(StoredValue->getType()))
      StoredValue = IRB.CreateIntToPtr(StoredValue, IRB.getInt8PtrTy());
    // Call TsanVptrUpdate.
    IRB.CreateCall2(TsanVptrUpdate,
                    IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()),
                    IRB.CreatePointerCast(StoredValue, IRB.getInt8PtrTy()));
    NumInstrumentedVtableWrites++;
    return true;
  }
  if (!IsWrite && isVtableAccess(I)) {
    IRB.CreateCall(TsanVptrLoad,
                   IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
    NumInstrumentedVtableReads++;
    return true;
  }
  /*
  Value *OnAccessFunc = IsWrite ? TsanWrite[Idx] : TsanRead[Idx];
  IRB.CreateCall(OnAccessFunc, IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));
  */

  /* DY */
  //IDAssigner &IDA = getAnalysis<IDAssigner>();
  //unsigned InsID = IDA.getInstructionID(I);
  Value* OnAccessFunc = nullptr;

  const unsigned Alignment = IsWrite
                             ? cast<StoreInst>(I)->getAlignment()
                             : cast<LoadInst>(I)->getAlignment();
  Type *OrigTy = cast<PointerType>(Addr->getType())->getElementType();
  const uint32_t TypeSize = DL.getTypeStoreSizeInBits(OrigTy);

  if (Alignment == 0 || Alignment >= 8 || (Alignment % (TypeSize / 8)) == 0)
    OnAccessFunc = IsWrite ? TsanWrite[Idx] : TsanRead[Idx];
  else
    OnAccessFunc = IsWrite ? TsanUnalignedWrite[Idx] : TsanUnalignedRead[Idx];

  IRB.CreateCall(OnAccessFunc, IRB.CreatePointerCast(Addr, IRB.getInt8PtrTy()));

  if (IsWrite) NumInstrumentedWrites++;
  else         NumInstrumentedReads++;
  return true;

}

static ConstantInt *createOrdering(IRBuilder<> *IRB, AtomicOrdering ord) {
  uint32_t v = 0;
  switch (ord) {
  case NotAtomic:              assert(false);
  case Unordered:              // Fall-through.
  case Monotonic:              v = 0; break;
  // case Consume:                v = 1; break;  // Not specified yet.
  case Acquire:                v = 2; break;
  case Release:                v = 3; break;
  case AcquireRelease:         v = 4; break;
  case SequentiallyConsistent: v = 5; break;
  }
  return IRB->getInt32(v);
}

static ConstantInt *createFailOrdering(IRBuilder<> *IRB, AtomicOrdering ord) {
  uint32_t v = 0;
  switch (ord) {
  case NotAtomic:              assert(false);
  case Unordered:              // Fall-through.
  case Monotonic:              v = 0; break;
  // case Consume:                v = 1; break;  // Not specified yet.
  case Acquire:                v = 2; break;
  case Release:                v = 0; break;
  case AcquireRelease:         v = 2; break;
  case SequentiallyConsistent: v = 5; break;
  }
  return IRB->getInt32(v);
}

// If a memset intrinsic gets inlined by the code gen, we will miss races on it.
// So, we either need to ensure the intrinsic is not inlined, or instrument it.
// We do not instrument memset/memmove/memcpy intrinsics (too complicated),
// instead we simply replace them with regular function calls, which are then
// intercepted by the run-time.
// Since tsan is running after everyone else, the calls should not be
// replaced back with intrinsics. If that becomes wrong at some point,
// we will need to call e.g. __tsan_memset to avoid the intrinsics.
bool TxInstrumenter::instrumentMemIntrinsic(Instruction * I) {
  IRBuilder<> IRB(I);
  if (MemSetInst *M = dyn_cast<MemSetInst>(I)) {
    IRB.CreateCall3(MemsetFn,
                    IRB.CreatePointerCast(M->getArgOperand(0), IRB.getInt8PtrTy()),
                    IRB.CreateIntCast(M->getArgOperand(1), IRB.getInt32Ty(), false),
                    IRB.CreateIntCast(M->getArgOperand(2), IntptrTy, false));
    I->eraseFromParent();
  } else if (MemTransferInst *M = dyn_cast<MemTransferInst>(I)) {
    IRB.CreateCall3(isa<MemCpyInst>(M) ? MemcpyFn : MemmoveFn,
                    IRB.CreatePointerCast(M->getArgOperand(0), IRB.getInt8PtrTy()),
                    IRB.CreatePointerCast(M->getArgOperand(1), IRB.getInt8PtrTy()),
                    IRB.CreateIntCast(M->getArgOperand(2), IntptrTy, false));
    I->eraseFromParent();
  }
  return false;
}

// Both llvm and TxInstrumenter atomic operations are based on C++11/C1x
// standards.  For background see C++11 standard.  A slightly older, publically
// available draft of the standard (not entirely up-to-date, but close enough
// for casual browsing) is available here:
// http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2011/n3242.pdf
// The following page contains more background information:
// http://www.hpl.hp.com/personal/Hans_Boehm/c++mm/

bool TxInstrumenter::instrumentAtomic(Instruction * I, const DataLayout & DL) {
  IRBuilder<> IRB(I);
  if (LoadInst *LI = dyn_cast<LoadInst>(I)) {
    Value *Addr = LI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const size_t ByteSize = 1 << Idx;
    const size_t BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     createOrdering(&IRB, LI->getOrdering())
                    };
    CallInst *C = CallInst::Create(TsanAtomicLoad[Idx],
                                   ArrayRef<Value*>(Args));
    ReplaceInstWithInst(I, C);

  } else if (StoreInst *SI = dyn_cast<StoreInst>(I)) {
    Value *Addr = SI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const size_t ByteSize = 1 << Idx;
    const size_t BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     IRB.CreateIntCast(SI->getValueOperand(), Ty, false),
                     createOrdering(&IRB, SI->getOrdering())
                    };
    CallInst *C = CallInst::Create(TsanAtomicStore[Idx],
                                   ArrayRef<Value*>(Args));
    ReplaceInstWithInst(I, C);
  } else if (AtomicRMWInst *RMWI = dyn_cast<AtomicRMWInst>(I)) {
    Value *Addr = RMWI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    Function *F = TsanAtomicRMW[RMWI->getOperation()][Idx];
    if (F == NULL)
      return false;
    const size_t ByteSize = 1 << Idx;
    const size_t BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     IRB.CreateIntCast(RMWI->getValOperand(), Ty, false),
                     createOrdering(&IRB, RMWI->getOrdering())
                    };
    CallInst *C = CallInst::Create(F, ArrayRef<Value*>(Args));
    ReplaceInstWithInst(I, C);
  } else if (AtomicCmpXchgInst *CASI = dyn_cast<AtomicCmpXchgInst>(I)) {
    Value *Addr = CASI->getPointerOperand();
    int Idx = getMemoryAccessFuncIndex(Addr, DL);
    if (Idx < 0)
      return false;
    const size_t ByteSize = 1 << Idx;
    const size_t BitSize = ByteSize * 8;
    Type *Ty = Type::getIntNTy(IRB.getContext(), BitSize);
    Type *PtrTy = Ty->getPointerTo();
    Value *Args[] = {IRB.CreatePointerCast(Addr, PtrTy),
                     IRB.CreateIntCast(CASI->getCompareOperand(), Ty, false),
                     IRB.CreateIntCast(CASI->getNewValOperand(), Ty, false),
                     createOrdering(&IRB, CASI->getOrdering()),
                     createFailOrdering(&IRB, CASI->getOrdering())
                    };
    CallInst *C = CallInst::Create(TsanAtomicCAS[Idx], ArrayRef<Value*>(Args));
    ReplaceInstWithInst(I, C);
  } else if (FenceInst *FI = dyn_cast<FenceInst>(I)) {
    Value *Args[] = {createOrdering(&IRB, FI->getOrdering())};
    Function *F = FI->getSynchScope() == SingleThread ?
                  TsanAtomicSignalFence : TsanAtomicThreadFence;
    CallInst *C = CallInst::Create(F, ArrayRef<Value*>(Args));
    ReplaceInstWithInst(I, C);
  }
  return true;
}

int TxInstrumenter::getMemoryAccessFuncIndex(Value * Addr, const DataLayout & DL) {
  Type *OrigPtrTy = Addr->getType();
  Type *OrigTy = cast<PointerType>(OrigPtrTy)->getElementType();
  assert(OrigTy->isSized());
  uint32_t TypeSize = DL.getTypeStoreSizeInBits(OrigTy);
  if (TypeSize != 8  && TypeSize != 16 &&
      TypeSize != 32 && TypeSize != 64 && TypeSize != 128) {
    NumAccessesWithBadSize++;
    // Ignore all unusual sizes.
    return -1;
  }
  size_t Idx = countTrailingZeros(TypeSize / 8);
  assert(Idx < kNumberOfAccessSizes);
  return Idx;
}
////////////////////////////////////

/*
 * Helper function to get function thread state
 */
TxInstrumenter::TRThreadState TxInstrumenter::getTRThreadStateForFunction(string functionName)
{

  TRThreadState funcTS = MT;
  TRThreadState funcATS = MT;

  std::map<string, TRThreadState>::iterator xit = functionThreadStateTable.find(functionName);

  //function as caller
  if (xit != functionThreadStateTable.end())
  {
    funcTS = xit->second;
  }

  //function as callee
  xit = functionAttributeTable.find(functionName);
  if (xit != functionAttributeTable.end())
  {
    funcATS = xit->second;
  }

  if ((funcTS == ST) && (funcATS == ST))
  {
    return ST;
  }
  return MT;
}



/*
 * Iterate through each instruction and extract function call info
 * i.e. profiled info.
 * Once ST and MT function is calculated,
 * calculate ST region in MT function using SCC(most cases, main)
 */

void TxInstrumenter::ConstructFunctionAttributeTable(Module & M)
{
  IDAssigner &IDA = getAnalysis<IDAssigner>();

  /*
   * add main function as Single Thread by default,
   * however this can be changed if we can prove main is called multithread
   */

  functionAttributeTable["main"] = ST;

  /*
   * add main function as function which will spawn new thread by default
   */
  functionThreadStateTable["main"] = MT;

  for (Module::iterator F = M.begin(); F != M.end(); ++F)
  {
    if (F->getName() != "main")
      functionThreadStateTable[F->getName()] = ST;

    if (F->isDeclaration())
    {
      continue;
    }
    if ((F->size() == 0) ||
        (F->isIntrinsic()) ||
        (F->getLinkage() == LLVMAvailableExternallyLinkage))
    {
      continue;
    }
    //errs() << "Analyse " << F->getName() << "\n";

    for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
    {

      StringRef CalleeName = "";
      /*
       * lastBBstate is used to record the end state of basicblock
       */
      TRBBState lastBBstate = BUT;

      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      {
        /////////////////////////////////////////////////////
        unsigned InsID = IDA.getInstructionID(I);
        if (InsID == IDAssigner::InvalidID)
        {
          continue;
        }
        CallSite CS(I);
        ////////////////////////////////////////////////////
        //get callee name
        Function* Callee;
        if (CS)
        {
          Callee = CS.getCalledFunction();
          if (Callee)
          {
            CalleeName = Callee->getName();
            if (CalleeName == "")
            {
              continue;
            }
          } else
          {
            continue;
          }
        } else
        {
          continue;
        }
        if (!(Callee->isIntrinsic()
              || (Callee->size() == 0)
              || (Callee->getLinkage() == LLVMAvailableExternallyLinkage)))
        {
          /*
           * State in functionAttributeTable is either started with ST or MT, then take transition to OT
           */
          TRThreadState ts = MT;// default MT
          if (Useless_b_InsID.find(InsID) != Useless_b_InsID.end())
          {
            /*
             * found TxHookBefore is useless,
             * means that before calling this function, it is executing in single thread mode
             */
            ts = ST;
            if (Useful_b_InsID.find(InsID) != Useful_b_InsID.end())
            {
              ts = OT;
            }
          } else if (Useful_b_InsID.find(InsID) == Useful_b_InsID.end())
          {
            /*
             * also not found in Useful list, then mark it as ST
             */
            //if (Callee->isIntrinsic() || (Callee->getLinkage()==LLVMAvailableExternallyLinkage))
            //{

            //}else
            //{
            ts = ST;
            //}
          }

          std::map<string, TRThreadState>::iterator it = functionAttributeTable.find(CalleeName);
          /*
           * not found, add new entry
           */
          if ((it == functionAttributeTable.end()) || (ts == OT))
          {
            functionAttributeTable[CalleeName] = ts;
          } else if (it->second != ts)
          {
            functionAttributeTable[CalleeName] = OT;
          }
          /*errs() << "             Callee "
          << CalleeName
          << " ID "
          << InsID
          << " is "
          << functionAttributeTable[CalleeName]
          << "\n";*/
        }
        /*
         * If found any instruction runs in multithread mode,
         * then treat this function as a thread which will respawn new thread
         */
        if (Useful_b_InsID.find(InsID) != Useful_b_InsID.end())
        {
          //errs() << "    InsID : " << InsID << " profiled MT\n";
          functionThreadStateTable[F->getName()] = MT;
        } else if (Useful_a_InsID.find(InsID) != Useful_a_InsID.end())
        {
          //errs() << "    InsID : " << InsID << " profiled MT\n";
          functionThreadStateTable[F->getName()] = MT;
        }
        /*
         * maintain lastBBstate
         */
        if (Useful_a_InsID.find(InsID) != Useful_a_InsID.end())
        {
          lastBBstate = BMT;
        } else if (Useless_a_InsID.find(InsID) != Useless_a_InsID.end())
        {
          lastBBstate = BST;
        }
      }
      string bbStoreName = F->getName();
      bbStoreName += "_";
      bbStoreName += BB->getName();

      bbTailState[bbStoreName] = lastBBstate;
      //errs() << " x: " << bbStoreName << " as " << lastBBstate << "\n";
    }
    //errs() << " : " << functionThreadStateTable[F->getName()] << "\n";
  }
#if 1
  /*
   * debug purpose
   */
  errs() << "--* function as callee, is it called when mt? *--\n";

  for (std::map<string, TRThreadState>::iterator it = functionAttributeTable.begin();
       it != functionAttributeTable.end();
       ++it)
  {
    errs() << it->first << " => " << it->second << "\n";
  }
  errs() << "-----------------------\n";
  errs() << "--* function as caller, will it respawn new thread? *--\n";
  for (std::map<string, TRThreadState>::iterator it = functionThreadStateTable.begin();
       it != functionThreadStateTable.end();
       ++it)
  {
    errs() << it->first << " => " << it->second << "\n";
  }
  errs() << "-----------------------\n";
#endif

  /*
   * Get SCC for each MT function and calculate MT region coverage
   */

  for (Module::iterator F = M.begin(); F != M.end(); ++F)
  {
    if (F->isDeclaration())
    {
      continue;
    }
    std::map<string, TRThreadState>::iterator xit = functionThreadStateTable.find(F->getName());
    if (xit == functionThreadStateTable.end())
      continue;
    if (xit->second == ST)
      continue;
    ///////////////////////////////////////////////////////////////////////
    /*
     * get SCC analysis for MT Function
     */

    std::map<string, std::list<std::string>*> sccres;
    std::list<std::list<std::string>*> sccrawres;

    Function &FF = *F;
    unsigned sccNum = 0;
    //errs() << "SCCs for Function " << FF.getName() << " in PostOrder:";
    for (scc_iterator<Function*> SCCI = scc_begin(&FF),
         E = scc_end(&FF); SCCI != E; ++SCCI)
    {
      std::vector<BasicBlock*> &nextSCC = *SCCI;
      //errs() << "\n    SCC #" << ++sccNum << " : ";

      std::list<std::string>* bblist = new std::list<std::string>;

      for (std::vector<BasicBlock*>::const_iterator I = nextSCC.begin(),
           E = nextSCC.end(); I != E; ++I)
      {
        //errs() << (*I)->getName() << ", ";
        bblist->push_back((*I)->getName());
      }
      if (nextSCC.size() == 1 && SCCI.hasLoop())
      {
        //errs() << " (Has self-loop).";
      }

      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        sccres[*it] = bblist;
      }
      sccrawres.push_back(bblist);
    }

#if 0
    /*
     * debug purpose
     */
    errs() << "SCCs for Function " << FF.getName();
    sccNum = 0;
    for (std::list<std::list<std::string>*>::iterator ibb = sccrawres.begin(); ibb != sccrawres.end(); ++ibb)
    {
      errs() << "\n    SCC #" << ++sccNum << " : ";
      std::list<std::string>* bblist = *ibb;
      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        errs() << *it << ", ";
      }
    }
    errs() << "\n";
#endif
    /*
     * calculate MT coverage using SCC and profiled info
     */
    /*
     * Pass 1, examine each SCC,
     * if there exists instructions which indicate MT, then mark all BBs in this SCC MT
     * if there exists only instructions which indicate (ST and UT) or (ST), then mark all BBs in this SCC ST
     * if there exists only instructions which indicate UT, then mark all BBs in this SCC UT
     * see TRSCCMachine
     */
    //errs() << "Pass 1.\nSCCs for Function " << FF.getName();
    sccNum = 0;
    for (std::list<std::list<std::string>*>::iterator ibb = sccrawres.begin(); ibb != sccrawres.end(); ++ibb)
    {
      //errs() << "\n    SCC #" << ++sccNum << " : ";
      std::list<std::string>* bblist = *ibb;
      TRBBState finalSCCState = BUT;
      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        string bbStoreName = FF.getName();
        bbStoreName += "_";
        bbStoreName += *it;
        finalSCCState = TRSCCMachine[finalSCCState][bbTailState[bbStoreName]];
      }
      //errs() << " TRBS : " << finalSCCState << " . ";
      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        string bbStoreName = FF.getName();
        bbStoreName += "_";
        bbStoreName += *it;
        bbTailState[bbStoreName] = finalSCCState;
        //errs() << *it << ", ";
      }
    }
    //errs() << "\n";
    /*
     * Pass 2, examine all SCC and fix [BUT] SCC vertex
     */
    //errs() << "Pass 2.\n Calculate MT Coverage\n";
    //////////////////////////////
    //function attribute
    std::map<string, TRThreadState>::iterator it = functionAttributeTable.find(F->getName());
    TRThreadState currentFuncAttribute;
    /*
     * not found, add new entry
     */
    if (it == functionAttributeTable.end())
    {
      currentFuncAttribute = MT;
    } else
    {
      currentFuncAttribute = it->second;
    }
    ///////////////////////////////
    for (Function::iterator BB = F->begin(); BB != F->end(); ++BB)
    {
      const TerminatorInst *TInst = BB->getTerminator();
      //get current bbstate
      string bbStoreName = F->getName();
      bbStoreName += "_";
      bbStoreName += BB->getName();
      TRBBState currentBBState = bbTailState[bbStoreName];
      if (currentBBState == BUT)
      {

        if (currentFuncAttribute == ST)
        {
          currentBBState = BST;
        } else
        {
          currentBBState = BMT;
        }
      }
      //errs()<<"  "<<BB->getName()<<"\n   `->";
      for (unsigned I = 0, NSucc = TInst->getNumSuccessors(); I < NSucc; ++I)
      {
        //TODO: should recursively find next scc state, if we encounter BUT vertex in Succ
        BasicBlock *Succ = TInst->getSuccessor(I);
        // Do stuff with Succ
        //errs() << "  " << Succ->getName();
        string succBBStoreName = F->getName();
        succBBStoreName += "_";
        succBBStoreName += Succ->getName();
        switch (bbTailState[succBBStoreName])
        {
        case (BUT):
          bbTailState[succBBStoreName] = currentBBState;
          break;
        case (BST):
          if (currentBBState == BMT)
          {
            //need to add txend to current basic block
            bbTailMiscState[succBBStoreName] = true;
          }
          break;
        case (BMT):
          break;
        }
      }
      //errs() << "\n";
    }
#if 0
    /*
     * Debug purpose
     */
    errs() << "Fixed SCC State for " << FF.getName() << ":";
    sccNum = 0;
    for (std::list<std::list<std::string>*>::iterator ibb = sccrawres.begin(); ibb != sccrawres.end(); ++ibb)
    {
      errs() << "\n    SCC #" << ++sccNum << " : ";
      std::list<std::string>* bblist = *ibb;
      TRBBState finalSCCState;
      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        string bbStoreName = FF.getName();
        bbStoreName += "_";
        bbStoreName += *it;
        finalSCCState = bbTailState[bbStoreName];
        break;
      }
      errs() << " TRBS : " << finalSCCState << " . ";
      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {
        errs() << *it << ", ";
      }
    }
    errs() << "\n------\n";
#endif
    /*
     * save scc result for further use
     */
    FuncSCCres[FF.getName()] = sccres;
    FuncSCCRawres[FF.getName()] = sccrawres;
    ///////////////////////////////////////////////////////////////////////////
  }
}

/*
 * Verify Tsan Free Functions and false tsan free functions
 * and Idenfity MT SCC calling these tsan free functions and must hook functions
 * if MTSCC is tsanfree
 *     exclude it from TX from TX
 * if MTSCC is nottsanfree but calling some big tsan free function,
 *     exclude that tsan free function from TX
 */
void TxInstrumenter::CalculateTsanFreeFunctionsAndFixMTSCC(Module &M)
{
  /*
   * if the function is not touched by tsan,
   * 1. calculate whether this function calls other functions, and examine it's callee functions recursively
   * 2. for each function that is not touched by tsan:
   *      2.1: remove all TxHooks from it
   *      2.2: if that function is duplicated, remove duplicated functions and restore callsite to original one
   *      2.3: if that function is used in MT SCC region, then we need to analyse the max region to add txend txbegin to
   */
  if (!isTSan)
  {
    return;
  }

  while (tsanFreeFunctions.size() != 0)
  {
    //1.
    for (std::set<Function*>::iterator fit = tsanFreeFunctions.begin();
         fit != tsanFreeFunctions.end();
         ++fit)
    {
      Function *canF = *fit;
      std::vector<Function*> calledFunctions;

      for (Function::iterator BB = canF->begin(); BB != canF->end(); ++BB)
      {
        for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
        {
          IDAssigner &IDA = getAnalysis<IDAssigner>();
          unsigned InsID = IDA.getInstructionID(I);
          if (InsID == IDAssigner::InvalidID)
            continue;
          CallSite CS(I);
          if (CS)
          {
            Function* Callee = CS.getCalledFunction();
            if (Callee == NULL)
              continue;

            if (Callee->size() == 0)
            {
              continue;
            }
            calledFunctions.push_back(Callee);
          }
        }
      }
      //This function is not calling other functions
      if (calledFunctions.size() == 0)
      {
        verifiedTsanFreeFunctions.insert(canF);
        continue;
      }
      //////////////////////////////////////////////////////////////////
      //If every callee is verified notsan, then it is tsan free
      bool tsanfree = true; //assume it is tsan free
      for (std::vector<Function*>::iterator ic = calledFunctions.begin();
           ic != calledFunctions.end();
           ++ic)
      {
        Function* cf = *ic;
        bool found = false;
        //is it verified?
        for (std::set<Function*>::iterator vfit = verifiedTsanFreeFunctions.begin();
             vfit != verifiedTsanFreeFunctions.end();
             ++vfit)
        {
          Function * srcf = *vfit;
          if (srcf == cf)
          {
            found = true;
            break;
          }
        }
        //let's verify next callee in this function
        if (found)
        {
          continue;
        }
        //not verified yet, but in tsanFreeFunctions?
        bool foundUnverifiedYet = false;
        for (std::set<Function*>::iterator ffit = tsanFreeFunctions.begin();
             ffit != tsanFreeFunctions.end();
             ++ffit)
        {
          Function *FF = *ffit;
          if (FF == cf)
          {
            foundUnverifiedYet = true;
            break;
          }
        }
        //not found in tsanFreeFunctions also, must has tsan
        if (!foundUnverifiedYet)
        {
          tsanfree = false;
          //add to falsetsanfree
          falseTsanFreeFunctions.insert(canF);
          break;
        }
      }
      if (tsanfree)
      {
        verifiedTsanFreeFunctions.insert(canF);
      }
      //////////////////////////////////////////////////////////////////
    }
    //function is verified tsan free
    for (std::set<Function*>::iterator vfit = verifiedTsanFreeFunctions.begin();
         vfit != verifiedTsanFreeFunctions.end();
         ++vfit)
    {
      Function *tf = *vfit;
      tsanFreeFunctions.erase(tf);
      tsanTouchedFunctions.insert(tf);
    }
    //functions is verified not tsan free
    for (std::set<Function*>::iterator vfit = falseTsanFreeFunctions.begin();
         vfit != falseTsanFreeFunctions.end();
         ++vfit)
    {
      Function *tf = *vfit;
      tsanFreeFunctions.erase(tf);
      tsanTouchedFunctions.insert(tf);
    }
  }
  /*
   * Debug purpose: verified tsan free functions
   */

  errs() << "--------DEBUG TSAN FREE FUNCTIONS---------------\n";
  errs() << "-----Verified tsan free------\n";
  //function is verified tsan free
  for (std::set<Function*>::iterator vfit = verifiedTsanFreeFunctions.begin();
       vfit != verifiedTsanFreeFunctions.end();
       ++vfit)
  {
    Function *tf = *vfit;
    errs() << tf->getName() << "\n";
  }
  errs() << "-----Verified false tsan free------\n";
  //functions is verified not tsan free
  for (std::set<Function*>::iterator vfit = falseTsanFreeFunctions.begin();
       vfit != falseTsanFreeFunctions.end();
       ++vfit)
  {
    Function *tf = *vfit;
    errs() << tf->getName() << "\n";
  }
  errs() << "------------------------------------------------------\n";
#if 0
  //2.1
  for (std::set<Function*>::iterator vfit = verifiedTsanFreeFunctions.begin();
       vfit != verifiedTsanFreeFunctions.end();
       ++vfit)
  {
    Function *tf = *vfit;
    //errs() << "in Function :" << tf->getName() << "\n";
    vector<Instruction*> ilist;
    for (Function::iterator BB = tf->begin(); BB != tf->end(); ++BB)
    {
      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
      {
        CallSite CS(I);
        if (CS)
        {
          Function* Callee = CS.getCalledFunction();
          if (Callee == NULL)
            continue;

          if (Callee->getName().endswith(_DUPLICATED_FUNCTION_SUFFIX_))
          {
            //errs() << "   Got " << Callee->getName() << "\n";
          } else if (Callee->getName().startswith("TxHookBefore") ||
                     Callee->getName().startswith("TxHookAfter"))
          {
            //errs() << "   Got " << Callee->getName() << "\n";
            ilist.push_back(I);
          }
        }
      }
    }

    for (unsigned i = 0; i < ilist.size(); i++)
    {
      ilist[i]->eraseFromParent();
    }
    ilist.clear();
  }
#endif
  /*
   * 2.3
   * examine all usage of verified tsan free functions/ and musthook functions
   * if in MT SCC region, need to find out max tsan free region to add txhookbefore and txhookafter
   */
  /*std::vector<Function*> mustHookFunctions;
  for (std::set<string>::iterator mhit = MustHook_FuncName.begin();
       mhit != MustHook_FuncName.end();
       mhit++)
  {
    mustHookFunctions.push_back(M.getFunction(*mhit));
  }*/
  /*
   * first calculate tsan free scc for MT/OT function
   */
  //iterate through functions
  for (map<string, std::map<string, std::list<std::string>*>>::iterator fit = FuncSCCres.begin();
       fit != FuncSCCres.end();
       ++fit)
  {
    string functionName = fit->first;
    std::map<string, std::list<std::string>*> sccres = fit->second;
    std::list<std::list<std::string>*> sccrawres = FuncSCCRawres[fit->first];
    Function *F = M.getFunction(functionName);
    //errs() << "Function : " << functionName << "\n";
    if (MustHook_FuncName.find(functionName) != MustHook_FuncName.end())
    {
      //errs() << "  is to be hooked.\n\n";
      continue;
    }
    if (verifiedTsanFreeFunctions.find(F) != verifiedTsanFreeFunctions.end())
    {
      continue;
    }
    //int scccount = 0;
    //iterate through sccs of this function(functionName)
    for (std::list<std::list<std::string>*>::iterator ibb = sccrawres.begin(); ibb != sccrawres.end(); ++ibb)
    {
      //iterate through basicblocks of this scc
      std::list<std::string>* bblist = *ibb;
      bool isThisSCCTsanFree = true;//assume it is tsan free till we found evidence to show it is not
      bool isThisSCCHasHook = false;
      //errs() << " SCC #" << scccount++ << " : \n";
      //examine whether this function is to be hooked or not.
      //only do analysis for MT SCC

      string bbStoreName = functionName;
      bbStoreName += "_";
      bbStoreName += *(bblist->begin());
      switch (bbTailState[bbStoreName])
      {
      case (BMT):
        break;
      case (BST):
        //errs()<<" is BST. skip\n";
        continue;
        break;
      case (BUT):
        //no possible
        continue;
        break;
      }


      for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
      {

        //errs() << "   [ " << *it << " ] ";
        BasicBlock *BB = getBasicBlockByName(F, *it);
#if 0
        llvm::DebugLoc::DebugLoc dbgloc = SDLoc(BB->begin(), 1).getDebugLoc();
        errs() << DIScope(dbgloc.getScope(BB->begin()->getContext())).getFilename()
               << ":"
               << dbgloc.getLine() << "\n";
#endif
        //iterate through instructions of this basicblock
        bool isThisBBTsanFree = true;//assume it is tsan free till we found evidence to show it is not
        for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I)
        {
          CallSite CS(I);
          if (CS)
          {
            Function *Callee = CS.getCalledFunction();
            if (Callee)
            {
              StringRef CalleeName = Callee->getName();
              //check callee for tsan
              if (CalleeName.startswith("__wrap_tsan"))
              {
                isThisSCCTsanFree = false;
                isThisBBTsanFree = false;
              }
              //check callee for cloned
              else if (CalleeName.endswith(_DUPLICATED_FUNCTION_SUFFIX_))
              {
                //isThisSCCHasHook = true;
              }
              //check callee for ST
              //else if ()
              //{
              //  isThisSCCHasHook = true;
              //}
              else if (callSiteCanBeHooked(CS))
              {
                isThisSCCHasHook = true;
              }
              //check if callee is verified tsan free
              else if (verifiedTsanFreeFunctions.find(Callee) != verifiedTsanFreeFunctions.end())
              {
              }
              //otherwise, this is not tsan free
              else
              {
                isThisSCCTsanFree = false;
                isThisBBTsanFree = false;
              }
            }
          }
        }
        //errs() << "    BBTSANFREE? " << isThisBBTsanFree << ", \n";
      }
      if (isThisSCCTsanFree)
      {
        #if 0
        for (std::list<std::string>::iterator it = bblist->begin(); it != bblist->end(); ++it)
        {
          errs() << "   [ " << *it << " ] ";
          BasicBlock *BB = getBasicBlockByName(F, *it);

          llvm::DebugLoc::DebugLoc dbgloc = SDLoc(BB->begin(), 1).getDebugLoc();
          errs() << DIScope(dbgloc.getScope(BB->begin()->getContext())).getFilename()
                 << ":"
                 << dbgloc.getLine() << "\n";
        }
        errs() << "       is SCCTsanFree?" << isThisSCCTsanFree << " hashook? " << isThisSCCHasHook << "\n\n";
        #endif
      }
    }
  }
}

