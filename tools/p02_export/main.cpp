// p02_export -- write a CFG as JSON or Graphviz DOT from your own walker (Part 2.8).
//
//   p02_export <file> --format=json|dot [--outdir=DIR] [--func=NAME]
//                     [--preset=..] [--set=..] [--clear=..] [--always-add=..]
//
// Without --outdir the document goes to stdout (use --func to pick one
// function). With --outdir each function is written to
//   DIR/<file-stem>.<function>.json|dot
//
// Why not CFG::viewCFG? Its DOTGraphTraits<const CFG*> specialization is
// private to CFG.cpp, so a tool that wants a DOT file has to emit its own.

#include "cfglab.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/Path.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p02_export options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)
static llvm::cl::opt<std::string> Format("format", llvm::cl::init("json"), llvm::cl::cat(Cat),
                                         llvm::cl::desc("json | dot"));
static llvm::cl::opt<std::string> OutDir("outdir", llvm::cl::cat(Cat),
                                         llvm::cl::desc("write one file per function here"));

// Role of the i-th successor edge, from the terminator kind. For every
// conditional terminator: successor 0 is the *true* edge, 1 the *false* edge.
static std::string edgeRole(const CFGBlock *B, unsigned I) {
  const Stmt *T = B->getTerminatorStmt();
  if (!T) return "";
  if (isa<SwitchStmt>(T)) return "switch";
  if (isa<CXXTryStmt>(T)) return I == 0 ? "handler" : "unwind";
  if (isa<IfStmt>(T) || isa<ConditionalOperator>(T) || isa<BinaryConditionalOperator>(T) ||
      isa<BinaryOperator>(T) || isa<WhileStmt>(T) || isa<ForStmt>(T) ||
      isa<CXXForRangeStmt>(T) || isa<DoStmt>(T))
    return I == 0 ? "T" : "F";
  return ""; // break, continue, goto, return: unconditional
}

static llvm::json::Value toJson(const FunctionDecl *FD, const CFG &G, ASTContext &Ctx) {
  const SourceManager &SM = Ctx.getSourceManager();
  llvm::json::Array Blocks;
  for (const CFGBlock *B : G) {
    llvm::json::Array Elems;
    for (const CFGElement &E : *B) {
      llvm::json::Object O{{"kind", cfglab::kindName(E.getKind())}};
      if (auto S = E.getAs<CFGStmt>()) {
        O["class"] = S->getStmt()->getStmtClassName();
        O["text"] = cfglab::stmtText(S->getStmt(), Ctx);
        O["line"] = cfglab::lineOf(SM, S->getStmt()->getBeginLoc());
      } else {
        O["detail"] = cfglab::elementDetail(E, Ctx);
      }
      Elems.push_back(std::move(O));
    }
    llvm::json::Array Succs;
    unsigned I = 0;
    for (const CFGBlock::AdjacentBlock &S : B->succs()) {
      llvm::json::Object O{{"reachable", S.isReachable()}, {"role", edgeRole(B, I++)}};
      // reachable edge: the block is in getReachableBlock(); pruned edge: it
      // survives (sometimes) in getPossiblyUnreachableBlock(); else both null.
      const CFGBlock *To = S.isReachable() ? S.getReachableBlock() : S.getPossiblyUnreachableBlock();
      if (To) O["to"] = To->getBlockID();
      Succs.push_back(std::move(O));
    }
    llvm::json::Array Preds;
    for (const CFGBlock::AdjacentBlock &P : B->preds())
      if (P.isReachable()) Preds.push_back(P->getBlockID());

    llvm::json::Object Blk{{"id", B->getBlockID()},
                           {"elements", std::move(Elems)},
                           {"succs", std::move(Succs)},
                           {"preds", std::move(Preds)},
                           {"noreturn", B->hasNoReturnElement()}};
    if (const Stmt *T = B->getTerminatorStmt()) {
      std::string Text;
      llvm::raw_string_ostream OS(Text);
      B->printTerminator(OS, Ctx.getLangOpts());
      Blk["terminator"] = llvm::json::Object{{"kind", cfglab::termKindName(B->getTerminator())},
                                             {"stmt", T->getStmtClassName()},
                                             {"line", cfglab::lineOf(SM, T->getBeginLoc())},
                                             {"text", OS.str()}};
    }
    if (const Stmt *L = B->getLabel()) Blk["label"] = L->getStmtClassName();
    if (const Stmt *L = B->getLoopTarget()) Blk["loopTarget"] = L->getStmtClassName();
    Blocks.push_back(std::move(Blk));
  }
  return llvm::json::Object{{"function", FD->getQualifiedNameAsString()},
                            {"entry", G.getEntry().getBlockID()},
                            {"exit", G.getExit().getBlockID()},
                            {"blocks", std::move(Blocks)}};
}

// DOT: one record node per block, one edge per successor. Reachability and
// roles become edge styles; the Entry and Exit blocks get colours.
static std::string dotEscape(const std::string &S) {
  std::string R;
  for (char C : S) {
    if (C == '"' || C == '<' || C == '>' || C == '{' || C == '}' || C == '|' || C == '\\')
      R += '\\';
    if (C == '\n') { R += "\\l"; continue; }
    R += C;
  }
  return R;
}

static void toDot(const FunctionDecl *FD, const CFG &G, ASTContext &Ctx, llvm::raw_ostream &OS) {
  OS << "digraph \"" << FD->getQualifiedNameAsString() << "\" {\n";
  OS << "  node [shape=record, fontname=\"Menlo\", fontsize=10];\n";
  OS << "  edge [fontname=\"Menlo\", fontsize=9];\n";
  for (const CFGBlock *B : G) {
    std::string Title = "B" + std::to_string(B->getBlockID());
    if (B == &G.getEntry()) Title += " (ENTRY)";
    if (B == &G.getExit()) Title += " (EXIT)";
    if (const Stmt *L = B->getLabel()) Title += "  " + std::string(L->getStmtClassName());
    OS << "  B" << B->getBlockID() << " [label=\"{" << dotEscape(Title);
    unsigned I = 0;
    for (const CFGElement &E : *B) {
      std::string Line;
      if (auto S = E.getAs<CFGStmt>())
        Line = std::string(S->getStmt()->getStmtClassName()) + " " + cfglab::stmtText(S->getStmt(), Ctx);
      else
        Line = std::string(cfglab::kindName(E.getKind())) + " " + cfglab::elementDetail(E, Ctx);
      OS << "\\l" << I++ << ": " << dotEscape(Line);
    }
    if (B->getTerminatorStmt()) {
      std::string Text;
      llvm::raw_string_ostream TS(Text);
      B->printTerminator(TS, Ctx.getLangOpts());
      OS << "\\lT: " << dotEscape(TS.str());
    }
    OS << "\\l}\"";
    if (B == &G.getEntry()) OS << ", style=filled, fillcolor=\"#d5e8d4\"";
    else if (B == &G.getExit()) OS << ", style=filled, fillcolor=\"#f8cecc\"";
    else if (B->getLoopTarget()) OS << ", style=dashed";
    OS << "];\n";
  }
  for (const CFGBlock *B : G) {
    unsigned I = 0;
    for (const CFGBlock::AdjacentBlock &S : B->succs()) {
      std::string Role = edgeRole(B, I++);
      const CFGBlock *To = S.isReachable() ? S.getReachableBlock() : S.getPossiblyUnreachableBlock();
      if (!To) continue; // pruned edge with no recorded target: nothing to draw
      std::string Attrs;
      if (!Role.empty()) Attrs += "label=\"" + Role + "\" ";
      if (!S.isReachable()) Attrs += "style=dashed color=gray ";
      else if (Role == "T") Attrs += "color=\"#2e7d32\" ";
      else if (Role == "F") Attrs += "color=\"#c62828\" ";
      OS << "  B" << B->getBlockID() << " -> B" << To->getBlockID();
      if (!Attrs.empty()) OS << " [" << Attrs.substr(0, Attrs.size() - 1) << "]";
      OS << ";\n";
    }
  }
  OS << "}\n";
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;

        std::string Doc;
        llvm::raw_string_ostream DS(Doc);
        if (Format == "json") {
          DS << llvm::formatv("{0:2}", toJson(FD, *G, Ctx)) << "\n";
        } else if (Format == "dot") {
          toDot(FD, *G, Ctx, DS);
        } else {
          llvm::errs() << "--format must be json or dot\n";
          std::exit(2);
        }

        if (OutDir.empty()) {
          llvm::outs() << DS.str();
          return;
        }
        llvm::sys::fs::create_directories(OutDir);
        const SourceManager &SM = Ctx.getSourceManager();
        std::string Stem = llvm::sys::path::stem(SM.getFileEntryRefForID(SM.getMainFileID())->getName()).str();
        std::string Name = FD->getQualifiedNameAsString();
        for (char &C : Name) if (!isalnum((unsigned char)C) && C != '_') C = '_';
        std::string Path = (llvm::Twine(OutDir) + "/" + Stem + "." + Name + "." + Format).str();
        std::error_code EC;
        llvm::raw_fd_ostream Out(Path, EC);
        if (EC) { llvm::errs() << Path << ": " << EC.message() << "\n"; return; }
        Out << DS.str();
        llvm::outs() << Path << "\n";
      });
}
