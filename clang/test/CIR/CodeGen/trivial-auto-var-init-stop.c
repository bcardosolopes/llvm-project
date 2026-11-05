// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ftrivial-auto-var-init=zero -ftrivial-auto-var-init-stop-after=2 \
// RUN:   %s -o %t.cir 2>&1 | FileCheck --check-prefix=WARN %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -ftrivial-auto-var-init=zero -ftrivial-auto-var-init-stop-after=2 \
// RUN:   %s -o %t.ogcg.ll 2>&1 | FileCheck --check-prefix=WARN %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -ftrivial-auto-var-init=zero -ftrivial-auto-var-init-max-size=8 \
// RUN:   %s -o %t.maxsize.cir
// RUN: FileCheck --input-file=%t.maxsize.cir --check-prefix=MAXSIZE %s

// Test -ftrivial-auto-var-init-stop-after=N and
// -ftrivial-auto-var-init-max-size=N.

// WARN: warning: -ftrivial-auto-var-init-stop-after=2

void test(void) {
  int a;
  int b;
  int c;
}

// First 2 vars should be initialized, the 3rd should NOT be.
// CIR-LABEL: @test
// CIR: cir.alloca {{.*}}, ["a", init]
// CIR: cir.alloca {{.*}}, ["b", init]
// CIR: cir.alloca {{.*}}, ["c"]
// CIR-NOT: ["c", init]

struct Large { int x; char buff[1024]; };

void test_max_size(void) {
  int small;
  struct Large large;
}

// 'small' (4 bytes) should be initialized, 'large' (>8 bytes) should NOT be.
// MAXSIZE-LABEL: @test_max_size
// MAXSIZE: cir.alloca {{.*}}, ["small", init]
// MAXSIZE: cir.alloca {{.*}}, ["large"]
// MAXSIZE-NOT: ["large", init]
