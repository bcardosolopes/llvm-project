// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -disable-llvm-passes -fclangir %s -o - | FileCheck %s --check-prefix=CIR %}

void cleanup_int(int *);
_Bool q(int);

// OGCG-LABEL: define{{.*}} void @cleanup_attr_early_exit()
// OGCG: %cleanup.dest.slot = alloca i32, align 4
// OGCG-COUNT-3: store i32 {{[0-9]+}}, ptr %cleanup.dest.slot, align 4
// OGCG-COUNT-1: call void @cleanup_int(ptr noundef %{{.*}})
// OGCG: %cleanup.dest = load i32, ptr %cleanup.dest.slot, align 4
// OGCG: switch i32 %cleanup.dest
//
// CIR-LABEL: define{{.*}} void @cleanup_attr_early_exit()
// CIR: %cleanup.dest.slot = alloca i32, align 4
// CIR-COUNT-3: store i32 {{[0-9]+}}, ptr %cleanup.dest.slot, align 4
// CIR-COUNT-1: call void @cleanup_int(ptr noundef %{{.*}})
// CIR: %cleanup.dest = load i32, ptr %cleanup.dest.slot, align 4
// CIR: switch i32 %cleanup.dest
void cleanup_attr_early_exit(void) {
  while (q(1)) {
    __attribute__((cleanup(cleanup_int))) int x = 1;
    if (q(2))
      continue;
    if (q(3))
      break;
  }
}
