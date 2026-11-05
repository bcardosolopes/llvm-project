// Verify that __attribute__((regparm(N))) on i386 marks the register-passed
// parameters `inreg` on the declaration, definition and call site, matching
// classic CodeGen's ABI. The output is checked against classic CodeGen (OGCG)
// to guarantee an identical calling convention.

// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG

// regparm(3): all three integer args go in eax/edx/ecx.
__attribute__((regparm(3))) int f3(int a, int b, int c);

// regparm(1): only the first arg is in a register.
__attribute__((regparm(1))) int f1(int a, int b);

// regparm(3) with more args than registers: later args spill to the stack.
__attribute__((regparm(3))) int f3over(int a, int b, int c, int d);

// regparm(3) with a leading float: floats never consume a GP register, so the
// following ints are still passed inreg.
__attribute__((regparm(3))) int ffloat(float a, int b, int c);

// regparm(2) with a small struct: the two-word struct is flattened and both
// words are passed inreg, exhausting the registers so the trailing int spills.
struct S { int x; int y; };
__attribute__((regparm(2))) int fstruct(struct S s, int a);

// No attribute: nothing is passed inreg (i386-linux has 0 default reg params).
int plain(int a, int b, int c);

int call_them(void) {
  struct S s = {1, 2};
  return f3(1, 2, 3) + f1(1, 2) + f3over(1, 2, 3, 4) + ffloat(1.0f, 2, 3) +
         fstruct(s, 3) + plain(1, 2, 3);
}

// A regparm definition carries inreg on its parameters too.
__attribute__((regparm(3))) int gdef(int a, int b, int c) { return a + b + c; }

// CIR: cir.call @f3({{.*}}) {cir.regparm = 3 : i32}
// CIR: cir.call @f1({{.*}}) {cir.regparm = 1 : i32}
// CIR: cir.call @ffloat({{.*}}) {cir.regparm = 3 : i32}
// CIR-DAG: cir.func private @f3(!s32i {cir.inreg, cir.noundef}, !s32i {cir.inreg, cir.noundef}, !s32i {cir.inreg, cir.noundef}) -> !s32i {{.*}}attributes {cir.regparm = 3 : i32}
// CIR-DAG: cir.func private @f1(!s32i {cir.inreg, cir.noundef}, !s32i {cir.noundef}) -> !s32i {{.*}}attributes {cir.regparm = 1 : i32}
// CIR-DAG: cir.func private @ffloat(!cir.float {cir.noundef}, !s32i {cir.inreg, cir.noundef}, !s32i {cir.inreg, cir.noundef}) -> !s32i {{.*}}attributes {cir.regparm = 3 : i32}
// CIR-DAG: cir.func private @fstruct(!s32i {cir.inreg}, !s32i {cir.inreg}, !s32i {cir.noundef}) -> !s32i {{.*}}attributes {cir.regparm = 2 : i32}
// CIR-DAG: cir.func no_inline optnone dso_local @gdef(%arg0: !s32i {cir.inreg, cir.noundef}{{.*}}, %arg1: !s32i {cir.inreg, cir.noundef}{{.*}}, %arg2: !s32i {cir.inreg, cir.noundef}{{.*}}) -> !s32i {{.*}}attributes {cir.regparm = 3 : i32}

// LLVM-LABEL: define dso_local i32 @call_them()
// LLVM: call i32 @f3(i32 inreg noundef 1, i32 inreg noundef 2, i32 inreg noundef 3)
// LLVM: call i32 @f1(i32 inreg noundef 1, i32 noundef 2)
// LLVM: call i32 @f3over(i32 inreg noundef 1, i32 inreg noundef 2, i32 inreg noundef 3, i32 noundef 4)
// LLVM: call i32 @ffloat(float noundef 1.000000e+00, i32 inreg noundef 2, i32 inreg noundef 3)
// LLVM: call i32 @fstruct(i32 inreg {{.*}}, i32 inreg {{.*}}, i32 noundef {{.*}})
// LLVM: call i32 @plain(i32 noundef 1, i32 noundef 2, i32 noundef 3)
// LLVM-DAG: declare i32 @f3(i32 inreg noundef, i32 inreg noundef, i32 inreg noundef)
// LLVM-DAG: declare i32 @f1(i32 inreg noundef, i32 noundef)
// LLVM-DAG: declare i32 @f3over(i32 inreg noundef, i32 inreg noundef, i32 inreg noundef, i32 noundef)
// LLVM-DAG: declare i32 @ffloat(float noundef, i32 inreg noundef, i32 inreg noundef)
// LLVM-DAG: declare i32 @fstruct(i32 inreg, i32 inreg, i32 noundef)
// LLVM-DAG: declare i32 @plain(i32 noundef, i32 noundef, i32 noundef)
// LLVM-DAG: define dso_local i32 @gdef(i32 inreg noundef %{{.*}}, i32 inreg noundef %{{.*}}, i32 inreg noundef %{{.*}})

// OGCG-LABEL: define dso_local i32 @call_them()
// OGCG: call i32 @f3(i32 inreg noundef 1, i32 inreg noundef 2, i32 inreg noundef 3)
// OGCG: call i32 @f1(i32 inreg noundef 1, i32 noundef 2)
// OGCG: call i32 @f3over(i32 inreg noundef 1, i32 inreg noundef 2, i32 inreg noundef 3, i32 noundef 4)
// OGCG: call i32 @ffloat(float noundef 1.000000e+00, i32 inreg noundef 2, i32 inreg noundef 3)
// OGCG: call i32 @fstruct(i32 inreg {{.*}}, i32 inreg {{.*}}, i32 noundef {{.*}})
// OGCG: call i32 @plain(i32 noundef 1, i32 noundef 2, i32 noundef 3)
// OGCG-DAG: declare i32 @f3(i32 inreg noundef, i32 inreg noundef, i32 inreg noundef)
// OGCG-DAG: declare i32 @f1(i32 inreg noundef, i32 noundef)
// OGCG-DAG: declare i32 @f3over(i32 inreg noundef, i32 inreg noundef, i32 inreg noundef, i32 noundef)
// OGCG-DAG: declare i32 @ffloat(float noundef, i32 inreg noundef, i32 inreg noundef)
// OGCG-DAG: declare i32 @fstruct(i32 inreg, i32 inreg, i32 noundef)
// OGCG-DAG: declare i32 @plain(i32 noundef, i32 noundef, i32 noundef)
// OGCG-DAG: define dso_local i32 @gdef(i32 inreg noundef %{{.*}}, i32 inreg noundef %{{.*}}, i32 inreg noundef %{{.*}})
