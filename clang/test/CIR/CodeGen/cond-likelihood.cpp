// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O1 %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 -disable-llvm-passes %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -O1 -disable-llvm-passes %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll --check-prefix=OGCG %s

// Test that [[likely]]/[[unlikely]] on if/for/while emit cir.expect
// (lowered to llvm.expect) for branch prediction hints.
// OGCG does not emit expect for do-while loops.

extern void bar();
extern bool cond();

// CIR-LABEL: @_Z7if_testb
// LLVM-LABEL: @_Z7if_testb
// OGCG-LABEL: @_Z7if_testb
void if_test(bool x) {
  if (x) [[likely]] {
    bar();
  }
}
// CIR: cir.expect({{.*}}, {{.*}}) : !cir.bool
// CIR: cir.if

// LLVM: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// OGCG: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// CIR-LABEL: @_Z16if_unlikely_testb
// LLVM-LABEL: @_Z16if_unlikely_testb
// OGCG-LABEL: @_Z16if_unlikely_testb
void if_unlikely_test(bool x) {
  if (x) [[unlikely]] {
    bar();
  }
}
// CIR: cir.expect({{.*}}, {{.*}}) : !cir.bool
// CIR: cir.if

// LLVM: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 false)

// OGCG: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 false)

// CIR-LABEL: @_Z10while_testv
// LLVM-LABEL: @_Z10while_testv
// OGCG-LABEL: @_Z10while_testv
void while_test() {
  while (cond()) [[likely]] {
    bar();
  }
}
// CIR: cir.while {
// CIR:   cir.call @_Z4condv()
// CIR:   cir.expect({{.*}}, {{.*}}) : !cir.bool
// CIR:   cir.condition(

// LLVM: call {{.*}} @_Z4condv()
// LLVM: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// OGCG: call {{.*}} @_Z4condv()
// OGCG: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// OGCG does NOT emit llvm.expect for do-while loops.
// CIR-LABEL: @_Z13do_while_testv
// LLVM-LABEL: @_Z13do_while_testv
// OGCG-LABEL: @_Z13do_while_testv
void do_while_test() {
  do [[likely]] {
    bar();
  } while (cond());
}
// CIR: cir.do {
// CIR-NOT: cir.expect
// CIR: cir.condition(

// LLVM-NOT: llvm.expect
// LLVM: call {{.*}} @_Z4condv()
// LLVM-NOT: llvm.expect

// OGCG-NOT: llvm.expect
// OGCG: call {{.*}} @_Z4condv()
// OGCG-NOT: llvm.expect

// CIR-LABEL: @_Z8for_testv
// LLVM-LABEL: @_Z8for_testv
// OGCG-LABEL: @_Z8for_testv
void for_test() {
  for (; cond();) [[likely]] {
    bar();
  }
}
// CIR: cir.for : cond {
// CIR:   cir.call @_Z4condv()
// CIR:   cir.expect({{.*}}, {{.*}}) : !cir.bool
// CIR:   cir.condition(

// LLVM: call {{.*}} @_Z4condv()
// LLVM: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// OGCG: call {{.*}} @_Z4condv()
// OGCG: call i1 @llvm.expect.i1(i1 %{{.*}}, i1 true)

// Infinite while loops warn that [[likely]] has no effect, and OGCG does not
// emit llvm.expect for the constant-true condition. Direct CIR should match.
// CIR-LABEL: @_Z15while_true_testv
// LLVM-LABEL: @_Z15while_true_testv
// OGCG-LABEL: @_Z15while_true_testv
void while_true_test() {
  while (1) [[likely]] {
    bar();
  }
}
// CIR: cir.while {
// CIR-NOT: cir.expect
// CIR: cir.condition(

// LLVM-NOT: @llvm.expect.i1
// LLVM: call {{.*}} @_Z3barv()

// OGCG-NOT: @llvm.expect.i1
// OGCG: call {{.*}} @_Z3barv()

// Test that -O0 does NOT emit expect intrinsics (matches OGCG behavior).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O0 %s -o %t.O0.cir
// RUN: FileCheck --input-file=%t.O0.cir --check-prefix=O0 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O0 -disable-llvm-passes %s -o %t.O0.ll
// RUN: FileCheck --input-file=%t.O0.ll --check-prefix=LLVM-O0 %s
// O0-LABEL: @_Z7if_testb
// O0-NOT: cir.expect
// O0-LABEL: @_Z16if_unlikely_testb
// O0-NOT: cir.expect
// O0-LABEL: @_Z10while_testv
// O0-NOT: cir.expect

// LLVM-O0-LABEL: @_Z7if_testb
// LLVM-O0-NOT: llvm.expect
// LLVM-O0-LABEL: @_Z16if_unlikely_testb
// LLVM-O0-NOT: llvm.expect
// LLVM-O0-LABEL: @_Z10while_testv
// LLVM-O0-NOT: llvm.expect
