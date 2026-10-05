// p04_solver -- a bit-vector fixpoint solver on Clang's worklists (Part 4.7).
//
//   p04_solver <file> --func=NAME [--problem=live|uninit] [--worklist=NAME|all] [--trace]
//
//   problem live     backward may-analysis: variables live at block entry/exit
//           uninit   forward may-analysis: variables that may be uninitialised at
//                    block entry, then a report of the uses that see one
//   worklist rpo     ForwardDataflowWorklist   (forward problems)
//            po      BackwardDataflowWorklist  (backward problems)
//            wto     WTODataflowWorklist       (either direction; reducible CFGs only)
//            fifo    a plain std::deque seeded in CFG order (ids ascending), for comparison
//            fifo-r  the same, seeded in descending id order
//            lifo    a plain stack, for comparison
//            all     run every strategy that applies and print a table
//   --trace  print the order in which blocks are dequeued

#include "cfglab.h"

#include "clang/Analysis/Analyses/IntervalPartition.h"
#include "clang/Analysis/Analyses/PostOrderCFGView.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "llvm/ADT/BitVector.h"

#include <deque>
#include <map>

using namespace clang;

static llvm::cl::OptionCategory Cat("p04_solver options");
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat), llvm::cl::desc("function"));
static llvm::cl::opt<std::string> ProblemOpt("problem", llvm::cl::init("live"), llvm::cl::cat(Cat), llvm::cl::desc("live|uninit"));
static llvm::cl::opt<std::string> WorklistOpt("worklist", llvm::cl::init("all"), llvm::cl::cat(Cat), llvm::cl::desc("rpo|po|wto|fifo|lifo|all"));
static llvm::cl::opt<bool> Trace("trace", llvm::cl::cat(Cat), llvm::cl::desc("print the dequeue order"));

// ---- def/use events (same extractor as p04_slice) ----
struct Event { bool IsDef; const VarDecl *V; };
static const VarDecl *asVar(const Expr *E) {
  if (auto *R = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts())) return dyn_cast<VarDecl>(R->getDecl());
  return nullptr;
}
static void collect(const Stmt *S, std::vector<Event> &Out) {
  if (!S) return;
  if (auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->isAssignmentOp()) {
    const VarDecl *V = asVar(BO->getLHS());
    if (!V) { collect(BO->getLHS(), Out); collect(BO->getRHS(), Out); return; }
    if (BO->isCompoundAssignmentOp()) Out.push_back({false, V});
    collect(BO->getRHS(), Out);
    Out.push_back({true, V});
    return;
  }
  if (auto *UO = dyn_cast<UnaryOperator>(S); UO && UO->isIncrementDecrementOp())
    if (const VarDecl *V = asVar(UO->getSubExpr())) { Out.push_back({false, V}); Out.push_back({true, V}); return; }
  if (auto *DS = dyn_cast<DeclStmt>(S)) {
    for (const Decl *D : DS->decls())
      if (auto *VD = dyn_cast<VarDecl>(D); VD && VD->getInit()) { collect(VD->getInit(), Out); Out.push_back({true, VD}); }
    return;
  }
  if (auto *R = dyn_cast<DeclRefExpr>(S)) {
    if (auto *V = dyn_cast<VarDecl>(R->getDecl())) Out.push_back({false, V});
    return;
  }
  for (const Stmt *C : S->children()) collect(C, Out);
}

struct Problem {
  const CFG &G;
  ASTContext &Ctx;
  std::vector<const VarDecl *> Vars;                 // bit i <-> Vars[i]
  std::map<const VarDecl *, unsigned> Index;
  std::vector<std::vector<Event>> Events;            // per block id, in execution order
  std::vector<llvm::BitVector> Use, Def;             // per block id
  llvm::BitVector LocalsNoParams;                    // for 'uninit': bit set for non-parameters
  Problem(const CFG &G, ASTContext &C, const FunctionDecl *FD) : G(G), Ctx(C) {
    Events.resize(G.getNumBlockIDs());
    for (const CFGBlock *B : G)
      for (const CFGElement &E : *B)
        if (auto CS = E.getAs<CFGStmt>()) collect(CS->getStmt(), Events[B->getBlockID()]);
    for (auto *P : FD->parameters()) add(P);
    for (auto &Ev : Events) for (const Event &X : Ev) add(X.V);
    unsigned N = Vars.size();
    LocalsNoParams.resize(N);
    for (unsigned I = 0; I < N; ++I) LocalsNoParams[I] = !isa<ParmVarDecl>(Vars[I]);
    Use.assign(G.getNumBlockIDs(), llvm::BitVector(N));
    Def.assign(G.getNumBlockIDs(), llvm::BitVector(N));
    for (const CFGBlock *B : G) {
      unsigned Id = B->getBlockID();
      for (const Event &X : Events[Id]) {
        unsigned I = Index[X.V];
        if (X.IsDef) Def[Id].set(I);
        else if (!Def[Id][I]) Use[Id].set(I);   // upward-exposed use
      }
    }
  }
  void add(const VarDecl *V) { if (Index.emplace(V, Vars.size()).second) Vars.push_back(V); }
  std::string names(const llvm::BitVector &Bits) const {
    std::string S;
    for (int I = Bits.find_first(); I >= 0; I = Bits.find_next(I)) S += (S.empty() ? "" : ",") + Vars[I]->getNameAsString();
    return "{" + S + "}";
  }
};

// Result of one run: per block id, the set at the block's "start" in analysis direction.
struct Result {
  std::vector<llvm::BitVector> In, Out;   // In = before transfer, Out = after (analysis direction)
  unsigned Visits = 0;
  std::string Order;
};

// A worklist abstraction so the solver loop is written once.
struct Worklist {
  virtual ~Worklist() = default;
  virtual void push(const CFGBlock *B) = 0;
  virtual const CFGBlock *pop() = 0;
};
template <typename W> struct Wrap : Worklist {
  W &WL;
  explicit Wrap(W &WL) : WL(WL) {}
  void push(const CFGBlock *B) override { WL.enqueueBlock(B); }
  const CFGBlock *pop() override { return WL.dequeue(); }
};
struct Plain : Worklist {
  bool Lifo;
  std::deque<const CFGBlock *> Q;
  llvm::BitVector In;
  Plain(const CFG &G, bool Lifo) : Lifo(Lifo), In(G.getNumBlockIDs()) {}
  void push(const CFGBlock *B) override { if (B && !In[B->getBlockID()]) { In[B->getBlockID()] = true; Q.push_back(B); } }
  const CFGBlock *pop() override {
    if (Q.empty()) return nullptr;
    const CFGBlock *B = Lifo ? Q.back() : Q.front();
    Lifo ? Q.pop_back() : Q.pop_front();
    In[B->getBlockID()] = false;
    return B;
  }
};

// Generic gen/kill may-analysis:  Out = Gen | (In & ~Kill),  In = union of neighbours' Out.
static Result solve(const Problem &P, bool Forward, Worklist &WL, const std::vector<llvm::BitVector> &Gen,
                    const std::vector<llvm::BitVector> &Kill, const llvm::BitVector &Boundary, bool ReverseSeed) {
  unsigned N = P.Vars.size(), NB = P.G.getNumBlockIDs();
  Result R;
  R.In.assign(NB, llvm::BitVector(N));
  R.Out.assign(NB, llvm::BitVector(N));
  const CFGBlock *Start = Forward ? &P.G.getEntry() : &P.G.getExit();
  R.In[Start->getBlockID()] = Boundary;
  // seed with everything; the priority worklists ignore the seeding order, plain ones do not
  if (ReverseSeed) for (auto I = P.G.rbegin(); I != P.G.rend(); ++I) WL.push(*I);
  else for (const CFGBlock *B : P.G) WL.push(B);
  while (const CFGBlock *B = WL.pop()) {
    unsigned Id = B->getBlockID();
    ++R.Visits;
    R.Order += (R.Order.empty() ? "B" : " B") + std::to_string(Id);
    llvm::BitVector In = (B == Start) ? Boundary : llvm::BitVector(N);
    if (B != Start || true) {
      auto Join = [&](const CFGBlock *X) { if (X) In |= R.Out[X->getBlockID()]; };
      if (Forward) for (const CFGBlock *X : B->preds()) Join(X);
      else for (const CFGBlock *X : B->succs()) Join(X);
    }
    R.In[Id] = In;
    llvm::BitVector Out = In;
    Out.reset(Kill[Id]);
    Out |= Gen[Id];
    if (Out != R.Out[Id]) {
      R.Out[Id] = Out;
      if (Forward) for (const CFGBlock *X : B->succs()) WL.push(X);
      else for (const CFGBlock *X : B->preds()) WL.push(X);
    }
  }
  return R;
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (FuncOpt.empty() || FD->getQualifiedNameAsString() != FuncOpt) return;
        AnalysisDeclContextManager Mgr(Ctx);
        AnalysisDeclContext *AC = Mgr.getContext(FD);
        CFG *G = AC->getCFG();
        if (!G) return;
        Problem P(*G, Ctx, FD);
        bool Forward = ProblemOpt == "uninit";
        unsigned N = P.Vars.size();
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << ": " << ProblemOpt << " ("
                     << (Forward ? "forward" : "backward") << "), " << G->size() << " blocks, " << N << " variables\n";

        // gen/kill per block
        std::vector<llvm::BitVector> Gen, Kill;
        llvm::BitVector Boundary(N);
        if (Forward) { Gen.assign(G->getNumBlockIDs(), llvm::BitVector(N)); Kill = P.Def; Boundary = P.LocalsNoParams; }
        else { Gen = P.Use; Kill = P.Def; }

        std::optional<WeakTopologicalOrdering> WTO = getIntervalWTO(*G);
        std::vector<std::string> Which;
        if (WorklistOpt == "all") Which = Forward ? std::vector<std::string>{"rpo", "wto", "fifo", "fifo-r", "lifo"}
                                                  : std::vector<std::string>{"po", "wto", "fifo", "fifo-r", "lifo"};
        else Which = {WorklistOpt};

        std::vector<Result> Results;
        for (const std::string &W : Which) {
          std::unique_ptr<Worklist> WL;
          std::unique_ptr<ForwardDataflowWorklist> F;
          std::unique_ptr<BackwardDataflowWorklist> Bk;
          std::unique_ptr<WTODataflowWorklist> T;
          std::unique_ptr<WTOCompare> Cmp;
          if (W == "rpo") { F = std::make_unique<ForwardDataflowWorklist>(*G, *AC); WL = std::make_unique<Wrap<ForwardDataflowWorklist>>(*F); }
          else if (W == "po") { Bk = std::make_unique<BackwardDataflowWorklist>(*G, *AC); WL = std::make_unique<Wrap<BackwardDataflowWorklist>>(*Bk); }
          else if (W == "wto") {
            if (!WTO) { llvm::outs() << llvm::left_justify("wto", 7) << "n/a (irreducible CFG: getIntervalWTO returned nullopt)\n"; continue; }
            Cmp = std::make_unique<WTOCompare>(*WTO);
            T = std::make_unique<WTODataflowWorklist>(*G, *Cmp);
            WL = std::make_unique<Wrap<WTODataflowWorklist>>(*T);
          } else if (W == "fifo" || W == "fifo-r" || W == "lifo") WL = std::make_unique<Plain>(*G, W == "lifo");
          else { llvm::errs() << "unknown worklist " << W << "\n"; return; }

          Result R = solve(P, Forward, *WL, Gen, Kill, Boundary, W == "fifo-r");
          llvm::outs() << llvm::left_justify(W, 7) << "visits=" << llvm::format("%-3u", R.Visits);
          if (Trace || WorklistOpt != "all") llvm::outs() << " order: " << R.Order;
          llvm::outs() << "\n";
          Results.push_back(std::move(R));
        }
        if (Results.empty()) return;
        bool Same = true;
        for (const Result &R : Results) Same &= R.In == Results[0].In && R.Out == Results[0].Out;
        if (Results.size() > 1) llvm::outs() << "all strategies reach the same fixpoint: " << (Same ? "yes" : "NO") << "\n";

        const Result &R = Results[0];
        for (const CFGBlock *B : *G) {
          unsigned Id = B->getBlockID();
          if (!Forward) {
            // In = live-out (join of successors), Out = live-in
            llvm::outs() << "  " << llvm::left_justify(cfglab::blockName(B), 4) << "live-in " << llvm::left_justify(P.names(R.Out[Id]), 10)
                         << " live-out " << P.names(R.In[Id]) << "\n";
          } else {
            llvm::outs() << "  " << llvm::left_justify(cfglab::blockName(B), 4) << "maybe-uninit at entry " << P.names(R.In[Id]) << "\n";
          }
        }
        if (Forward) {
          // Replay each block statement by statement with its entry state and report uses.
          SourceManager &SM = Ctx.getSourceManager();
          for (const CFGBlock *B : *G) {
            llvm::BitVector State = R.In[B->getBlockID()];
            for (const CFGElement &E : *B) {
              auto CS = E.getAs<CFGStmt>();
              if (!CS) continue;
              std::vector<Event> Ev;
              collect(CS->getStmt(), Ev);
              for (const Event &X : Ev) {
                unsigned I = P.Index.at(X.V);
                if (X.IsDef) State.reset(I);
                else if (State[I])
                  llvm::outs() << "  line " << cfglab::lineOf(SM, CS->getStmt()->getBeginLoc()) << ": '" << X.V->getName()
                               << "' may be used uninitialised (" << cfglab::stmtText(CS->getStmt(), Ctx) << ")\n";
              }
            }
          }
        }
      });
}
