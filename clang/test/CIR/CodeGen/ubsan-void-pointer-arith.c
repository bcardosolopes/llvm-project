// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fsanitize=pointer-overflow -emit-llvm %s -o - | FileCheck %s

// Pointer arithmetic on a `void*` (GNU extension, sizeof(void)==1) under the
// pointer-overflow sanitizer used to crash CIR: emitArraySubscriptPtr computed
// getTypeAllocSize on the unsized `!cir.void` element type, which asserts. The
// bounds/overflow check must instead use an element size of 1, matching classic
// CodeGen. Here `data[37]` is a `void*` and `(...)[927]` steps it by 927 bytes.

void bork(void **data) { (*(unsigned short *)(&(data[37])[927])) = 0; }

// CHECK-LABEL: define dso_local void @bork
// The void* subscript of 927 must scale by element size 1 (== 927 bytes).
// CHECK: add i64 %{{[0-9]+}}, 927
// CHECK: call void @__ubsan_handle_pointer_overflow
