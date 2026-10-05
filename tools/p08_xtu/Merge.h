// Merge.h -- merge the call graphs of several translation units by USR (Part 8.9).
//
// A ClangTool run over several files builds one clang::CallGraph per TU, and each is freed
// with its AST. A node is a declaration in *that* TU's AST: the declaration of b_fn that
// p08_xtu_a.cpp sees (no body: a callee-only node) and the definition in p08_xtu_b.cpp are
// two unrelated pointers. What they share is the USR, the stable string
// clang::index::generateUSRForDecl computes from the declaration's name, scope and
// signature (file-prefixed for internal linkage, so two `static void local()` stay apart).
//
//   p08xtu::Merged M;
//   M.addTU(cglab::Graph(CG, Ctx, {MainFileOnly = false}));     // once per TU, inside the action
//   M.finish();                                                // after ClangTool::run
//
// Merge rules
//   key         the USR; a block has none, so it is keyed by file:line:column
//   node        kind=def if any TU defines it, else decl; tus = the TUs that define it
//               (decl: the TUs that mention it); an inline function in a header is defined
//               in every TU that includes it and is still ONE node
//   name        the printed name; two nodes that print alike get "@<tu>" appended
//   edge        (caller, callee, call-site file, line, column), deduplicated across TUs;
//               xtu = the callee is defined, but not in some TU the edge was seen in: the
//               edge only resolves because of the merge
//   unresolved  a node no TU defines (declaration-only, implicit declarations left out)
//
// Everything is ordered by name, never by pointer.

#ifndef CFGLAB_P08_MERGE_H
#define CFGLAB_P08_MERGE_H

#include "cglab.h"

#include "llvm/Support/FileSystem.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace p08xtu {

using namespace clang;

// The path as the compiler saw it, made relative to the current directory when it is inside it
// (what `clang file.cpp` would print in a diagnostic).
inline std::string displayPath(llvm::StringRef Path) {
  llvm::SmallString<256> Cwd;
  if (!llvm::sys::fs::current_path(Cwd)) {
    std::string Prefix = Cwd.str().str() + "/";
    if (Path.starts_with(Prefix)) return Path.drop_front(Prefix.size()).str();
  }
  return Path.str();
}

struct Node {
  unsigned Id = 0;
  std::string Key, Name, Usr;
  bool Def = false, Noreturn = false, Static = false, Implicit = false;
  std::set<std::string> DefTus, SeenTus; // main-file names
  std::string DeclFile;                  // where a declaration-only node is declared
  unsigned Scc = 0;
  bool Recursive = false;
  const std::set<std::string> &tus() const { return Def ? DefTus : SeenTus; }
};

struct Edge {
  unsigned From = 0, To = 0;
  std::string File, Path; // call-site file: its name, and the path the compiler saw
  unsigned Line = 0, Col = 0;
  bool Xtu = false;
  bool Back = false; // its target is on the DFS stack: it closes a cycle
};

struct Scc {
  unsigned Id = 0;
  std::vector<unsigned> Members; // ascending (= name order)
  bool Cyclic = false, Self = false;
};

class Merged {
public:
  // Copy what the merge needs out of one TU's graph; the TU may be freed afterwards.
  void addTU(const cglab::Graph &G) {
    const SourceManager &SM = G.sm();
    std::string Tu = G.file();
    Tus.push_back(Tu);
    std::vector<std::string> Keys(G.nodes().size());
    for (const cglab::Node &N : G.nodes()) {
      if (N.Id == 0) continue; // <root>
      std::string Key = N.Usr.empty() ? blockKey(SM, N) : N.Usr;
      Keys[N.Id] = Key;
      Raw &R = ByKey[Key];
      bool Def = N.K != cglab::Kind::Decl;
      if (R.N.Key.empty()) {
        R.N.Key = Key;
        R.N.Name = N.Name;
        R.N.Usr = N.Usr;
      }
      if (Def && !R.N.Def) R.N.Name = N.Name;
      R.N.Def |= Def;
      if (Def) R.N.DefTus.insert(Tu);
      R.N.SeenTus.insert(Tu);
      R.N.Noreturn |= N.Noreturn;
      R.N.Static |= N.Static;
      R.N.Implicit |= N.D->isImplicit();
      std::string F = fileOf(SM, cglab::declLoc(N.Best));
      if (!F.empty() && (R.N.DeclFile.empty() || F < R.N.DeclFile)) R.N.DeclFile = F;
    }
    for (const cglab::Edge &E : G.edges()) {
      if (E.Root || !E.Site) continue;
      SourceLocation L = SM.getSpellingLoc(E.Site->getBeginLoc());
      EdgeKey K{Keys[E.From], Keys[E.To], SM.getFilename(L).str(), E.Line, E.Col};
      EdgeTus[K].insert(Tu);
    }
  }

  // Resolve names, number the nodes and edges, compute SCCs and cycle-closing edges.
  void finish() {
    std::map<std::string, std::vector<Raw *>> ByName;
    for (auto &[Key, R] : ByKey) ByName[R.N.Name].push_back(&R);
    for (auto &[Name, Rs] : ByName)
      if (Rs.size() > 1)
        for (Raw *R : Rs) R->N.Name += "@" + *R->N.tus().begin();
    for (auto &[Key, R] : ByKey) Nodes.push_back(R.N);
    std::sort(Nodes.begin(), Nodes.end(), [](const Node &A, const Node &B) { return A.Name < B.Name; });
    std::map<std::string, unsigned> Id;
    for (unsigned I = 0; I < Nodes.size(); ++I) {
      Nodes[I].Id = I;
      Id[Nodes[I].Key] = I;
    }
    for (auto &[K, SeenIn] : EdgeTus) {
      const auto &[From, To, Path, Line, Col] = K;
      Edge E;
      E.From = Id[From];
      E.To = Id[To];
      E.Path = Path;
      E.File = llvm::sys::path::filename(Path).str();
      E.Line = Line;
      E.Col = Col;
      const Node &Callee = Nodes[E.To];
      if (Callee.Def)
        for (const std::string &T : SeenIn)
          if (!Callee.DefTus.count(T)) E.Xtu = true;
      Edges.push_back(std::move(E));
    }
    std::sort(Edges.begin(), Edges.end(), [](const Edge &A, const Edge &B) {
      return std::tie(A.From, A.To, A.File, A.Line, A.Col) < std::tie(B.From, B.To, B.File, B.Line, B.Col);
    });
    Out.assign(Nodes.size(), {});
    for (unsigned I = 0; I < Edges.size(); ++I) Out[Edges[I].From].push_back(I);
    computeSccs();
    markBackEdges();
  }

  const std::vector<std::string> &tus() const { return Tus; }
  const std::vector<Node> &nodes() const { return Nodes; }
  const std::vector<Edge> &edges() const { return Edges; }
  const std::vector<Scc> &sccs() const { return Sccs; }
  const Node &node(unsigned Id) const { return Nodes[Id]; }
  // Indices into edges() of the edges leaving Id, ordered by callee name then call site.
  const std::vector<unsigned> &outEdges(unsigned Id) const { return Out[Id]; }

private:
  struct Raw {
    Node N;
  };
  using EdgeKey = std::tuple<std::string, std::string, std::string, unsigned, unsigned>;

  static std::string fileOf(const SourceManager &SM, SourceLocation L) {
    if (L.isInvalid()) return "";
    return llvm::sys::path::filename(SM.getFilename(SM.getSpellingLoc(L))).str();
  }
  static std::string blockKey(const SourceManager &SM, const cglab::Node &N) {
    return "block:" + fileOf(SM, cglab::declLoc(N.Best)) + ":" + std::to_string(N.Line) + ":" +
           std::to_string(N.Col);
  }

  // Tarjan; roots and edges are taken in name order, so the numbering is the same every run
  // and, like llvm::scc_iterator's, callees come before callers.
  void computeSccs() {
    std::vector<int> Index(Nodes.size(), -1), Low(Nodes.size(), 0);
    std::vector<char> OnStack(Nodes.size(), 0);
    std::vector<unsigned> Stack;
    int Counter = 0;
    std::function<void(unsigned)> Strong = [&](unsigned V) {
      Index[V] = Low[V] = Counter++;
      Stack.push_back(V);
      OnStack[V] = 1;
      for (unsigned Ei : Out[V]) {
        unsigned W = Edges[Ei].To;
        if (Index[W] < 0) {
          Strong(W);
          Low[V] = std::min(Low[V], Low[W]);
        } else if (OnStack[W]) {
          Low[V] = std::min(Low[V], Index[W]);
        }
      }
      if (Low[V] != Index[V]) return;
      Scc S;
      S.Id = Sccs.size();
      for (;;) {
        unsigned W = Stack.back();
        Stack.pop_back();
        OnStack[W] = 0;
        S.Members.push_back(W);
        if (W == V) break;
      }
      std::sort(S.Members.begin(), S.Members.end());
      bool SelfEdge = false;
      for (unsigned Ei : Out[V]) SelfEdge |= Edges[Ei].To == V;
      S.Self = S.Members.size() == 1 && SelfEdge;
      S.Cyclic = S.Members.size() > 1 || S.Self;
      for (unsigned M : S.Members) {
        Nodes[M].Scc = S.Id;
        Nodes[M].Recursive = S.Cyclic;
      }
      Sccs.push_back(std::move(S));
    };
    for (unsigned V = 0; V < Nodes.size(); ++V)
      if (Index[V] < 0) Strong(V);
  }

  // DFS from every node in name order: an edge into a node still on the stack closes a cycle.
  void markBackEdges() {
    std::vector<char> Color(Nodes.size(), 0); // 0 white, 1 on stack, 2 done
    std::function<void(unsigned)> Dfs = [&](unsigned V) {
      Color[V] = 1;
      for (unsigned Ei : Out[V]) {
        Edge &E = Edges[Ei];
        if (Color[E.To] == 1) E.Back = true;
        else if (Color[E.To] == 0) Dfs(E.To);
      }
      Color[V] = 2;
    };
    for (unsigned V = 0; V < Nodes.size(); ++V)
      if (Color[V] == 0) Dfs(V);
  }

  std::vector<std::string> Tus;
  std::map<std::string, Raw> ByKey;
  std::map<EdgeKey, std::set<std::string>> EdgeTus;
  std::vector<Node> Nodes;
  std::vector<Edge> Edges;
  std::vector<std::vector<unsigned>> Out;
  std::vector<Scc> Sccs;
};

} // namespace p08xtu

#endif // CFGLAB_P08_MERGE_H
