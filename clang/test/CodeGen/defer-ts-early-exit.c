// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fdefer-ts -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c23 -fdefer-ts -emit-llvm -disable-llvm-passes -fclangir %s -o - | FileCheck %s --check-prefix=CIR %}

void x(int);
_Bool q(int);

// OGCG-LABEL: define{{.*}} void @cleanup_no_insert_point()
// OGCG: %cleanup.dest.slot = alloca i32, align 4
// OGCG-COUNT-3: store i32 {{[0-9]+}}, ptr %cleanup.dest.slot, align 4
// OGCG-COUNT-1: call void @x(i32 noundef 9)
// OGCG: %cleanup.dest = load i32, ptr %cleanup.dest.slot, align 4
// OGCG: switch i32 %cleanup.dest
//
// CIR-LABEL: define{{.*}} void @cleanup_no_insert_point()
// CIR: %cleanup.dest.slot = alloca i32, align 4
// CIR-COUNT-3: store i32 {{[0-9]+}}, ptr %cleanup.dest.slot, align 4
// CIR-COUNT-1: call void @x(i32 noundef 9)
// CIR: %cleanup.dest = load i32, ptr %cleanup.dest.slot, align 4
// CIR: switch i32 %cleanup.dest
void cleanup_no_insert_point(void) {
  while (q(1)) {
    _Defer { x(9); }
    if (q(2))
      continue;
    if (q(3))
      break;
  }
}
