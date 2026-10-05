// p06_env -- look inside the Environment (Part 6.4, 6.8)
//
//   build/bin/p06_env manifests/p06_env.cpp --func=scalars
//   build/bin/p06_env manifests/p06_env.cpp --mode=create
//
// --mode=inspect   (default) at every `return`: each variable's StorageLocation and Value
// --mode=create    what Environment::createValue / createObject build for a few types
// --mode=synthetic synthetic fields on a record + Value properties
// --mode=calls     context-sensitive analysis: --depth=N inlines callees (DataflowAnalysisContext::Options)
#include "cfglab.h"

#include "clang/Analysis/FlowSensitive/AdornedCFG.h"
#include "clang/Analysis/FlowSensitive/DataflowAnalysis.h"
#include "clang/Analysis/FlowSensitive/NoopAnalysis.h"
#include "clang/Analysis/FlowSensitive/WatchedLiteralsSolver.h"

#include <algorithm>
#include <map>

using namespace clang;
using namespace clang::dataflow;

static llvm::cl::OptionCategory Cat("p06_env options");
static llvm::cl::opt<std::string> OptFunc("func", llvm::cl::desc("only this function"),
                                          llvm::cl::init(""), llvm::cl::cat(Cat));
static llvm::cl::opt<std::string> OptMode("mode", llvm::cl::init("inspect"), llvm::cl::cat(Cat));
static llvm::cl::opt<bool> OptNoBuiltin("no-builtin", llvm::cl::desc("DataflowAnalysisOptions::BuiltinOpts = nullopt"),
                                        llvm::cl::cat(Cat));
static llvm::cl::opt<int> OptDepth("depth", llvm::cl::desc("context-sensitive depth (-1 = off)"),
                                   llvm::cl::init(-1), llvm::cl::cat(Cat));

namespace {
// Stable small ids for objects whose identity matters (locations and values):
// pointers are not printable in a deterministic doc.
struct Ids {
  std::map<const void *, int> Loc, Val;
  std::string loc(const void *P) { return "L" + std::to_string(id(Loc, P)); }
  std::string val(const void *P) { return "#" + std::to_string(id(Val, P)); }
private:
  static int id(std::map<const void *, int> &M, const void *P) {
    auto It = M.find(P);
    if (It != M.end()) return It->second;
    int N = M.size() + 1;
    M[P] = N;
    return N;
  }
};

const char *valueKind(Value::Kind K) {
  switch (K) {
  case Value::Kind::Integer: return "Integer";
  case Value::Kind::Pointer: return "Pointer";
  case Value::Kind::TopBool: return "TopBool";
  case Value::Kind::AtomicBool: return "AtomicBool";
  case Value::Kind::FormulaBool: return "FormulaBool";
  }
  return "?";
}

std::string typeStr(QualType T, ASTContext &Ctx) {
  return T.isNull() ? std::string("(null)") : T.getAsString(Ctx.getPrintingPolicy());
}

std::string valueStr(const Value *V, Ids &I, ASTContext &Ctx) {
  if (!V) return "(no value)";
  std::string S = std::string(valueKind(V->getKind())) + I.val(V);
  if (auto *B = dyn_cast<BoolValue>(V)) {
    std::string F;
    llvm::raw_string_ostream OS(F);
    B->formula().print(OS);
    S += " formula=" + F;
  }
  if (auto *P = dyn_cast<PointerValue>(V)) S += " -> " + I.loc(&P->getPointeeLoc());
  for (auto &[Name, Prop] : V->properties()) S += " {" + Name.str() + "=" + valueKind(Prop->getKind()) + I.val(Prop) + "}";
  return S;
}

void printLoc(const StorageLocation *L, const Environment &Env, Ids &I, ASTContext &Ctx, const std::string &Indent) {
  if (!L) { llvm::outs() << "(null location)\n"; return; }
  if (auto *R = dyn_cast<RecordStorageLocation>(L)) {
    llvm::outs() << "Record " << I.loc(L) << " " << typeStr(L->getType(), Ctx) << "\n";
    std::vector<std::pair<std::string, const StorageLocation *>> Kids;
    for (auto &[D, C] : R->children()) Kids.push_back({D->getNameAsString(), C});
    std::sort(Kids.begin(), Kids.end(), [](auto &A, auto &B) { return A.first < B.first; });
    for (auto &[N, C] : Kids) { llvm::outs() << Indent << "  ." << N << ": "; printLoc(C, Env, I, Ctx, Indent + "  "); }
    std::vector<std::pair<std::string, const StorageLocation *>> Syn;
    for (auto &E : R->synthetic_fields()) Syn.push_back({E.getKey().str(), E.getValue()});
    std::sort(Syn.begin(), Syn.end(), [](auto &A, auto &B) { return A.first < B.first; });
    for (auto &[N, C] : Syn) { llvm::outs() << Indent << "  $" << N << ": "; printLoc(C, Env, I, Ctx, Indent + "  "); }
    return;
  }
  llvm::outs() << "Scalar " << I.loc(L) << " " << typeStr(L->getType(), Ctx) << " -> "
               << valueStr(Env.getValue(*L), I, Ctx) << "\n";
}

struct VarCollector : RecursiveASTVisitor<VarCollector> {
  std::vector<const VarDecl *> Vars;
  bool VisitVarDecl(VarDecl *V) { Vars.push_back(V); return true; }
};

void inspect(const FunctionDecl *FD, ASTContext &Ctx, const AdornedCFG &ACFG, DataflowAnalysisContext &DACtx) {
  VarCollector C;
  for (auto *P : FD->parameters()) C.Vars.push_back(P);
  C.TraverseStmt(FD->getBody());
  Environment Env(DACtx, *FD);
  // BuiltinOpts empty == the framework's own transfer functions are switched off.
  NoopAnalysis A = OptNoBuiltin ? NoopAnalysis(Ctx, DataflowAnalysisOptions{std::nullopt}) : NoopAnalysis(Ctx);
  Ids I;
  CFGEltCallbacks<NoopAnalysis> CB;
  CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
    auto S = E.getAs<CFGStmt>();
    if (!S || !isa<ReturnStmt>(S->getStmt())) return;
    llvm::outs() << "  at `" << cfglab::stmtText(S->getStmt(), Ctx, 30) << "`:\n";
    for (const VarDecl *V : C.Vars) {
      llvm::outs() << "    " << V->getName() << ": ";
      printLoc(St.Env.getStorageLocation(*V), St.Env, I, Ctx, "    ");
    }
    if (FD->getReturnType()->isVoidType()) return;
    llvm::outs() << "    value of the returned expression: " << valueStr(St.Env.getValue(*cast<ReturnStmt>(S->getStmt())->getRetValue()), I, Ctx) << "\n";
  };
  auto R = runDataflowAnalysis(ACFG, A, Env, CB);
  if (!R) llvm::outs() << "  failed: " << llvm::toString(R.takeError()) << "\n";
}

void create(ASTContext &Ctx, DataflowAnalysisContext &DACtx, const FunctionDecl *FD) {
  Environment Env(DACtx, *FD);
  Ids I;
  auto show = [&](const char *Name, QualType T) {
    Value *V = Env.createValue(T);
    llvm::outs() << "  createValue(" << Name << ") = ";
    if (!V) { llvm::outs() << "nullptr\n"; return; }
    llvm::outs() << valueStr(V, I, Ctx);
    if (auto *P = dyn_cast<PointerValue>(V)) {
      llvm::outs() << "\n      pointee location: ";
      printLoc(&P->getPointeeLoc(), Env, I, Ctx, "      ");
    } else llvm::outs() << "\n";
  };
  show("int", Ctx.IntTy);
  show("bool", Ctx.BoolTy);
  show("long", Ctx.LongTy);
  show("int *", Ctx.getPointerType(Ctx.IntTy));
  show("int **", Ctx.getPointerType(Ctx.getPointerType(Ctx.IntTy)));
  show("double", Ctx.DoubleTy);
  llvm::outs() << "  createObject(int): ";
  StorageLocation &L = Env.createObject(Ctx.IntTy);
  printLoc(&L, Env, I, Ctx, "");
  llvm::outs() << "  getIntLiteralValue(3) twice: "
               << (&Env.getIntLiteralValue(llvm::APInt(32, 3)) == &Env.getIntLiteralValue(llvm::APInt(32, 3)) ? "same Value" : "different Values") << "\n";
  llvm::outs() << "  getIntLiteralValue(3) vs (4): "
               << (&Env.getIntLiteralValue(llvm::APInt(32, 3)) == &Env.getIntLiteralValue(llvm::APInt(32, 4)) ? "same Value" : "different Values") << "\n";
  llvm::outs() << "  getBoolLiteralValue(true) formula: " << Env.getBoolLiteralValue(true).formula() << "\n";
  llvm::outs() << "  makeAtomicBoolValue() twice: "
               << (&Env.makeAtomicBoolValue() == &Env.makeAtomicBoolValue() ? "same" : "different") << "\n";
}

void synthetic(const FunctionDecl *FD, ASTContext &Ctx, const AdornedCFG &ACFG) {
  DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
  // Must happen before any RecordStorageLocation exists; must be a pure function of the type.
  DACtx.setSyntheticFieldCallback([&](QualType T) {
    llvm::StringMap<QualType> M;
    if (auto *RD = T->getAsCXXRecordDecl(); RD && RD->getName() == "Counter") M["count"] = Ctx.IntTy;
    return M;
  });
  Environment Env(DACtx, *FD);
  NoopAnalysis A(Ctx);
  Ids I;
  CFGEltCallbacks<NoopAnalysis> CB;
  CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
    auto S = E.getAs<CFGStmt>();
    if (!S || !isa<ReturnStmt>(S->getStmt())) return;
    Environment Fork = St.Env.fork();
    for (auto *P : FD->parameters()) {
      llvm::outs() << "  " << P->getName() << ": ";
      auto *L = Fork.get<RecordStorageLocation>(*P);
      if (!L) { llvm::outs() << "(not a record)\n"; continue; }
      printLoc(L, Fork, I, Ctx, "  ");
      if (L->synthetic_fields().empty()) continue;
      // Write the synthetic field, then a property on that value.
      StorageLocation &CL = L->getSyntheticField("count");
      // A fresh Value: the interned literal Values are shared by every use of that literal.
      IntegerValue &Fresh = Fork.create<IntegerValue>();
      Fork.setValue(CL, Fresh);
      Fresh.setProperty("checked", Fork.getBoolLiteralValue(true));
      llvm::outs() << "  after setValue($count, <fresh Integer>) and setProperty(\"checked\", true):\n    $count: ";
      printLoc(&CL, Fork, I, Ctx, "    ");
    }
  };
  auto R = runDataflowAnalysis(ACFG, A, Env, CB);
  if (!R) llvm::outs() << "  failed: " << llvm::toString(R.takeError()) << "\n";
}

void calls(const FunctionDecl *FD, ASTContext &Ctx, const AdornedCFG &ACFG) {
  DataflowAnalysisContext::Options Opts;
  if (OptDepth >= 0) Opts.ContextSensitiveOpts = ContextSensitiveOptions{(unsigned)OptDepth};
  DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>(), Opts);
  Environment Env(DACtx, *FD);
  // The default constructor keeps the built-in transfer on, and passes DACtx's
  // options through: that is where pushCall/popCall happens.
  NoopAnalysis A(Ctx, DataflowAnalysisOptions{Opts});
  Ids I;
  CFGEltCallbacks<NoopAnalysis> CB;
  CB.Before = [&](const CFGElement &E, const DataflowAnalysisState<NoopLattice> &St) {
    auto S = E.getAs<CFGStmt>();
    if (!S || !isa<ReturnStmt>(S->getStmt())) return;
    VarCollector C;
    for (auto *P : FD->parameters()) C.Vars.push_back(P);
    C.TraverseStmt(FD->getBody());
    for (const VarDecl *V : C.Vars)
      llvm::outs() << "  " << V->getName() << " = " << valueStr(St.Env.getValue(*V), I, Ctx) << "\n";
  };
  auto R = runDataflowAnalysis(ACFG, A, Env, CB);
  if (!R) llvm::outs() << "  failed: " << llvm::toString(R.takeError()) << "\n";
}
} // namespace

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!OptFunc.empty() && FD->getNameAsString() != OptFunc) return;
        if (OptMode == "create") {
          if (FD->getNameAsString() != "scalars" && OptFunc.empty()) return;
          llvm::outs() << "== createValue / createObject (inside the environment of " << FD->getNameAsString() << ")\n";
          DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
          create(Ctx, DACtx, FD);
          return;
        }
        auto ACFG = AdornedCFG::build(*FD);
        if (!ACFG) { llvm::errs() << llvm::toString(ACFG.takeError()) << "\n"; return; }
        llvm::outs() << "== " << FD->getNameAsString() << "\n";
        if (OptMode == "synthetic") synthetic(FD, Ctx, *ACFG);
        else if (OptMode == "calls") calls(FD, Ctx, *ACFG);
        else {
          DataflowAnalysisContext DACtx(std::make_unique<WatchedLiteralsSolver>());
          inspect(FD, Ctx, *ACFG, DACtx);
        }
      });
}
