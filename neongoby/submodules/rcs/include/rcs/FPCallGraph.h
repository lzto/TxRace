// Fit into the CallGraph interface, so that we can run graph algorithms
// (e.g. SCC) on it.

#ifndef __RCS_FP_CALLGRAPH_H
#define __RCS_FP_CALLGRAPH_H

#include <vector>

#include "llvm/IR/Module.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/ADT/DenseMap.h"

#include "rcs/typedefs.h"

using namespace llvm;

namespace rcs {
struct FPCallGraph: public ModulePass {
  typedef DenseMap<const Instruction *, FuncList> SiteToFuncsMapTy;
  typedef DenseMap<const Function *, InstList> FuncToSitesMapTy;

private:
  template <typename T>
  static void MakeUnique(std::vector<T> &V);

  void processCallSite(const CallSite &CS, const FuncSet &AllFuncs);
  void simplifyCallGraph();

  SiteToFuncsMapTy SiteToFuncs;
  FuncToSitesMapTy FuncToSites;

  // Copied from CallGraph
  Module* M;
  typedef std::map<const Function *, CallGraphNode *> FunctionMapTy;
  FunctionMapTy FunctionMap;
  CallGraphNode *Root;
  CallGraphNode *ExternalCallingNode;
  CallGraphNode *CallsExternalNode;

public:
  // Copied from CallGraph
  typedef FunctionMapTy::iterator iterator;
  typedef FunctionMapTy::const_iterator const_iterator;
  Module &getModule() const { return *M; }
  inline iterator begin() { return FunctionMap.begin(); }
  inline iterator end() { return FunctionMap.end(); }
  inline const_iterator begin() const { return FunctionMap.begin(); }
  inline const_iterator end() const { return FunctionMap.end(); }

  inline const CallGraphNode *operator[](const Function *F) const {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second;
  }
  inline CallGraphNode *operator[](const Function *F) {
    const_iterator I = FunctionMap.find(F);
    assert(I != FunctionMap.end() && "Function not in callgraph!");
    return I->second;
  }
  Function *removeFunctionFromModule(CallGraphNode *CGN);
  CallGraphNode *getOrInsertFunction(const Function *F);

  static char ID;

  // Interfaces of ModulePass
  FPCallGraph();
  ~FPCallGraph();
  virtual void getAnalysisUsage(AnalysisUsage &AU) const;
  virtual bool runOnModule(Module &M);
  virtual void print(raw_ostream &O, const Module *M) const;
  // This method is used when a pass implements
  // an analysis interface through multiple inheritance.  If needed, it
  // should override this to adjust the this pointer as needed for the
  // specified pass info.
  //virtual void *getAdjustedAnalysisPointer(AnalysisID PI);

  // Interfaces of CallGraph
  const CallGraphNode *getRoot() const { return Root; }
  CallGraphNode *getRoot() { return Root; }
  // SCC algorithm starts from this external calling node.
  CallGraphNode *getExternalCallingNode() const {
    return ExternalCallingNode;
  }
  CallGraphNode *getCallsExternalNode() const { return CallsExternalNode; }

  FuncList getCalledFunctions(const Instruction *Ins) const;
  InstList getCallSites(const Function *F) const;

 protected:
  void addCallEdge(const CallSite &CS, Function *Callee);
};
}

namespace llvm
{
//===----------------------------------------------------------------------===//
// GraphTraits specializations for FPCallGraph so that they can be treated as
// graphs by the generic graph algorithms.
//

// Provide graph traits for tranversing call graphs using standard graph
// traversals.
template <>
struct GraphTraits<rcs::FPCallGraph *> : public GraphTraits<CallGraphNode *> {
  static NodeType *getEntryNode(rcs::FPCallGraph *CGN) {
    return CGN->getExternalCallingNode(); // Start at the external node!
  }
  typedef std::pair<const Function *, CallGraphNode *> PairTy;
  typedef std::pointer_to_unary_function<PairTy, CallGraphNode &> DerefFun;

  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef mapped_iterator<rcs::FPCallGraph::iterator, DerefFun> nodes_iterator;
  static nodes_iterator nodes_begin(rcs::FPCallGraph *CG) {
    return map_iterator(CG->begin(), DerefFun(CGdereference));
  }
  static nodes_iterator nodes_end(rcs::FPCallGraph *CG) {
    return map_iterator(CG->end(), DerefFun(CGdereference));
  }

  static CallGraphNode &CGdereference(PairTy P) { return *P.second; }
};

template <>
struct GraphTraits<const rcs::FPCallGraph *> : public GraphTraits<
                                            const CallGraphNode *> {
  static NodeType *getEntryNode(const rcs::FPCallGraph *CGN) {
    return CGN->getExternalCallingNode();
  }
  // nodes_iterator/begin/end - Allow iteration over all nodes in the graph
  typedef rcs::FPCallGraph::const_iterator nodes_iterator;
  static nodes_iterator nodes_begin(const rcs::FPCallGraph *CG) { return CG->begin(); }
  static nodes_iterator nodes_end(const rcs::FPCallGraph *CG) { return CG->end(); }
};
}

#endif
