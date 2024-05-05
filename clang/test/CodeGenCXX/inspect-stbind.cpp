// RUN: %clang_cc1 -triple i386-unknown-unknown -fpattern-matching -emit-llvm %s -o - | FileCheck %s

struct s { int a, b, c; };

// CHECK-LABEL: _Z7stbind0R1s(
int stbind0(s &a) {
  // CHECK: %inspect.result{{.*}}alloca
  // CHECK: br label %pat.stbind
  int x = inspect (a) {
      [1,2,3] => 3;
      // CHECK: pat.stbind:
      // CHECK: icmp eq i32 {{.*}}, 1
      // CHECK: br i1 {{.*}}, label %[[TRUE:.*]], label %pat.wildcard
      // CHECK: [[TRUE]]:
      // CHECK: icmp eq i32 {{.*}}, 2
      // CHECK: br i1 {{.*}}, label %[[TRUE1:.*]], label %pat.wildcard
      // CHECK: [[TRUE1]]:
      // CHECK: icmp eq i32 {{.*}}, 3
      // CHECK: br i1 {{.*}}, label %patbody, label %pat.wildcard
      __ => 2;
  };

  // CHECK: load{{.*}}%inspect.result
  return x;
}

// CHECK-LABEL: _Z7stbind1R1s(
int stbind1(s &a) {
  // CHECK: %inspect.result{{.*}}alloca
  // CHECK: br label %pat.stbind
  int x = inspect (a) {
       [1,__,new_id] => new_id+1;
      // CHECK: pat.stbind:
      // CHECK: icmp eq i32 {{.*}}, 1
      // CHECK: br i1 {{.*}}, label %patbody, label %pat.wildcard
      // CHECK: patbody:
      // CHECK: %[[C:.*]] = getelementptr inbounds %struct.s, ptr {{.*}}, i32 0, i32 2
      // CHECK: load i32, ptr %[[C]], align 4
      // CHECK: br label %inspect.epilogue
      __ => 0;
  };

  // CHECK: load{{.*}}%inspect.result
  return x;
}

void stbind_bitfield() {
  struct insn_type {
    unsigned opc : 16, imm : 16;
  };
  insn_type insn;
  inspect(insn) {
    [o, i] => { o++; };
  };
}
