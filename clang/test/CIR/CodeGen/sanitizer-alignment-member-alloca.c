// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment,null -fsanitize-recover=alignment,null %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir \
// RUN:   -fsanitize=alignment,null -fsanitize-recover=alignment,null %s -o %t.cir
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s --check-prefix=LLVM

union U {
  int i;
};

// LLVM-LABEL: define{{.*}} void @foo
// LLVM-NOT: call void @__ubsan_handle_type_mismatch_v1
// LLVM: store i32 1
void foo(void) {
  union U u;
  u.i = 1;
}

