// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// A temporary whose destructor is [[noreturn]] must generate valid CIR on the
// AArch32 C++ ABI, where destructors return 'this'. emitCall follows the
// noreturn call with cir.unreachable and creates a dead continuation block; on
// arm32 it also materializes an undef 'this' result into that block, so the
// block is not empty. The enclosing cir.scope's dead-continuation-block cleanup
// must still drop it, otherwise the scope's last block is left unterminated and
// CIR verification fails with "'cir.scope' op last block of cir.scope must be
// terminated". This is the shape of a
// logging CHECK(false) macro on armv7.

struct Fatal {
  ~Fatal() __attribute__((noreturn));
};

void f() { Fatal(); }

// The AArch32 dtor returns 'this' (non-void), followed by cir.unreachable with
// no trailing unterminated block.
// CIR-LABEL: cir.func{{.*}} @_Z1fv(
// CIR:         cir.scope {
// CIR:           cir.call @_ZN5FatalD1Ev({{.*}}) nothrow : (!cir.ptr<!rec_Fatal>) -> !cir.ptr<!rec_Fatal>
// CIR-NEXT:      cir.unreachable
// CIR-NEXT:    }
// CIR:         cir.return

// LLVM-LABEL: define{{.*}} void @_Z1fv(
// LLVM:         call {{.*}}ptr @_ZN5FatalD1Ev(
// LLVM:         unreachable

// OGCG-LABEL: define{{.*}} void @_Z1fv(
// OGCG:         call {{.*}}ptr @_ZN5FatalD1Ev(
// OGCG:         unreachable
