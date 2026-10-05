// p07_verify -- Part 7.4: a lit-style test harness for the use-after-move checker.
//
//   build/bin/p07_verify FIXTURE.cpp... [--verbose]
//
// Expectations live in the fixture, as trailing comments (like clang -verify):
//
//   a.use();                 // expect: certain      a diagnostic of this kind on THIS line
//   a.use();                 // known-fp: possible   reported today but WRONG (strict)
//   a.use();                 // xfail: possible      SHOULD be reported, is not today (strict)
//   void heavy(...) {        // expect-error: max-sat   this function must fail like so
//   // budget: max-visits=20 max-sat=5000            file-level budgets (optional)
//
// "strict": when the gap closes, the marker itself fails the run (XPASS) until removed.
// Exit status: 0 all fixtures pass, 1 otherwise.

#include "cfglab.h"
#include "../p07_tu/Driver.h"

#include <set>

using namespace clang;

static llvm::cl::OptionCategory Cat("p07_verify options");
static llvm::cl::opt<bool> VfVerbose("verbose", llvm::cl::desc("print every check, not only failures"), llvm::cl::cat(Cat));

namespace {
struct Marker {
  unsigned Line;
  std::string Kind;   // certain | possible | max-sat | max-visits | ...
  enum { Expect, KnownFP, XFail, ExpectError } What;
  bool Used = false;
};

int FixturesRun = 0, FixturesFailed = 0;

void parseMarkers(StringRef Text, std::vector<Marker> &Out, p07::Budget &B) {
  unsigned Line = 0;
  for (StringRef L : llvm::split(Text, '\n')) {
    ++Line;
    size_t C = L.find("//");
    if (C == StringRef::npos) continue;
    StringRef Cm = L.drop_front(C + 2).trim();
    bool HasCode = !L.take_front(C).trim().empty();   // markers are trailing comments only
    auto take = [&](StringRef Tag, decltype(Marker::What) W) {
      if (!Cm.consume_front(Tag)) return false;
      Out.push_back({Line, Cm.trim().str(), W});
      return true;
    };
    if (HasCode && (take("expect-error:", Marker::ExpectError) || take("expect:", Marker::Expect) ||
                    take("known-fp:", Marker::KnownFP) || take("xfail:", Marker::XFail)))
      continue;
    if (!HasCode && Cm.consume_front("budget:")) {
      SmallVector<StringRef> Parts;
      Cm.trim().split(Parts, ' ', -1, false);
      for (StringRef P : Parts) {
        if (P.consume_front("max-sat=")) P.getAsInteger(10, B.MaxSat);
        else if (P.consume_front("max-visits=")) P.getAsInteger(10, B.MaxVisits);
      }
    }
  }
}

struct Visitor : RecursiveASTVisitor<Visitor> {
  ASTContext &Ctx;
  p07::Options O;
  std::vector<std::pair<const FunctionDecl *, p07::FnResult>> Results;
  explicit Visitor(ASTContext &C, p07::Budget B) : Ctx(C) { O.B = B; }
  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->doesThisDeclarationHaveABody() || FD->isImplicit()) return true;
    p07::FnResult R = p07::analyze(FD, Ctx, O);
    if (R.St != p07::Status::SkipTemplated && R.St != p07::Status::SkipFile) Results.push_back({FD, std::move(R)});
    return true;
  }
};

struct Consumer : ASTConsumer {
  void HandleTranslationUnit(ASTContext &Ctx) override {
    SourceManager &SM = Ctx.getSourceManager();
    FileID FID = SM.getMainFileID();
    std::string File = SM.getFileEntryRefForID(FID)->getName().str();
    StringRef Text = SM.getBufferData(FID);

    std::vector<Marker> Marks;
    p07::Budget B;
    parseMarkers(Text, Marks, B);
    Visitor V(Ctx, B);
    V.TraverseDecl(Ctx.getTranslationUnitDecl());

    std::vector<std::string> Failures, Notes;
    auto find = [&](unsigned Line, StringRef Kind, decltype(Marker::What) W) -> Marker * {
      for (Marker &M : Marks)
        if (!M.Used && M.Line == Line && M.What == W && M.Kind == Kind) return &M;
      return nullptr;
    };
    auto fmt = [&](unsigned L, const std::string &S) { return "line " + std::to_string(L) + ": " + S; };

    for (auto &[FD, R] : V.Results) {
      unsigned FnLine = SM.getPresumedLineNumber(FD->getLocation());
      if (p07::isError(R.St)) {
        std::string Want = std::string(p07::statusName(R.St)).substr(6);   // strip "error:"
        if (Marker *M = find(FnLine, Want, Marker::ExpectError)) {
          M->Used = true;
          Notes.push_back(fmt(FnLine, "ok, " + FD->getNameAsString() + " failed as expected (" + Want + ")"));
        } else {
          Failures.push_back(fmt(FnLine, FD->getNameAsString() + ": unexpected " + p07::statusName(R.St)));
        }
        continue;
      }
      if (R.St != p07::Status::Analyzed) continue;
      for (const p07::MoveDiag &D : R.Diags) {
        unsigned L = SM.getPresumedLineNumber(D.Loc);
        std::string Kind = D.Certain ? "certain" : "possible";
        if (Marker *M = find(L, Kind, Marker::Expect)) {
          M->Used = true;
          Notes.push_back(fmt(L, "ok, " + Kind + " as expected"));
        } else if (Marker *M2 = find(L, Kind, Marker::KnownFP)) {
          M2->Used = true;
          Notes.push_back(fmt(L, "known false positive (" + Kind + ")"));
        } else if (Marker *M3 = find(L, Kind, Marker::XFail)) {
          M3->Used = true;
          Failures.push_back(fmt(L, "XPASS " + Kind + " is now reported; remove the xfail marker"));
        } else {
          Failures.push_back(fmt(L, "unexpected " + Kind + "   (add: // expect: " + Kind + ")"));
        }
      }
    }
    for (Marker &M : Marks) {
      if (M.Used) continue;
      switch (M.What) {
      case Marker::Expect: Failures.push_back(fmt(M.Line, "missing " + M.Kind)); break;
      case Marker::ExpectError: Failures.push_back(fmt(M.Line, "expected failure " + M.Kind + " did not happen")); break;
      case Marker::KnownFP: Failures.push_back(fmt(M.Line, "XPASS known-fp " + M.Kind + " no longer reported; remove the marker")); break;
      case Marker::XFail: Notes.push_back(fmt(M.Line, "xfail, " + M.Kind + " still missing (known gap)")); break;
      }
    }

    unsigned Expect = 0, Gaps = 0;
    for (Marker &M : Marks) (M.What == Marker::XFail || M.What == Marker::KnownFP ? Gaps : Expect) += 1;
    ++FixturesRun;
    if (!Failures.empty()) ++FixturesFailed;
    llvm::outs() << (Failures.empty() ? "PASS " : "FAIL ") << llvm::sys::path::filename(File) << ": " << Expect
                 << " expectations, " << Gaps << " known gaps\n";
    for (auto &F : Failures) llvm::outs() << "  FAIL " << F << "\n";
    if (VfVerbose)
      for (auto &N : Notes) llvm::outs() << "  " << N << "\n";
  }
};
struct Act : ASTFrontendAction {
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &, StringRef) override {
    return std::make_unique<Consumer>();
  }
};
} // namespace

int main(int argc, const char **argv) {
  int RC = cfglab::runTool<Act>(argc, argv, Cat);
  if (RC) return RC;
  llvm::outs() << "ran " << FixturesRun << " fixtures, " << (FixturesRun - FixturesFailed) << " passed, "
               << FixturesFailed << " failed\n";
  return FixturesFailed ? 1 : 0;
}
