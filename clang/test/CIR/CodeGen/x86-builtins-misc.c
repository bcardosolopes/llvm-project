// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// X86 builtins

void test_set_flt_rounds(int x) {
  __builtin_set_flt_rounds(x);
}
// CHECK: call void @llvm.set.rounding(i32
// OGCG: call void @llvm.set.rounding(i32

int test_flt_rounds(void) {
  return __builtin_flt_rounds();
}
// CHECK: call i32 @llvm.get.rounding()
// OGCG: call i32 @llvm.get.rounding()

void *test_extract_return_addr(void) {
  return __builtin_extract_return_addr(__builtin_return_address(0));
}
// CHECK: call ptr @llvm.returnaddress(i32 0)
// OGCG: call ptr @llvm.returnaddress(i32 0)

void *test_frob_return_addr(void) {
  return __builtin_frob_return_addr(__builtin_return_address(0));
}
// CHECK-LABEL: @test_frob_return_addr(
// CHECK: call ptr @llvm.returnaddress(i32 0)
// OGCG-LABEL: @test_frob_return_addr(
// OGCG: call ptr @llvm.returnaddress(i32 0)

void *test_dwarf_cfa(void) {
  return __builtin_dwarf_cfa();
}
// CHECK-LABEL: @test_dwarf_cfa(
// CHECK: call ptr @llvm.eh.dwarf.cfa(i32 0)
// OGCG-LABEL: @test_dwarf_cfa(
// OGCG: call ptr @llvm.eh.dwarf.cfa(i32 0)

void *test_stack_address(void) {
  return __builtin_stack_address();
}
// CHECK: call ptr @llvm.stackaddress
// OGCG: call ptr @llvm.stackaddress

void *test_thread_pointer(void) {
  return __builtin_thread_pointer();
}
// CHECK-LABEL: @test_thread_pointer(
// CHECK: call ptr @llvm.thread.pointer
// OGCG-LABEL: @test_thread_pointer(
// OGCG: call ptr @llvm.thread.pointer

void test_unwind_init(void) {
  __builtin_unwind_init();
}
// CHECK: call void @llvm.eh.unwind.init()
// OGCG: call void @llvm.eh.unwind.init()
