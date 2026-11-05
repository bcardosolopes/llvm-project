// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple armv7-none-linux-androideabi19 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// A noreturn full-expression (a temporary whose destructor is [[noreturn]],
// consumed through a non-void method + operator<< chain, the shape of
// a LOG(FATAL) macro) immediately followed by an explicit __builtin_unreachable() must
// generate valid CIR on the AArch32 C++ ABI, where destructors return 'this'.
//
// emitCall follows the noreturn destructor with cir.unreachable and creates a
// dead continuation block; on arm32 it also materializes an unused undef 'this'
// result into that block via getUndefRValue, and the full-expression cleanup
// restores the insertion point ahead of it. The following __builtin_unreachable()
// then emitted its cir.unreachable before that undef result, leaving an op after
// a terminator, so CIR verification failed with "'cir.unreachable' op must be
// the last operation in the parent block". emitUnreachable now emits the
// terminator at the end of the block, keeping cir.unreachable last.

struct PlanetCrashNote {
  ~PlanetCrashNote() __attribute__((noreturn));
  struct Stream {
    Stream &operator<<(const char *);
  };
  Stream &stream();
};

void f() {
  PlanetCrashNote().stream() << "boom";
  __builtin_unreachable();
}

// The noreturn destructor is followed by cir.unreachable; the explicit
// __builtin_unreachable() lands in the dead continuation block as that block's
// last (and only) op -- no unused undef 'this' result trails it.
// CIR-LABEL: cir.func{{.*}} @_Z1fv(
// CIR:         cir.call @_ZN15PlanetCrashNoteD1Ev({{.*}}) nothrow : (!cir.ptr<!rec_PlanetCrashNote>) -> !cir.ptr<!rec_PlanetCrashNote>
// CIR-NEXT:    cir.unreachable
// CIR-NEXT:  ^{{.*}}: // no predecessors
// CIR-NEXT:    cir.unreachable
// CIR-NEXT:  }

// LLVM-LABEL: define{{.*}} void @_Z1fv(
// LLVM:         call {{.*}}ptr @_ZN15PlanetCrashNoteD1Ev(
// LLVM-NEXT:    unreachable

// OGCG-LABEL: define{{.*}} void @_Z1fv(
// OGCG:         call {{.*}}ptr @_ZN15PlanetCrashNoteD1Ev(
// OGCG-NEXT:    unreachable
