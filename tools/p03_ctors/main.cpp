// p03_ctors -- constructors and their ConstructionContext (Part 3.3).
//
//   p03_ctors <file> [--func=NAME] [--preset=..] [--set=..] [--clear=..]
//
// Builds the CFG with AddInitializers, AddRichCXXConstructors (and, unless you --clear it,
// MarkElidedCXXConstructors) on top of the chosen preset (default: "default"),
// then prints one line per CFGConstructor / CFGCXXRecordTypedCall:
//
//   B1.3 Constructor        V(1 args)         ctx=SimpleVariable  decl=`V v(1)`

#include "cfglab.h"

using namespace clang;

static llvm::cl::OptionCategory Cat("p03_ctors options");
CFGLAB_DEFINE_BUILD_FLAGS(Cat)

static const char *ccKindName(ConstructionContext::Kind K) {
  switch (K) {
#define CFGLAB_CC(x) case ConstructionContext::x##Kind: return #x;
    CFGLAB_CC(SimpleVariable) CFGLAB_CC(CXX17ElidedCopyVariable)
    CFGLAB_CC(SimpleConstructorInitializer) CFGLAB_CC(CXX17ElidedCopyConstructorInitializer)
    CFGLAB_CC(NewAllocatedObject) CFGLAB_CC(SimpleTemporaryObject) CFGLAB_CC(ElidedTemporaryObject)
    CFGLAB_CC(SimpleReturnedValue) CFGLAB_CC(CXX17ElidedCopyReturnedValue)
    CFGLAB_CC(Argument) CFGLAB_CC(LambdaCapture)
#undef CFGLAB_CC
  }
  return "?";
}

// Type spelling without the (path-bearing) name of a lambda closure type.
static std::string typeName(QualType Ty) {
  if (const CXXRecordDecl *RD = Ty->getAsCXXRecordDecl(); RD && RD->isLambda()) return "<lambda>";
  return Ty.getAsString();
}

static std::string ctxDetail(const ConstructionContext *CC, ASTContext &Ctx) {
  std::string S;
  llvm::raw_string_ostream OS(S);
  auto T = [&](const Stmt *X) { return "`" + cfglab::stmtText(X, Ctx, 40) + "`"; };
  OS << "ctx=" << ccKindName(CC->getKind());
  // Each abstract family has one accessor that names the construction site.
  if (auto *V = dyn_cast<VariableConstructionContext>(CC))
    OS << " decl=" << T(V->getDeclStmt());
  if (auto *I = dyn_cast<ConstructorInitializerConstructionContext>(CC)) {
    const CXXCtorInitializer *CI = I->getCXXCtorInitializer();
    OS << " init=" << (CI->isAnyMemberInitializer() ? "member " + CI->getAnyMember()->getName().str()
                                                    : std::string("base"));
  }
  if (auto *N = dyn_cast<NewAllocatedObjectConstructionContext>(CC))
    OS << " new=" << T(N->getCXXNewExpr());
  if (auto *Tm = dyn_cast<TemporaryObjectConstructionContext>(CC)) {
    OS << " bte=" << (Tm->getCXXBindTemporaryExpr() ? "yes" : "no")
       << " mte=" << (Tm->getMaterializedTemporaryExpr() ? "yes" : "no");
    if (auto *E = dyn_cast<ElidedTemporaryObjectConstructionContext>(CC))
      OS << " elided-ctor=" << typeName(E->getConstructorAfterElision()->getType())
         << " then{" << ctxDetail(E->getConstructionContextAfterElision(), Ctx) << "}";
  }
  if (auto *R = dyn_cast<ReturnedValueConstructionContext>(CC))
    OS << " return=" << T(R->getReturnStmt());
  if (auto *A = dyn_cast<ArgumentConstructionContext>(CC))
    OS << " call=" << T(A->getCallLikeExpr()) << " index=" << A->getIndex();
  if (auto *L = dyn_cast<LambdaCaptureConstructionContext>(CC))
    OS << " field-type=" << L->getFieldDecl()->getType().getAsString() << " index=" << L->getIndex();
  // Kinds that carry a CXXBindTemporaryExpr of their own (C++17 elided copies).
  if (auto *E = dyn_cast<CXX17ElidedCopyVariableConstructionContext>(CC))
    OS << " bte=" << (E->getCXXBindTemporaryExpr() ? "yes" : "no");
  if (auto *E = dyn_cast<CXX17ElidedCopyConstructorInitializerConstructionContext>(CC))
    OS << " bte=" << (E->getCXXBindTemporaryExpr() ? "yes" : "no");
  if (auto *E = dyn_cast<CXX17ElidedCopyReturnedValueConstructionContext>(CC))
    OS << " bte=" << (E->getCXXBindTemporaryExpr() ? "yes" : "no");
  return OS.str();
}

int main(int argc, const char **argv) {
  return cfglab::runPerFunction(
      argc, argv, Cat, [](const FunctionDecl *FD, ASTContext &Ctx, Preprocessor &) {
        if (!flagWantsFunction(FD)) return;
        CFG::BuildOptions BO;
        if (!flagsToOptions(BO)) std::exit(2);
        BO.AddInitializers = true; // constructor-initializer contexts need CFGInitializer
        BO.AddRichCXXConstructors = true;
        BO.MarkElidedCXXConstructors = true;
        // --clear still wins for the Mark flag, so the doc can show the difference.
        for (const std::string &L : ClearFlag) cfglab::setOptionList(BO, L, false);
        if (!BO.AddRichCXXConstructors) BO.MarkElidedCXXConstructors = false;
        std::unique_ptr<CFG> G = CFG::buildCFG(FD, FD->getBody(), &Ctx, BO);
        if (!G) return;
        llvm::outs() << "== " << FD->getQualifiedNameAsString() << "\n";
        for (const CFGBlock *B : *G) {
          unsigned I = 0;
          for (const CFGElement &E : *B) {
            ++I;
            const ConstructionContext *CC = nullptr;
            std::string What;
            if (auto C = E.getAs<CFGConstructor>()) {
              CC = C->getConstructionContext();
              const auto *CE = cast<CXXConstructExpr>(C->getStmt());
              What = "ctor " + typeName(CE->getType()) + "(" + std::to_string(CE->getNumArgs()) +
                     " args" + (CE->isElidable() ? ", elidable" : "") + ")";
            } else if (auto C = E.getAs<CFGCXXRecordTypedCall>()) {
              CC = C->getConstructionContext();
              What = "call `" + cfglab::stmtText(C->getStmt(), Ctx, 24) + "`";
            } else
              continue;
            llvm::outs() << cfglab::blockName(B) << "." << I << " " << llvm::format("%-18s", cfglab::kindName(E.getKind()))
                         << " " << What << "  " << ctxDetail(CC, Ctx) << "\n";
          }
        }
      });
}
