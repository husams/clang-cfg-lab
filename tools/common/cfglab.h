// cfglab.h -- shared helpers for every tool in the clang-cfg-lab.
//
// Header-only. Include as  #include "cfglab.h"  (tools/CMakeLists.txt adds
// tools/common to the include path).
//
// What it gives you
//   * cfglab::runTool<Action>()      -- boilerplate main(): parse args, add the
//                                       macOS platform flags, run the action
//   * cfglab::runPerFunction()       -- same, but you only supply a lambda that
//                                       receives every function definition in
//                                       the main file
//   * cfglab::addPlatformFlags()     -- -resource-dir, libc++ and SDK flags
//   * cfglab::semaPreset() ...       -- the CFG::BuildOptions presets Clang's
//                                       own consumers use (Part 2.3)
//   * cfglab::kindName() ...         -- names for CFGElement / CFGTerminator
//   * cfglab::stmtText() ...         -- one-line source text for a Stmt
//
// Set CFGLAB_RAW=1 in the environment to skip addPlatformFlags (Part 2.1
// uses that to show what the flags are for).

#ifndef CFGLAB_H
#define CFGLAB_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/PrettyPrinter.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/ConstructionContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Injected by tools/CMakeLists.txt (add_cfg_tool). Fallbacks keep the header
// usable if someone compiles a tool by hand.
#ifndef CFGLAB_LLVM_PREFIX
#define CFGLAB_LLVM_PREFIX "/opt/homebrew/opt/llvm"
#endif
#ifndef CFGLAB_RESOURCE_DIR
#define CFGLAB_RESOURCE_DIR CFGLAB_LLVM_PREFIX "/lib/clang/22"
#endif

namespace cfglab {

using namespace clang;
using namespace clang::tooling;

// --------------------------------------------------------------------------
// Platform plumbing
// --------------------------------------------------------------------------

inline std::string sdkPath() {
  std::string Out;
  if (FILE *P = popen("xcrun --show-sdk-path 2>/dev/null", "r")) {
    char Buf[512] = {};
    if (fgets(Buf, sizeof(Buf), P) && Buf[0] == '/') {
      Out = Buf;
      Out.erase(Out.find_last_not_of('\n') + 1);
    }
    pclose(P);
  }
  return Out;
}

// An embedded front end does not know where the compiler's builtin headers
// (stdarg.h, ...) or the C++ standard library live. We point it at:
//   -resource-dir   builtin headers of the *Homebrew* clang we link against
//   -nostdinc++ -isystem <llvm>/include/c++/v1
//                   Homebrew's libc++ headers, instead of the SDK's copy
//                   (mixing the two breaks <cmath>/<cstring>)
//   -isysroot       the macOS SDK for the C library headers
inline void addPlatformFlags(ClangTool &Tool) {
  if (const char *Raw = std::getenv("CFGLAB_RAW"); Raw && *Raw == '1')
    return;
  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      {"-resource-dir", CFGLAB_RESOURCE_DIR}, ArgumentInsertPosition::END));
  Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
      {"-nostdinc++", "-isystem", CFGLAB_LLVM_PREFIX "/include/c++/v1"},
      ArgumentInsertPosition::END));
  std::string SDK = sdkPath();
  if (!SDK.empty())
    Tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster({"-isysroot", SDK}, ArgumentInsertPosition::END));
}

// CommonOptionsParser needs either compile_commands.json or "-- <flags>".
// Let the user omit the "--": we append one with a sensible -std for the
// extension of the first positional argument.
inline std::vector<const char *> withDefaultCompileFlags(int argc, const char **argv,
                                                         std::vector<std::string> &Storage) {
  std::vector<const char *> Args(argv, argv + argc);
  bool HasDashDash = false;
  bool HasCompDB = false;
  std::string FirstSource;
  for (int I = 1; I < argc; ++I) {
    std::string A = argv[I];
    if (A == "--") { HasDashDash = true; break; }
    if (A == "-p" || A.rfind("-p=", 0) == 0 || A.rfind("-p ", 0) == 0) HasCompDB = true;
    if (!A.empty() && A[0] != '-' && FirstSource.empty()) FirstSource = A;
  }
  if (HasDashDash || HasCompDB) return Args;
  bool IsC = FirstSource.size() > 2 && FirstSource.compare(FirstSource.size() - 2, 2, ".c") == 0;
  Storage = {"--", IsC ? "-std=c11" : "-std=c++17"};
  for (auto &S : Storage) Args.push_back(S.c_str());
  return Args;
}

// --------------------------------------------------------------------------
// main() boilerplate
// --------------------------------------------------------------------------

template <typename ActionT>
int runTool(int argc, const char **argv, llvm::cl::OptionCategory &Cat) {
  std::vector<std::string> Storage;
  auto Args = withDefaultCompileFlags(argc, argv, Storage);
  int N = static_cast<int>(Args.size());
  auto Options = CommonOptionsParser::create(N, Args.data(), Cat);
  if (!Options) {
    llvm::errs() << llvm::toString(Options.takeError());
    return 1;
  }
  ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  addPlatformFlags(Tool);
  return Tool.run(newFrontendActionFactory<ActionT>().get());
}

// --------------------------------------------------------------------------
// Per-function driver: "give me every function definition in the main file"
// --------------------------------------------------------------------------

using FunctionCallback =
    std::function<void(const FunctionDecl *, ASTContext &, Preprocessor &)>;

// Function definitions that make sense to analyze: has a body, is not a
// template pattern (templated bodies have dependent types), lives in the
// main file (not in a header).
inline bool isInteresting(const FunctionDecl *FD, ASTContext &Ctx) {
  return FD->doesThisDeclarationHaveABody() && !FD->isTemplated() &&
         Ctx.getSourceManager().isInMainFile(FD->getLocation());
}

namespace detail {
struct FnVisitor : RecursiveASTVisitor<FnVisitor> {
  ASTContext &Ctx;
  Preprocessor &PP;
  const FunctionCallback &CB;
  FnVisitor(ASTContext &C, Preprocessor &P, const FunctionCallback &F)
      : Ctx(C), PP(P), CB(F) {}
  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (isInteresting(FD, Ctx)) CB(FD, Ctx, PP);
    return true;
  }
};
struct FnConsumer : ASTConsumer {
  Preprocessor &PP;
  const FunctionCallback &CB;
  FnConsumer(Preprocessor &P, const FunctionCallback &F) : PP(P), CB(F) {}
  void HandleTranslationUnit(ASTContext &Ctx) override {
    FnVisitor(Ctx, PP, CB).TraverseDecl(Ctx.getTranslationUnitDecl());
  }
};
struct FnActionFactory : FrontendActionFactory {
  const FunctionCallback &CB;
  explicit FnActionFactory(const FunctionCallback &F) : CB(F) {}
  std::unique_ptr<FrontendAction> create() override {
    struct Act : ASTFrontendAction {
      const FunctionCallback &CB;
      explicit Act(const FunctionCallback &F) : CB(F) {}
      std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
                                                     StringRef) override {
        return std::make_unique<FnConsumer>(CI.getPreprocessor(), CB);
      }
    };
    return std::make_unique<Act>(CB);
  }
};
} // namespace detail

inline int runPerFunction(int argc, const char **argv, llvm::cl::OptionCategory &Cat,
                          FunctionCallback CB) {
  std::vector<std::string> Storage;
  auto Args = withDefaultCompileFlags(argc, argv, Storage);
  int N = static_cast<int>(Args.size());
  auto Options = CommonOptionsParser::create(N, Args.data(), Cat);
  if (!Options) {
    llvm::errs() << llvm::toString(Options.takeError());
    return 1;
  }
  ClangTool Tool(Options->getCompilations(), Options->getSourcePathList());
  addPlatformFlags(Tool);
  detail::FnActionFactory F(CB);
  return Tool.run(&F);
}

// --------------------------------------------------------------------------
// CFG::BuildOptions presets (what real Clang consumers use; see Part 2.3)
// --------------------------------------------------------------------------

// Everything off except the one default-on field (PruneTriviallyFalseEdges).
inline CFG::BuildOptions defaultOptions() { return CFG::BuildOptions(); }

// What Sema's -W... analyses start from (AnalysisBasedWarnings.cpp), with the
// "linearized CFG" switch on, as for unreachable-code / thread-safety.
inline CFG::BuildOptions semaPreset() {
  CFG::BuildOptions BO;
  BO.PruneTriviallyFalseEdges = true;
  BO.AddEHEdges = false;
  BO.AddInitializers = true;
  BO.AddImplicitDtors = true;
  BO.AddTemporaryDtors = true;
  BO.AddCXXNewAllocator = false;
  BO.AddCXXDefaultInitExprInCtors = true;
  BO.setAllAlwaysAdd();
  return BO;
}

// What clang::dataflow::AdornedCFG::build() uses (FlowSensitive framework).
inline CFG::BuildOptions adornedPreset() {
  CFG::BuildOptions BO;
  BO.PruneTriviallyFalseEdges = true;
  BO.AddImplicitDtors = true;
  BO.AddTemporaryDtors = true;
  BO.AddInitializers = true;
  BO.AddCXXDefaultInitExprInCtors = true;
  BO.AddLifetime = true;
  BO.setAllAlwaysAdd();
  return BO;
}

// What `clang -analyze` (the Static Analyzer, and so debug.DumpCFG) builds by
// default: the cfg-* analyzer-config defaults (cfg-implicit-dtors,
// cfg-temporary-dtors, cfg-rich-constructors, cfg-conditional-static-
// initializers = true; cfg-lifetime, cfg-scopes, cfg-loopexit = false) plus
// what AnalysisManager hard-codes: initializers on, virtual-base branches on,
// and setAllAlwaysAdd() -- which is why the analyzer's dumps list every
// sub-expression.
inline CFG::BuildOptions analyzerPreset() {
  CFG::BuildOptions BO;
  BO.PruneTriviallyFalseEdges = true;
  BO.AddInitializers = true;
  BO.AddImplicitDtors = true;
  BO.AddTemporaryDtors = true;
  BO.AddRichCXXConstructors = true;
  BO.MarkElidedCXXConstructors = true;
  BO.AddStaticInitBranches = true;
  BO.AddCXXNewAllocator = true;
  BO.AddVirtualBaseBranches = true;
  BO.setAllAlwaysAdd();
  return BO;
}

// Every switch that *adds* information on (all 17 fields except
// OmitImplicitValueInitializers, the one option that removes elements), plus
// setAllAlwaysAdd(). Useful to see "everything the CFG can express".
inline CFG::BuildOptions kitchenSinkPreset() {
  CFG::BuildOptions BO;
  BO.PruneTriviallyFalseEdges = true;
  BO.AddEHEdges = true;
  BO.AddInitializers = true;
  BO.AddImplicitDtors = true;
  BO.AddTemporaryDtors = true;
  BO.AddLifetime = true;
  BO.AddScopes = true;
  BO.AddLoopExit = true;
  BO.AddStaticInitBranches = true;
  BO.AddCXXNewAllocator = true;
  BO.AddCXXDefaultInitExprInCtors = true;
  BO.AddCXXDefaultInitExprInAggregates = true;
  BO.AddRichCXXConstructors = true;
  BO.MarkElidedCXXConstructors = true;
  BO.AddVirtualBaseBranches = true;
  BO.AssumeReachableDefaultInSwitchStatements = true;
  BO.setAllAlwaysAdd();
  return BO;
}

// Copy every setting from Src into Dst but keep Dst's forcedBlkExprs pointer.
// AnalysisDeclContext points that field at its own map, so assigning a whole
// BuildOptions over AC->getCFGBuildOptions() would silently disconnect it.
inline void applyPreset(CFG::BuildOptions &Dst, const CFG::BuildOptions &Src) {
  CFG::BuildOptions::ForcedBlkExprs **Keep = Dst.forcedBlkExprs;
  Dst = Src;
  Dst.forcedBlkExprs = Keep;
}

// The 17 boolean fields of CFG::BuildOptions, by name. Lets a tool toggle any
// field from the command line (--set=AddImplicitDtors,AddScopes).
struct OptionField {
  const char *Name;
  bool CFG::BuildOptions::*Ptr;
};
inline const std::vector<OptionField> &optionFields() {
  static const std::vector<OptionField> F = {
#define CFGLAB_F(x) {#x, &CFG::BuildOptions::x},
      CFGLAB_F(PruneTriviallyFalseEdges) CFGLAB_F(AddEHEdges)
      CFGLAB_F(AddInitializers) CFGLAB_F(AddImplicitDtors) CFGLAB_F(AddLifetime)
      CFGLAB_F(AddLoopExit) CFGLAB_F(AddTemporaryDtors) CFGLAB_F(AddScopes)
      CFGLAB_F(AddStaticInitBranches) CFGLAB_F(AddCXXNewAllocator)
      CFGLAB_F(AddCXXDefaultInitExprInCtors)
      CFGLAB_F(AddCXXDefaultInitExprInAggregates)
      CFGLAB_F(AddRichCXXConstructors) CFGLAB_F(MarkElidedCXXConstructors)
      CFGLAB_F(AddVirtualBaseBranches) CFGLAB_F(OmitImplicitValueInitializers)
      CFGLAB_F(AssumeReachableDefaultInSwitchStatements)
#undef CFGLAB_F
  };
  return F;
}

// Set every field named in a comma-separated list to Value. Returns false and
// prints to stderr on an unknown name.
inline bool setOptionList(CFG::BuildOptions &BO, llvm::StringRef List, bool Value) {
  llvm::SmallVector<llvm::StringRef, 8> Names;
  List.split(Names, ',', -1, /*KeepEmpty=*/false);
  for (llvm::StringRef N : Names) {
    bool Found = false;
    for (const OptionField &F : optionFields())
      if (N == F.Name) { BO.*(F.Ptr) = Value; Found = true; }
    if (!Found) {
      llvm::errs() << "unknown BuildOptions field '" << N << "'\n";
      return false;
    }
  }
  return true;
}

// Preset by name: default | sema | adorned | analyzer | kitchen | none.
// "none" is default with PruneTriviallyFalseEdges also turned off.
inline bool presetByName(llvm::StringRef Name, CFG::BuildOptions &Out) {
  if (Name == "default") Out = defaultOptions();
  else if (Name == "sema") Out = semaPreset();
  else if (Name == "adorned") Out = adornedPreset();
  else if (Name == "analyzer") Out = analyzerPreset();
  else if (Name == "kitchen") Out = kitchenSinkPreset();
  else if (Name == "none") { Out = defaultOptions(); Out.PruneTriviallyFalseEdges = false; }
  else return false;
  return true;
}

// Every concrete Stmt class by name (from clang/AST/StmtNodes.inc), for
// --always-add=BinaryOperator,DeclRefExpr style flags.
inline const std::map<std::string, Stmt::StmtClass> &stmtClassesByName() {
  static const std::map<std::string, Stmt::StmtClass> M = {
#define STMT(CLASS, PARENT) {#CLASS, Stmt::CLASS##Class},
#define ABSTRACT_STMT(STMT)
#include "clang/AST/StmtNodes.inc"
  };
  return M;
}

// Compose BuildOptions from the usual flag values. Returns false (after
// printing why) on a bad name.
inline bool makeBuildOptions(llvm::StringRef Preset, llvm::ArrayRef<std::string> Set,
                             llvm::ArrayRef<std::string> Clear, llvm::StringRef AlwaysAdd,
                             CFG::BuildOptions &BO) {
  if (!presetByName(Preset, BO)) {
    llvm::errs() << "unknown preset '" << Preset
                 << "' (default|sema|adorned|analyzer|kitchen|none)\n";
    return false;
  }
  for (const std::string &L : Set)
    if (!setOptionList(BO, L, true)) return false;
  for (const std::string &L : Clear)
    if (!setOptionList(BO, L, false)) return false;
  if (AlwaysAdd == "all") {
    BO.setAllAlwaysAdd();
  } else if (!AlwaysAdd.empty()) {
    llvm::SmallVector<llvm::StringRef, 8> Names;
    AlwaysAdd.split(Names, ',', -1, false);
    for (llvm::StringRef N : Names) {
      auto It = stmtClassesByName().find(N.str());
      if (It == stmtClassesByName().end()) {
        llvm::errs() << "unknown Stmt class '" << N << "'\n";
        return false;
      }
      BO.setAlwaysAdd(It->second);
    }
  }
  return true;
}

// --------------------------------------------------------------------------
// Names
// --------------------------------------------------------------------------

inline const char *kindName(CFGElement::Kind K) {
  switch (K) {
#define CFGLAB_K(x) case CFGElement::x: return #x;
    CFGLAB_K(Initializer) CFGLAB_K(ScopeBegin) CFGLAB_K(ScopeEnd)
    CFGLAB_K(NewAllocator) CFGLAB_K(LifetimeEnds) CFGLAB_K(LoopExit)
    CFGLAB_K(Statement) CFGLAB_K(Constructor) CFGLAB_K(CXXRecordTypedCall)
    CFGLAB_K(AutomaticObjectDtor) CFGLAB_K(DeleteDtor) CFGLAB_K(BaseDtor)
    CFGLAB_K(MemberDtor) CFGLAB_K(TemporaryDtor) CFGLAB_K(CleanupFunction)
#undef CFGLAB_K
  }
  return "?";
}

inline const char *termKindName(const CFGTerminator &T) {
  if (!T.isValid()) return "none";
  switch (T.getKind()) {
  case CFGTerminator::StmtBranch: return "StmtBranch";
  case CFGTerminator::TemporaryDtorsBranch: return "TemporaryDtorsBranch";
  case CFGTerminator::VirtualBaseBranch: return "VirtualBaseBranch";
  }
  return "?";
}

// --------------------------------------------------------------------------
// Small printing helpers
// --------------------------------------------------------------------------

inline unsigned lineOf(const SourceManager &SM, SourceLocation L) {
  return SM.getSpellingLineNumber(L);
}

// One-line source text for a statement, truncated to Max characters.
inline std::string stmtText(const Stmt *S, ASTContext &Ctx, size_t Max = 48) {
  if (!S) return "<null>";
  std::string Buf;
  llvm::raw_string_ostream OS(Buf);
  PrintingPolicy PP(Ctx.getLangOpts());
  PP.TerseOutput = true;
  S->printPretty(OS, nullptr, PP);
  OS.flush();
  for (char &C : Buf)
    if (C == '\n') C = ' ';
  // collapse runs of spaces
  std::string Out;
  for (char C : Buf)
    if (!(C == ' ' && !Out.empty() && Out.back() == ' ')) Out += C;
  while (!Out.empty() && (Out.back() == ' ' || Out.back() == ';')) Out.pop_back();
  if (Out.size() > Max) Out = Out.substr(0, Max - 3) + "...";
  return Out;
}

// Human-readable, kind-specific description of one CFGElement (the part of
// the line after the kind name). Shows which accessor each kind offers.
inline std::string elementDetail(const CFGElement &E, ASTContext &Ctx) {
  std::string S;
  llvm::raw_string_ostream OS(S);
  switch (E.getKind()) {
  case CFGElement::Statement:
  case CFGElement::Constructor:
  case CFGElement::CXXRecordTypedCall: {
    // getAs<CFGStmt> matches all three kinds above (they are CFGStmt subclasses).
    const Stmt *St = E.castAs<CFGStmt>().getStmt();
    OS << llvm::format("%-18s", St->getStmtClassName()) << " " << stmtText(St, Ctx);
    if (auto C = E.getAs<CFGConstructor>())
      OS << "   [ctor ctx kind " << C->getConstructionContext()->getKind() << "]";
    break;
  }
  case CFGElement::Initializer: {
    const CXXCtorInitializer *I = E.castAs<CFGInitializer>().getInitializer();
    if (I->isBaseInitializer()) OS << "base " << I->getTypeSourceInfo()->getType().getAsString();
    else if (I->isAnyMemberInitializer()) OS << "member " << I->getAnyMember()->getName();
    else OS << "delegating";
    break;
  }
  case CFGElement::ScopeBegin:
    OS << "var " << E.castAs<CFGScopeBegin>().getVarDecl()->getName();
    break;
  case CFGElement::ScopeEnd:
    OS << "var " << E.castAs<CFGScopeEnd>().getVarDecl()->getName();
    break;
  case CFGElement::LifetimeEnds:
    OS << "var " << E.castAs<CFGLifetimeEnds>().getVarDecl()->getName();
    break;
  case CFGElement::LoopExit:
    OS << E.castAs<CFGLoopExit>().getLoopStmt()->getStmtClassName();
    break;
  case CFGElement::NewAllocator:
    OS << stmtText(E.castAs<CFGNewAllocator>().getAllocatorExpr(), Ctx);
    break;
  case CFGElement::AutomaticObjectDtor:
    OS << "~" << E.castAs<CFGAutomaticObjDtor>().getVarDecl()->getType().getAsString()
       << " for var " << E.castAs<CFGAutomaticObjDtor>().getVarDecl()->getName();
    break;
  case CFGElement::DeleteDtor:
    OS << "delete of " << E.castAs<CFGDeleteDtor>().getCXXRecordDecl()->getName();
    break;
  case CFGElement::BaseDtor:
    OS << "base " << E.castAs<CFGBaseDtor>().getBaseSpecifier()->getType().getAsString();
    break;
  case CFGElement::MemberDtor:
    OS << "member " << E.castAs<CFGMemberDtor>().getFieldDecl()->getName();
    break;
  case CFGElement::TemporaryDtor:
    OS << "temporary " << stmtText(E.castAs<CFGTemporaryDtor>().getBindTemporaryExpr(), Ctx);
    break;
  case CFGElement::CleanupFunction:
    OS << "cleanup fn " << E.castAs<CFGCleanupFunction>().getFunctionDecl()->getName();
    break;
  }
  return OS.str();
}

inline std::string blockName(const CFGBlock *B) {
  return B ? "B" + std::to_string(B->getBlockID()) : "null";
}

} // namespace cfglab

// Put this once at namespace scope in a tool, after its OptionCategory, to give
// the tool the standard flags
//     --preset=NAME --set=F,.. --clear=F,.. --always-add=Class,..|all --func=NAME
// and two helpers:  bool flagsToOptions(CFG::BuildOptions&)
//                   bool flagWantsFunction(const FunctionDecl*)
#define CFGLAB_DEFINE_BUILD_FLAGS(CAT)                                              \
  static llvm::cl::opt<std::string> PresetFlag(                                     \
      "preset", llvm::cl::init("default"), llvm::cl::cat(CAT),                      \
      llvm::cl::desc("default|sema|adorned|analyzer|kitchen|none"));                \
  static llvm::cl::list<std::string> SetFlag(                                       \
      "set", llvm::cl::CommaSeparated, llvm::cl::cat(CAT),                          \
      llvm::cl::desc("BuildOptions fields to turn on (repeatable)"));               \
  static llvm::cl::list<std::string> ClearFlag(                                     \
      "clear", llvm::cl::CommaSeparated, llvm::cl::cat(CAT),                        \
      llvm::cl::desc("BuildOptions fields to turn off (repeatable)"));              \
  static llvm::cl::opt<std::string> AlwaysAddFlag(                                  \
      "always-add", llvm::cl::cat(CAT),                                             \
      llvm::cl::desc("Stmt classes to force into the CFG, or `all`"));              \
  static llvm::cl::opt<std::string> FuncFlag(                                       \
      "func", llvm::cl::cat(CAT), llvm::cl::desc("only this function"));            \
  static bool flagsToOptions(clang::CFG::BuildOptions &BO) {                        \
    return cfglab::makeBuildOptions(PresetFlag, SetFlag, ClearFlag, AlwaysAddFlag, BO); \
  }                                                                                 \
  static bool flagWantsFunction(const clang::FunctionDecl *FD) {                    \
    return FuncFlag.empty() || FD->getQualifiedNameAsString() == FuncFlag;          \
  }

#endif // CFGLAB_H
