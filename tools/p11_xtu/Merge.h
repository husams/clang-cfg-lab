// Merge.h -- merge the call graphs of several translation units by USR (Part 11.5).
//
// A ClangTool run over several files builds one clang::CallGraph per TU, and each is freed
// with its AST. A node is a declaration in *that* TU's AST: the declaration of b_fn that
// p11_xtu_a.cpp sees (no body: a callee-only node) and the definition in p11_xtu_b.cpp are
// two unrelated pointers. What they share is the USR, the stable string
// clang::index::generateUSRForDecl computes from the declaration's name, scope and
// signature (file-prefixed for internal linkage, so two `static void local()` stay apart).
//
//   p11xtu::Merged M;
//   M.addTU(cglab::Graph(CG, Ctx, {MainFileOnly = false}));     // once per TU, inside the action
//   M.finish();                                                // after ClangTool::run
//
// The per-TU facts the merge needs are a small table (TuData): the nodes with their keys and
// the edges with their call-site positions. addTU copies one out of a live graph;
// loadJson reads the same table back from a `p11_xtu --emit=json` file of ONE translation
// unit, so the merge can run later, without the compiler (the two-pass design of Part 11.7).
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

#ifndef CFGLAB_P11_MERGE_H
#define CFGLAB_P11_MERGE_H

#include "cglab.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace p11xtu {

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

// What the merge needs from one translation unit (see the header comment).
struct TuData {
  struct N {
    std::string Key, Name, Usr, DeclFile;
    bool Def = false, Noreturn = false, Static = false, Implicit = false;
  };
  struct E {
    size_t From, To; // indices into Nodes
    std::string Path;
    unsigned Line, Col;
  };
  std::string Tu; // the main file's name
  std::vector<N> Nodes;
  std::vector<E> Edges;
};

class Merged {
public:
  // Copy what the merge needs out of one TU's graph; the TU may be freed afterwards.
  void addTU(const cglab::Graph &G) {
    const SourceManager &SM = G.sm();
    TuData D;
    D.Tu = G.file();
    std::vector<size_t> Index(G.nodes().size(), 0); // graph node id -> index in D.Nodes
    for (const cglab::Node &N : G.nodes()) {
      if (N.Id == 0) continue; // <root>
      TuData::N R;
      R.Key = N.Usr.empty() ? blockKey(SM, N) : N.Usr;
      R.Name = N.Name;
      R.Usr = N.Usr;
      R.Def = N.K != cglab::Kind::Decl;
      R.Noreturn = N.Noreturn;
      R.Static = N.Static;
      R.Implicit = N.D->isImplicit();
      R.DeclFile = fileOf(SM, cglab::declLoc(N.Best));
      Index[N.Id] = D.Nodes.size();
      D.Nodes.push_back(std::move(R));
    }
    for (const cglab::Edge &E : G.edges()) {
      if (E.Root || !E.Site) continue;
      SourceLocation L = SM.getSpellingLoc(E.Site->getBeginLoc());
      D.Edges.push_back({Index[E.From], Index[E.To], SM.getFilename(L).str(), E.Line, E.Col});
    }
    add(D);
  }

  // Merge one translation unit's table. The first TU to mention a node names it; a definition
  // renames it (the printed name of the definition wins over a declaration's).
  void add(const TuData &D) {
    Tus.push_back(D.Tu);
    for (const TuData::N &N : D.Nodes) {
      Raw &R = ByKey[N.Key];
      if (R.N.Key.empty()) {
        R.N.Key = N.Key;
        R.N.Name = N.Name;
        R.N.Usr = N.Usr;
      }
      if (N.Def && !R.N.Def) R.N.Name = N.Name;
      R.N.Def |= N.Def;
      if (N.Def) R.N.DefTus.insert(D.Tu);
      R.N.SeenTus.insert(D.Tu);
      R.N.Noreturn |= N.Noreturn;
      R.N.Static |= N.Static;
      R.N.Implicit |= N.Implicit;
      if (!N.DeclFile.empty() && (R.N.DeclFile.empty() || N.DeclFile < R.N.DeclFile)) R.N.DeclFile = N.DeclFile;
    }
    for (const TuData::E &E : D.Edges) {
      EdgeKey K{D.Nodes[E.From].Key, D.Nodes[E.To].Key, E.Path, E.Line, E.Col};
      EdgeTus[K].insert(D.Tu);
    }
  }

  // Read the table back from a file written by `p11_xtu FILE --emit=json` (exactly one TU).
  // Returns false with a message in Err when the file is not such an export.
  bool loadJson(llvm::StringRef Path, std::string &Err) {
    auto Buf = llvm::MemoryBuffer::getFile(Path);
    if (!Buf) {
      Err = Path.str() + ": " + Buf.getError().message();
      return false;
    }
    auto Parsed = llvm::json::parse((*Buf)->getBuffer());
    if (!Parsed) {
      Err = Path.str() + ": " + llvm::toString(Parsed.takeError());
      return false;
    }
    auto Bad = [&](const char *What) {
      Err = Path.str() + ": not a one-TU `p11_xtu --emit=json` export (" + What + ")";
      return false;
    };
    const llvm::json::Object *O = Parsed->getAsObject();
    if (!O) return Bad("not an object");
    const llvm::json::Array *Tus = O->getArray("tus"), *Ns = O->getArray("nodes"), *Es = O->getArray("edges");
    if (!Tus || !Ns || !Es) return Bad("no tus, nodes or edges");
    if (Tus->size() != 1 || !(*Tus)[0].getAsString()) return Bad("it covers several translation units");
    TuData D;
    D.Tu = (*Tus)[0].getAsString()->str();
    std::map<std::string, size_t> ByName;
    for (const llvm::json::Value &V : *Ns) {
      const llvm::json::Object *J = V.getAsObject();
      if (!J || !J->getString("name") || !J->getString("key") || !J->getString("kind"))
        return Bad("a node lacks name, key or kind");
      TuData::N N;
      N.Name = J->getString("name")->str();
      N.Key = J->getString("key")->str();
      N.Usr = J->getString("usr").value_or("").str();
      N.Def = *J->getString("kind") == "def";
      N.Noreturn = J->getBoolean("noreturn").value_or(false);
      N.Static = J->getBoolean("static").value_or(false);
      N.Implicit = J->getBoolean("implicit").value_or(false);
      N.DeclFile = J->getString("declFile").value_or("").str();
      ByName[N.Name] = D.Nodes.size();
      D.Nodes.push_back(std::move(N));
    }
    for (const llvm::json::Value &V : *Es) {
      const llvm::json::Object *J = V.getAsObject();
      if (!J || !J->getString("from") || !J->getString("to") || !J->getInteger("line") || !J->getInteger("col"))
        return Bad("an edge lacks from, to, line or col");
      auto From = ByName.find(J->getString("from")->str()), To = ByName.find(J->getString("to")->str());
      if (From == ByName.end() || To == ByName.end()) return Bad("an edge names an unknown node");
      std::string EdgePath = J->getString("path").value_or(J->getString("file").value_or("")).str();
      D.Edges.push_back({From->second, To->second, EdgePath, static_cast<unsigned>(*J->getInteger("line")),
                         static_cast<unsigned>(*J->getInteger("col"))});
    }
    add(D);
    return true;
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

// `--load` turns the positional arguments into JSON files. ClangTool's option parser insists on
// source files, so the tool splits argv itself: Files gets the positional arguments, Rest the
// other options (without --load) for llvm::cl::ParseCommandLineOptions. Returns false when
// --load is absent (argv is then left for the normal path); anything after `--` is refused,
// because there is no compiler to pass it to.
inline bool splitLoadArgs(int argc, const char **argv, std::vector<std::string> &Files, std::vector<const char *> &Rest,
                          std::string &Err) {
  bool Load = false;
  for (int I = 1; I < argc; ++I)
    if (llvm::StringRef(argv[I]) == "--load") Load = true;
  if (!Load) return false;
  Rest.push_back(argv[0]);
  for (int I = 1; I < argc; ++I) {
    llvm::StringRef A = argv[I];
    if (A == "--load") continue;
    if (A == "--") {
      Err = "--load reads JSON tables: there is no compiler to pass `--` flags to";
      return true;
    }
    if (A.starts_with("-")) Rest.push_back(argv[I]);
    else Files.push_back(A.str());
  }
  return true;
}

} // namespace p11xtu

#endif // CFGLAB_P11_MERGE_H
