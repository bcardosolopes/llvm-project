// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -debug-info-kind=limited -std=c++20 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited -std=c++20 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that virtual thunks carry valid debug locations on all operations.
// When debug info is enabled, thunk functions get a DISubprogram, and the
// LLVM verifier requires all inlinable calls in such functions to have a
// debug location. Previously, thunk bodies used loc(unknown) which mapped
// to no debug location, causing the verifier to crash.

struct Base { virtual ~Base() {} };
struct Derived : virtual Base {};

void test() { Derived d; }

// CIR: cir.func {{.*}}@_ZTv0_n24_N7DerivedD1Ev
// CIR-NOT: loc(unknown)
// CIR: cir.call tail @_ZN7DerivedD1Ev
// CIR-NOT: loc(unknown)
// CIR: cir.return

// LLVM: define {{.*}}void @_ZTv0_n24_N7DerivedD1Ev({{.*}}) {{.*}}!dbg
// LLVM:   tail call void @_ZN7DerivedD1Ev(ptr {{.*}}){{.*}}, !dbg
// LLVM:   ret void, !dbg

// Verify thunk DISubprogram has DIFlagArtificial | DIFlagThunk and uses
// linkageName (not name) for the mangled symbol.
// LLVM-DAG: !DISubprogram(linkageName: "_ZTv0_n24_N7DerivedD1Ev", {{.*}}flags: DIFlagArtificial | DIFlagThunk
// LLVM-DAG: !DISubprogram(linkageName: "_ZTv0_n24_N7DerivedD0Ev", {{.*}}flags: DIFlagArtificial | DIFlagThunk
