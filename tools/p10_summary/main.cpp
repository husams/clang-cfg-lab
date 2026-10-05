// p10_summary -- bottom-up summaries over the call graph's SCCs (Part 10.3-10.4).
//
//   p10_summary <file> [--prop=sink|depth] [--sink=NAME] [--trace] [--func=NAME]
//                      [--inf=scc|reach] [--edges] [--emit=text|dot|json] [--all-files]
//
// Components are processed callee-first (llvm::scc_iterator); a cyclic component is
// iterated to a fixed point. Text output, one block per component in processing order
// (--sort is accepted like in every call-graph tool but does not apply to the text):
//   scc <id> [cyclic]: <members...> iter=<k>
//   trace scc <id> iter <k>: <name>=<value> ...     (--trace; cyclic components)
//   summary <name>: sink=none|may|always [via <callee>]
//   summary <name>: depth=<n>|inf [via <callee>]
//   edge <caller> -> <callee>                      (--edges, after the blocks; both ends printed above)
// The ids are the ones p09_walk prints. The algorithm lives in Summary.h.

#include "cglab.h"
#include "Summary.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p10_summary options");
CGLAB_DEFINE_COMMON_FLAGS(Cat) // --sort --emit --with-root --all-files
static llvm::cl::opt<std::string> PropOpt("prop", llvm::cl::init("sink"), llvm::cl::cat(Cat),
                                          llvm::cl::desc("sink | depth"));
static llvm::cl::opt<std::string> SinkOpt("sink", llvm::cl::cat(Cat),
                                          llvm::cl::desc("the function that is the sink (default: every [[noreturn]] function)"));
static llvm::cl::opt<bool> TraceOpt("trace", llvm::cl::cat(Cat),
                                    llvm::cl::desc("print every pass of every cyclic component"));
static llvm::cl::opt<std::string> FuncOpt("func", llvm::cl::cat(Cat),
                                          llvm::cl::desc("only the component of this function"));
static llvm::cl::opt<bool> EdgesOpt("edges", llvm::cl::cat(Cat),
                                    llvm::cl::desc("also print the call edges between the printed functions"));
static llvm::cl::opt<std::string> InfOpt("inf", llvm::cl::init("scc"), llvm::cl::cat(Cat),
                                         llvm::cl::desc("depth: scc = only cyclic members are inf, reach = their callers too"));

namespace {

int Status = 0;

std::string valueText(const p10::Summary &S, bool Depth) {
  return Depth ? p10::depthText(S.Depth) : p10::levelName(S.Sink);
}

// One printed component: the members that are in the graph, in name order.
struct Block {
  unsigned Id;
  const p10::SccRun *Run;
  std::vector<std::pair<unsigned, size_t>> Members; // (graph id, index in Run->Members)
};

void run(ASTContext &Ctx, CallGraph &CG) {
  cglab::SortKey Sort;
  cglab::EmitKind Emit;
  if (!cgCommonFlags(Sort, Emit)) { Status = 2; return; }
  if (PropOpt != "sink" && PropOpt != "depth") {
    llvm::errs() << "--prop must be sink or depth\n";
    Status = 2;
    return;
  }
  if (InfOpt != "scc" && InfOpt != "reach") {
    llvm::errs() << "--inf must be scc or reach\n";
    Status = 2;
    return;
  }
  cglab::Graph G(CG, Ctx, cgGraphOptions());

  p10::Options O;
  O.P = PropOpt == "depth" ? p10::Prop::Depth : p10::Prop::Sink;
  O.SinkName = SinkOpt;
  O.InfReaches = InfOpt == "reach";
  O.Trace = true; // cheap, and the json export carries the passes
  p10::Result R = p10::analyze(CG, Ctx, O);
  bool Depth = O.P == p10::Prop::Depth;
  const char *Key = Depth ? "depth=" : "sink=";

  std::optional<unsigned> Only;
  if (!FuncOpt.empty()) {
    Only = G.find(llvm::StringRef(FuncOpt));
    if (!Only) {
      llvm::errs() << "no function named '" << FuncOpt << "' in the call graph\n";
      Status = 2;
      return;
    }
  }

  std::vector<Block> Blocks;
  for (const p10::SccRun &S : R.Sccs) {
    Block B{0, &S, {}};
    for (size_t I = 0; I < S.Members.size(); ++I)
      if (auto Id = G.find(S.Members[I])) B.Members.push_back({*Id, I});
    if (B.Members.empty()) continue;
    std::sort(B.Members.begin(), B.Members.end());
    B.Id = G.node(B.Members.front().first).Scc;
    if (Only && G.node(*Only).Scc != B.Id) continue;
    Blocks.push_back(std::move(B));
  }

  auto viaName = [&](const p10::Summary &V) -> std::string {
    if (!V.Via) return "";
    if (auto Id = G.find(V.Via)) return G.name(*Id);
    return "";
  };

  std::set<unsigned> Shown;
  for (const Block &B : Blocks)
    for (auto &M : B.Members) Shown.insert(M.first);

  if (Emit == cglab::EmitKind::Text) {
    for (const Block &B : Blocks) {
      llvm::outs() << "scc " << B.Id << (B.Run->Cyclic ? " cyclic" : "") << ":";
      for (auto &M : B.Members) llvm::outs() << " " << G.name(M.first);
      llvm::outs() << " iter=" << B.Run->Iter << "\n";
      if (TraceOpt && B.Run->Cyclic)
        for (size_t K = 0; K < B.Run->Passes.size(); ++K) {
          llvm::outs() << "trace scc " << B.Id << " iter " << K + 1 << ":";
          for (auto &M : B.Members)
            llvm::outs() << " " << G.name(M.first) << "=" << valueText(B.Run->Passes[K][M.second], Depth);
          llvm::outs() << "\n";
        }
      for (auto &M : B.Members) {
        const p10::Summary &V = R.of(B.Run->Members[M.second]);
        llvm::outs() << "summary " << G.name(M.first) << ": " << Key << valueText(V, Depth);
        if (!viaName(V).empty()) llvm::outs() << " via " << viaName(V);
        llvm::outs() << "\n";
      }
    }
    if (EdgesOpt) {
      // one line per distinct caller/callee pair among the printed functions
      for (unsigned Id : G.order(Sort)) {
        if (!Shown.count(Id)) continue;
        std::set<unsigned> Done;
        for (unsigned Ei : G.outEdges(Id, Sort))
          if (Shown.count(G.edge(Ei).To) && Done.insert(G.edge(Ei).To).second)
            cglab::printEdgeLine(llvm::outs(), G, G.edge(Ei), false, false);
      }
    }
    return;
  }

  auto summaryOf = [&](const cglab::Node &N) -> const p10::Summary * {
    return N.CGN && R.Fn.count(N.CGN) ? &R.of(N.CGN) : nullptr;
  };

  if (Emit == cglab::EmitKind::Dot) {
    cglab::DotOptions D;
    D.SccClusters = true;
    D.Sort = Sort;
    D.WithRoot = false;
    D.Keep = [&](const cglab::Node &N) { return Shown.count(N.Id) > 0; };
    std::map<unsigned, unsigned> IterOf;
    for (const Block &B : Blocks) IterOf[B.Id] = B.Run->Iter;
    D.ClusterLabel = [&](const cglab::Scc &S) {
      return "scc " + std::to_string(S.Id) + " iter=" + std::to_string(IterOf[S.Id]);
    };
    D.NodeHook = [&](const cglab::Node &N, cglab::DotAttrs &A) {
      const p10::Summary *V = summaryOf(N);
      if (!V) return;
      A.Label = N.Name + "\n" + Key + valueText(*V, Depth);
      if (!Depth && p10::isSink(N.CGN, O)) {
        A.addClass("sink");
        A.dropClass("dim"); // a declared-only sink is still the point of the picture
      } else if (!Depth && V->Sink != p10::SinkLevel::None) {
        A.addClass("hl");
      }
      if (!viaName(*V).empty()) A.Tooltip = std::string(Key) + valueText(*V, Depth) + " via " + viaName(*V);
    };
    cglab::writeDot(llvm::outs(), G, D);
    return;
  }

  cglab::JsonOptions J;
  J.Sort = Sort;
  J.Keep = [&](const cglab::Node &N) { return Shown.count(N.Id) > 0; };
  J.NodeHook = [&](const cglab::Node &N, llvm::json::Object &Obj) {
    const p10::Summary *V = summaryOf(N);
    if (!V) return;
    Obj["property"] = Depth ? "depth" : "sink";
    Obj["value"] = valueText(*V, Depth);
    Obj["via"] = viaName(*V);
  };
  llvm::json::Object Out = cglab::toJson(G, J);
  // The components with what the fixed point did: iter and one row per pass.
  llvm::json::Array Sccs;
  for (const Block &B : Blocks) {
    llvm::json::Array Members, Passes;
    for (auto &M : B.Members) Members.push_back(G.node(M.first).Name);
    for (const auto &Pass : B.Run->Passes) {
      llvm::json::Object Row;
      for (auto &M : B.Members) Row[G.node(M.first).Name] = valueText(Pass[M.second], Depth);
      Passes.push_back(std::move(Row));
    }
    Sccs.push_back(llvm::json::Object{{"cyclic", B.Run->Cyclic},
                                      {"id", B.Id},
                                      {"iter", B.Run->Iter},
                                      {"members", std::move(Members)},
                                      {"passes", std::move(Passes)}});
  }
  Out["sccs"] = std::move(Sccs);
  llvm::outs() << llvm::formatv("{0:2}", llvm::json::Value(std::move(Out))) << "\n";
}

} // namespace

int main(int argc, const char **argv) {
  int RC = cglab::runPerTU(argc, argv, Cat, run);
  return RC ? RC : Status;
}
