// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// ASan accepts the kernel-address alias as equivalent to address.
_Bool test_allow_sanitize_check_kernel_address_alias(void) {
  return __builtin_allow_sanitize_check("kernel-address");
}
// ASAN-LABEL: @test_allow_sanitize_check_kernel_address_alias
// ASAN: zext i1 true to i8

// Querying a sanitizer that is not enabled still folds to false.
_Bool test_allow_sanitize_check_thread_under_asan(void) {
  return __builtin_allow_sanitize_check("thread");
}
// ASAN-LABEL: @test_allow_sanitize_check_thread_under_asan
// ASAN: store i8 0,

struct s {
  char x;
  short count;
  int array[] __attribute__((counted_by(count)));
};

// __builtin_counted_by_ref: emits a GEP to the counted_by field.
void test_counted_by_ref(struct s *p, int size) {
  *__builtin_counted_by_ref(p->array) = size;
}
// CIR-LABEL: @test_counted_by_ref
// CIR: cir.get_member
// CIR: cir.store {{.*}} : !s16i, !cir.ptr<!s16i>

// LLVM-LABEL: @test_counted_by_ref
// LLVM: getelementptr inbounds {{(nuw )?}}%struct.s, ptr %{{.*}}, i32 0, i32 1
// LLVM: store i16 %{{.*}}, ptr %{{.*}}, align 2
