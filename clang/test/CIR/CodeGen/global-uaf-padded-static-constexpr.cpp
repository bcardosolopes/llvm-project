// Regression test for a heap-use-after-free in
// CIRGenModule::getOrCreateCIRGlobal that fired when a static constexpr
// data member of a class with trailing padding was emitted.
//
// The path was:
//
//   getOrCreateCIRGlobal(VarDecl*, ...)
//     -> getOrCreateCIRGlobal(StringRef, ...)        // creates `gv`
//       -> [available_externally const path: init type != gv.getSymType()]
//          gv.erase();
//          getOrCreateCIRGlobal(VarDecl*, initType, NotForDefinition)
//            -> getOrCreateCIRGlobal(StringRef, ...)
//              -> getGlobalValue(mangledName)        // returns the FREED gv
//              -> dyn_cast<cir::GlobalOp>(v)         // UAF read in
//                                                    // mlir::Operation::getName
//
// Root cause: gv.erase() did not invalidate the symbol-lookup cache, so the
// recursive call resurrected the freed pointer. Fixed by calling
// eraseGlobalSymbol(gv) and clearing lastGlobalOp before gv.erase().
//
// In Release / NDEBUG builds the same UAF surfaces downstream as a SEGV
// inside MLIRContext::getTypeUniquer when a stale type chains a corrupted
// MLIRContext pointer through Type::getContext.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -emit-cir -std=c++11 -O0 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

namespace ns {

enum class Lock : char { NO_LOCK };
enum class Comp : char { NONE };

class Inner {
public:
  static constexpr long FLAG_R = 1L << 0;
  constexpr Inner() {}
  constexpr Inner(long f) : flags(f) {}
private:
  long flags = 0;
  Lock lock = Lock::NO_LOCK;
  Comp comp = Comp::NONE;
};

class Outer {
public:
  static constexpr Inner FLAG_R = Inner(Inner::FLAG_R);
};

} // namespace ns

const ns::Inner &use_r() { return ns::Outer::FLAG_R; }

// CHECK: cir.global {{.*}} @_ZN2ns5Outer6FLAG_RE
// CHECK: cir.func {{.*}} @_Z5use_rv
