// Summary.h -- bottom-up summaries over a clang::CallGraph (Part 8.6, reused by p08_check in 8.9).
//
// A summary is a per-function fact computed from the summaries of its callees.
// llvm::scc_iterator hands out the strongly connected components callee-first,
// so when a component is processed every function outside it already has its
// final summary. Inside a cyclic component the summaries are iterated to a
// fixed point with Jacobi passes: a pass reads only the values of the previous
// pass, so the number of passes depends on the graph, not on the order in which
// the members happen to be visited.
//
// Two properties (Options::Prop selects one per run):
//   sink   none < may < always        a lattice of height 3
//            may     some chain of calls reaches a sink
//            always  every path through the function's CFG passes a call that is
//                    itself always (or is the sink): no path ENTRY -> EXIT avoids it
//          a sink is a [[noreturn]] function, or the function named by Options::SinkName
//   depth  0 for a leaf, 1 + the deepest callee otherwise; kInf for the members of a
//          cyclic component. The first pass would grow forever, so it is widened:
//          the members jump to kInf and are not iterated.
//
// API
//   p08::Result R = p08::analyze(CG, Ctx, Opt);
//   R.Sccs           one SccRun per component, callee-first, the root's component left out
//   R.of(Node)       the Summary of a node: Sink, Depth, Via
//   p08::sinkPath(R, Node)   the shortest call chain from Node to a sink (empty when none)
//   p08::isSink(Node, Opt)   the predicate behind both
//
// The call graph is the one clang::CallGraph builds: no edge for a function pointer,
// an implicit destructor or `delete`, so a sink behind one of those is invisible here.

#ifndef CFGLAB_P08_SUMMARY_H
#define CFGLAB_P08_SUMMARY_H

#include "clang/AST/ASTContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/CallGraph.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SCCIterator.h"

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace p08 {

using namespace clang;

enum class Prop { Sink, Depth };
enum class SinkLevel : unsigned char { None, May, Always };

// The top of the depth lattice (also the largest unsigned, so `>` and max work on it).
inline constexpr unsigned kInf = ~0u;

struct Options {
  Prop P = Prop::Sink;
  std::string SinkName;     // empty: [[noreturn]] functions; else the function with this qualified name
  bool InfReaches = false;  // depth: a caller of a cyclic component is kInf too (default: members only)
  bool Trace = false;       // keep every pass of every component in SccRun::Passes
};

struct Summary {
  SinkLevel Sink = SinkLevel::None;
  unsigned Depth = 0;
  // sink: first step of the shortest chain to a sink. depth: the first callee that sets the depth.
  const CallGraphNode *Via = nullptr;
  bool sameValue(const Summary &O) const { return Sink == O.Sink && Depth == O.Depth; }
};

struct SccRun {
  unsigned Id = 0;                                   // position in scc_iterator order
  bool Cyclic = false;                               // scc_iterator::hasCycle()
  std::vector<const CallGraphNode *> Members;        // scc_iterator order; sort by name to print
  unsigned Iter = 1;                                 // passes run; the last one changed nothing (1 if acyclic)
  std::vector<std::vector<Summary>> Passes;          // Trace only: Passes[k][i] is Members[i] after pass k + 1
};

struct Result {
  Options Opt;
  std::vector<SccRun> Sccs;
  llvm::DenseMap<const CallGraphNode *, Summary> Fn;
  llvm::DenseMap<const CallGraphNode *, unsigned> SccOf;  // node -> index into Sccs
  const Summary &of(const CallGraphNode *N) const { return Fn.find(N)->second; }
  const SccRun &sccOf(const CallGraphNode *N) const { return Sccs[SccOf.find(N)->second]; }
};

inline const char *levelName(SinkLevel L) {
  return L == SinkLevel::None ? "none" : L == SinkLevel::May ? "may" : "always";
}
inline std::string depthText(unsigned D) { return D == kInf ? "inf" : std::to_string(D); }

// A sink is a function that never returns by declaration ([[noreturn]], `isNoReturn()`)
// or, with SinkName set, the function with that qualified name instead.
inline bool isSink(const CallGraphNode *N, const Options &O) {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(N->getDecl());
  if (!FD) return false;
  return O.SinkName.empty() ? FD->isNoReturn() : FD->getQualifiedNameAsString() == O.SinkName;
}

// The shortest chain of calls from N to a sink: N, ..., the sink. Empty when N cannot reach one.
// Breadth-first over the callees in call-site order, through functions that may reach a sink.
inline std::vector<const CallGraphNode *> sinkPath(const Result &R, const CallGraphNode *N) {
  std::vector<const CallGraphNode *> Path;
  if (R.Fn.find(N) == R.Fn.end() || R.of(N).Sink == SinkLevel::None) return Path;
  llvm::DenseMap<const CallGraphNode *, const CallGraphNode *> Parent;
  std::deque<const CallGraphNode *> Q{N};
  Parent[N] = nullptr;
  while (!Q.empty()) {
    const CallGraphNode *X = Q.front();
    Q.pop_front();
    if (isSink(X, R.Opt)) {
      for (; X; X = Parent[X]) Path.insert(Path.begin(), X);
      return Path;
    }
    for (const CallGraphNode::CallRecord &CR : X->callees())
      if (R.of(CR.Callee).Sink != SinkLevel::None && !Parent.count(CR.Callee)) {
        Parent[CR.Callee] = X;
        Q.push_back(CR.Callee);
      }
  }
  return Path;
}

namespace detail {

// The part of a function's CFG the sink summary needs: which callees (as call graph
// nodes) each block calls, and the reachable successors of each block.
struct Shape {
  unsigned Entry = 0, Exit = 0;
  std::vector<std::vector<const CallGraphNode *>> Calls;  // by block id
  std::vector<std::vector<unsigned>> Succs;               // by block id
};

class Engine {
public:
  Engine(CallGraph &G, ASTContext &C, const Options &O) : CG(G), Ctx(C) { R.Opt = O; }

  Result run() {
    for (auto &KV : CG)
      if (KV.first) R.Fn[KV.second.get()] = Summary();
    unsigned Id = 0;
    for (auto I = llvm::scc_begin(&CG); !I.isAtEnd(); ++I, ++Id) {
      SccRun S;
      S.Id = Id;
      S.Cyclic = I.hasCycle();
      for (CallGraphNode *N : *I)
        if (N != CG.getRoot()) S.Members.push_back(N);
      if (S.Members.empty()) continue;  // the virtual root
      solve(S);
      for (const CallGraphNode *N : S.Members) R.SccOf[N] = R.Sccs.size();
      R.Sccs.push_back(std::move(S));
    }
    if (R.Opt.P == Prop::Sink)
      for (const SccRun &S : R.Sccs)
        for (const CallGraphNode *N : S.Members) R.Fn[N].Via = sinkVia(N);
    return std::move(R);
  }

private:
  CallGraph &CG;
  ASTContext &Ctx;
  Result R;
  llvm::DenseMap<const CallGraphNode *, std::unique_ptr<Shape>> Shapes;

  Summary &val(const CallGraphNode *N) { return R.Fn[N]; }

  void solve(SccRun &S) {
    if (R.Opt.P == Prop::Depth && S.Cyclic) {
      // Widening: depth never settles on a cycle, so jump to the top at once.
      for (const CallGraphNode *N : S.Members) {
        Summary Top;
        Top.Depth = kInf;
        val(N) = Top;
      }
      S.Iter = 1;
      record(S);
      return;
    }
    for (;;) {
      std::vector<Summary> Next;
      Next.reserve(S.Members.size());
      bool Changed = false;
      for (const CallGraphNode *N : S.Members) {
        Summary V = R.Opt.P == Prop::Sink ? sinkTransfer(N) : depthTransfer(N);
        Changed |= !V.sameValue(val(N));
        Next.push_back(V);
      }
      for (size_t K = 0; K < Next.size(); ++K) val(S.Members[K]) = Next[K];
      if (R.Opt.Trace) S.Passes.push_back(std::move(Next));
      if (!S.Cyclic || !Changed) return;
      ++S.Iter;
    }
  }

  void record(SccRun &S) {
    if (!R.Opt.Trace) return;
    std::vector<Summary> Now;
    for (const CallGraphNode *N : S.Members) Now.push_back(val(N));
    S.Passes.push_back(std::move(Now));
  }

  // ---- sink ----------------------------------------------------------------

  Summary sinkTransfer(const CallGraphNode *N) {
    Summary V;
    if (isSink(N, R.Opt)) {
      V.Sink = SinkLevel::Always;
      return V;
    }
    bool May = false;
    for (const CallGraphNode::CallRecord &CR : N->callees())
      May |= val(CR.Callee).Sink != SinkLevel::None;
    if (!May) return V;
    V.Sink = alwaysDies(N) ? SinkLevel::Always : SinkLevel::May;
    return V;
  }

  // "Always": remove every block that calls an always-sink callee and ask whether EXIT
  // is still reachable from ENTRY. A function without a body has no CFG: never always.
  bool alwaysDies(const CallGraphNode *N) {
    const Shape *Sh = shapeOf(N);
    if (!Sh) return false;
    std::vector<bool> Seen(Sh->Calls.size(), false);
    std::vector<unsigned> Work{Sh->Entry};
    Seen[Sh->Entry] = true;
    while (!Work.empty()) {
      unsigned B = Work.back();
      Work.pop_back();
      if (B == Sh->Exit) return false;
      bool Kills = false;
      for (const CallGraphNode *C : Sh->Calls[B]) Kills |= val(C).Sink == SinkLevel::Always;
      if (Kills) continue;
      for (unsigned S : Sh->Succs[B])
        if (!Seen[S]) {
          Seen[S] = true;
          Work.push_back(S);
        }
    }
    return true;
  }

  const Shape *shapeOf(const CallGraphNode *N) {
    auto It = Shapes.find(N);
    if (It != Shapes.end()) return It->second.get();
    std::unique_ptr<Shape> Sh = buildShape(N);
    const Shape *P = Sh.get();
    Shapes[N] = std::move(Sh);
    return P;
  }

  std::unique_ptr<Shape> buildShape(const CallGraphNode *N) {
    // CFG::buildCFG wants the declaration that carries the body (a constructor's
    // initialisers live on the definition, not on the canonical declaration).
    const Decl *D = N->getDecl();
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      const FunctionDecl *Def = nullptr;
      if (!FD->hasBody(Def)) return nullptr;
      D = Def;
    }
    Stmt *Body = D->getBody();
    if (!Body) return nullptr;
    CFG::BuildOptions BO;
    BO.AddInitializers = true; // the calls in a constructor's member initialisers are calls too
    std::unique_ptr<CFG> G = CFG::buildCFG(D, Body, &Ctx, BO);
    if (!G) return nullptr;
    auto Sh = std::make_unique<Shape>();
    Sh->Entry = G->getEntry().getBlockID();
    Sh->Exit = G->getExit().getBlockID();
    Sh->Calls.resize(G->getNumBlockIDs());
    Sh->Succs.resize(G->getNumBlockIDs());
    // The graph's own records: call expression -> callee. A call the graph has no edge for
    // (function pointer, implicit destructor, delete) is invisible to this analysis, as it
    // is to every consumer of the call graph.
    llvm::DenseMap<const Expr *, const CallGraphNode *> Edge;
    for (const CallGraphNode::CallRecord &CR : N->callees())
      if (CR.CallExpr) Edge[CR.CallExpr] = CR.Callee;
    for (const CFGBlock *B : *G) {
      unsigned Id = B->getBlockID();
      for (const CFGElement &E : *B) {
        auto St = E.getAs<CFGStmt>();
        if (!St) continue;
        const auto *Ex = dyn_cast<Expr>(St->getStmt());
        auto It = Ex ? Edge.find(Ex) : Edge.end();
        if (It != Edge.end()) Sh->Calls[Id].push_back(It->second);
      }
      // A pruned edge (if (0), the exit of while (1)) has no reachable block.
      for (const CFGBlock::AdjacentBlock &S : B->succs())
        if (const CFGBlock *T = S.getReachableBlock()) Sh->Succs[Id].push_back(T->getBlockID());
    }
    return Sh;
  }

  const CallGraphNode *sinkVia(const CallGraphNode *N) {
    std::vector<const CallGraphNode *> P = sinkPath(R, N);
    return P.size() > 1 ? P[1] : nullptr;
  }

  // ---- depth ---------------------------------------------------------------

  Summary depthTransfer(const CallGraphNode *N) {
    Summary V;
    bool First = true;
    for (const CallGraphNode::CallRecord &CR : N->callees()) {
      unsigned D = val(CR.Callee).Depth;
      unsigned Via = D == kInf ? (R.Opt.InfReaches ? kInf : 1) : 1 + D;
      if (First || Via > V.Depth) {
        V.Depth = Via;
        V.Via = CR.Callee;
        First = false;
      }
    }
    return V;
  }

};

} // namespace detail

inline Result analyze(CallGraph &CG, ASTContext &Ctx, const Options &Opt) {
  return detail::Engine(CG, Ctx, Opt).run();
}

} // namespace p08

#endif // CFGLAB_P08_SUMMARY_H
