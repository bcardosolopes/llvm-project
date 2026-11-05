// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Test that statement attributes (noinline, nomerge, always_inline, musttail)
// don't crash CIR codegen.

void foo();

void test_noinline() {
    [[clang::noinline]] foo();
}

void test_nomerge() {
    [[clang::nomerge]] foo();
}

void test_always_inline() {
    [[clang::always_inline]] foo();
}

int bar(int x);

int test_musttail(int x) {
    [[clang::musttail]] return bar(x);
}

// Musttail with aggregate (sret) return: verify no dead load between
// the musttail call and return after call convention lowering.
struct HasNonTrivialCopyConstructor {
  HasNonTrivialCopyConstructor(const HasNonTrivialCopyConstructor &);
};
HasNonTrivialCopyConstructor ReturnsClassByValue();
HasNonTrivialCopyConstructor test_musttail_aggregate() {
  [[clang::musttail]] return ReturnsClassByValue();
}

// Musttail with member function call (scalar return).
struct Foo {
  int MemberFunction(int x);
  int CallerMember(int x);
};
int Foo::CallerMember(int x) {
  [[clang::musttail]] return MemberFunction(x);
}

// Musttail with complex return (Direct ABI coercion: complex -> {double, double}).
double _Complex complex_callee(signed short P0);
double _Complex test_musttail_complex(signed short P0) {
  signed short P1 = 20391;
  [[clang::musttail]] return complex_callee(P1);
}

// Musttail with _BitInt return (Direct ABI coercion: _BitInt(100) -> {i64, i64}).
signed _BitInt(100) bitint_callee(float P0, double P1);
signed _BitInt(100) test_musttail_bitint(float P0, double P1) {
  [[clang::musttail]] return bitint_callee(P0, P1);
}

// Musttail with indirect call and sret return: the musttail call's sret
// pointer should be the function's own sret parameter, with matching ABI
// attributes.
struct ReallyBig { int a[100]; };
ReallyBig F3(int P0, int P1, int P2, int P3, float P4, double P5);
struct FunctionPointers {
  ReallyBig (*F3)(int, int, int, int, float, double);
};
static struct FunctionPointers FP = {F3};
ReallyBig test_musttail_indirect_sret(int P0, int P1, int P2, int P3, float P4, double P5) {
  [[clang::musttail]] return FP.F3(P0, P1, P2, P3, P4, P5);
}

// Musttail with member function pointer call (emitCXXMemberPointerCallExpr path).
struct Bar {
  int target(int x);
  int call_via_ptr(int x);
};
typedef int (Bar::*BarMemFn)(int);
int Bar::call_via_ptr(int x) {
  BarMemFn fn = &Bar::target;
  [[clang::musttail]] return (this->*fn)(x);
}

// CIR: cir.func {{.*}} @_Z13test_noinlinev
// CIR:   cir.call @_Z3foov()

// CIR: cir.func {{.*}} @_Z12test_nomergev
// CIR:   cir.call @_Z3foov()

// CIR: cir.func {{.*}} @_Z18test_always_inlinev
// CIR:   cir.call @_Z3foov()

// CIR: cir.func {{.*}} @_Z13test_musttaili
// CIR:   cir.call must_tail @_Z3bari({{.*}})

// CIR: cir.func {{.*}} @_Z23test_musttail_aggregatev
// CIR:   cir.call must_tail @_Z19ReturnsClassByValuev(
// CIR:   cir.return

// CIR: cir.func {{.*}} @_ZN3Foo12CallerMemberEi
// CIR:   cir.call must_tail @_ZN3Foo14MemberFunctionEi({{.*}})

// CIR: cir.func {{.*}} @_Z21test_musttail_complexs
// CIR:   cir.call must_tail @_Z14complex_callees({{.*}}) : (!s16i) -> !rec_anon_struct
// CIR:   cir.return

// CIR: cir.func {{.*}} @_Z20test_musttail_bitintfd
// CIR:   cir.call must_tail @_Z13bitint_calleefd({{.*}})
// CIR:   cir.return

// CIR: cir.func {{.*}} @_Z27test_musttail_indirect_sretiiiifd
// CIR:   cir.call must_tail %{{.*}}(%arg0, {{.*}}) {cir.call_arg_attrs = [{cir.align = 4 : i64, cir.dead_on_unwind, cir.noalias, cir.sret = !rec_ReallyBig, cir.writable}
// CIR-NEXT:   cir.const #cir.undef
// CIR-NEXT:   cir.return

// CIR: cir.func {{.*}} @_ZN3Bar12call_via_ptrEi
// CIR:   cir.call must_tail %{{.*}}({{.*}})
// CIR:   cir.return

// LLVM: define {{.*}} @_Z13test_noinlinev
// LLVM:   call void @_Z3foov() #[[NOINLINE:[0-9]+]]

// LLVM: define {{.*}} @_Z12test_nomergev
// LLVM:   call void @_Z3foov() #[[NOMERGE:[0-9]+]]

// LLVM: define {{.*}} @_Z18test_always_inlinev
// LLVM:   call void @_Z3foov() #[[ALWAYSINLINE:[0-9]+]]

// LLVM: define {{.*}} @_Z13test_musttaili
// LLVM:   musttail call noundef i32 @_Z3bari(

// LLVM: define {{.*}} @_Z23test_musttail_aggregatev
// LLVM:   musttail call void @_Z19ReturnsClassByValuev(
// LLVM-NEXT:   ret void

// LLVM: define {{.*}} @_ZN3Foo12CallerMemberEi
// LLVM:   musttail call noundef i32 @_ZN3Foo14MemberFunctionEi(

// LLVM: define {{.*}} @_Z21test_musttail_complexs
// LLVM:   musttail call noundef { double, double } @_Z14complex_callees(
// LLVM-NEXT:   ret { double, double }

// LLVM: define {{.*}} @_Z20test_musttail_bitintfd
// LLVM:   musttail call noundef { i64, i64 } @_Z13bitint_calleefd(
// LLVM-NEXT:   ret { i64, i64 }

// LLVM: define {{.*}} @_Z27test_musttail_indirect_sretiiiifd
// LLVM:   musttail call void %{{.*}}(ptr dead_on_unwind writable sret(%struct.ReallyBig) align 4 %0,
// LLVM-NEXT:   ret void

// LLVM: define {{.*}} @_ZN3Bar12call_via_ptrEi
// LLVM:   musttail call noundef i32 %{{.*}}(ptr noundef
// LLVM-NEXT:   ret i32

// LLVM-DAG: attributes #[[NOINLINE]] = { noinline }
// LLVM-DAG: attributes #[[NOMERGE]] = { nomerge }
// LLVM-DAG: attributes #[[ALWAYSINLINE]] = { alwaysinline }
