// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -O1 %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t-O0.cir
// RUN: FileCheck --check-prefix=CIR-O0 --input-file=%t-O0.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -O1 %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Verify that __builtin_unpredictable sets the cir.unpredictable attribute
// on cir.if and cir.switch ops at -O1+, and not at -O0.
// Also verify that !unpredictable metadata reaches the LLVM IR output.
// Matches OGCG CodeGenFunction.cpp:2106-2115 and CGStmt.cpp:2520-2531.

void foo(void);
void bar(void);

void test_if(int x) {
  if (__builtin_unpredictable(x > 0))
    foo();
  else
    bar();
}
// CIR: cir.if %{{.*}} {
// CIR:   cir.call @foo
// CIR: } else {
// CIR:   cir.call @bar
// CIR: } {cir.unpredictable}

// CIR-O0: cir.if
// CIR-O0-NOT: cir.unpredictable

// LLVM: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !unpredictable

void test_switch(int x) {
  switch (__builtin_unpredictable(x)) {
    case 0: foo(); break;
    case 1: bar(); break;
  }
}
// CIR: cir.switch({{.*}}) {
// CIR:   cir.case
// CIR: } {cir.unpredictable}

// LLVM: ], !unpredictable
