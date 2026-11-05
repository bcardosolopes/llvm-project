// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -fsanitize=alignment -fsanitize-recover=alignment %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -fsanitize=alignment -fsanitize-recover=alignment %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// Under -fsanitize=alignment, __builtin_memcpy bitcasts each pointer argument
// to `void *` before the argument type check. ClangIR must strip that no-op
// bitcast so the known-base-alignment elision sees through to a statically
// aligned alloca and skips the redundant alignment check, matching classic's
// EmitTypeCheck which operates on Ptr->stripPointerCasts()
// (CGExpr.cpp:767, elision at CGExpr.cpp:854-855). A genuinely under-aligned
// pointer must still be checked.

// Both memcpy pointers refer to statically 4-aligned allocas that already
// satisfy int's alignment, so no alignment check is emitted, matching classic.
// LLVM-LABEL: define{{.*}} i32 @aligned_allocas
// LLVM-NOT: call void @__ubsan_handle_type_mismatch_v1
// LLVM: call void @llvm.memcpy
//
// OGCG-LABEL: define{{.*}} i32 @aligned_allocas
// OGCG-NOT: call void @__ubsan_handle_type_mismatch_v1
// OGCG: call void @llvm.memcpy
int aligned_allocas(void) {
  int v = 99, e;
  __builtin_memcpy(&e, &v, sizeof(e));
  return e;
}

// The destination pointer is an int* parameter with no statically known base,
// so its alignment cannot be proven and the alignment check must still be
// emitted (do not over-elide). The source &local elides as an aligned alloca.
// LLVM-LABEL: define{{.*}} void @unknown_base
// LLVM: call void @__ubsan_handle_type_mismatch_v1
//
// OGCG-LABEL: define{{.*}} void @unknown_base
// OGCG: call void @__ubsan_handle_type_mismatch_v1
void unknown_base(int *out) {
  int local = 7;
  __builtin_memcpy(out, &local, sizeof(int));
}
