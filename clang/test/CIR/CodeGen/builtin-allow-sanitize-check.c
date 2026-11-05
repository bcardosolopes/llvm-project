// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-NONE
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=address -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-ASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-address -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-ASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=thread -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-TSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=memory -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-MSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-memory -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-MSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=hwaddress -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-HWASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-hwaddress -emit-cir -o - %s | FileCheck %s --check-prefixes=CHECK,CIR-HWASAN

// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-NONE
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=address -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-ASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-address -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-ASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=thread -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-TSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=memory -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-MSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-memory -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-MSAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=hwaddress -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-HWASAN
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -fsanitize=kernel-hwaddress -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefixes=CHECK,LLVM-HWASAN

// CHECK-LABEL: @test_address
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.call_llvm_intrinsic "allow.sanitize.address"
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.const #false
// CIR-HWASAN: cir.const #false
// LLVM-NONE: ret i1 false
// LLVM-ASAN: call i1 @llvm.allow.sanitize.address()
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: ret i1 false
// LLVM-HWASAN: ret i1 false
_Bool test_address(void) {
  return __builtin_allow_sanitize_check("address");
}

// CHECK-LABEL: @test_kernel_address
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.call_llvm_intrinsic "allow.sanitize.address"
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.const #false
// CIR-HWASAN: cir.const #false
// LLVM-NONE: ret i1 false
// LLVM-ASAN: call i1 @llvm.allow.sanitize.address()
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: ret i1 false
// LLVM-HWASAN: ret i1 false
_Bool test_kernel_address(void) {
  return __builtin_allow_sanitize_check("kernel-address");
}

// CHECK-LABEL: @test_thread
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.const #false
// CIR-TSAN: cir.call_llvm_intrinsic "allow.sanitize.thread"
// CIR-MSAN: cir.const #false
// CIR-HWASAN: cir.const #false
// LLVM-NONE: ret i1 false
// LLVM-ASAN: ret i1 false
// LLVM-TSAN: call i1 @llvm.allow.sanitize.thread()
// LLVM-MSAN: ret i1 false
// LLVM-HWASAN: ret i1 false
_Bool test_thread(void) {
  return __builtin_allow_sanitize_check("thread");
}

// CHECK-LABEL: @test_memory
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.const #false
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.call_llvm_intrinsic "allow.sanitize.memory"
// CIR-HWASAN: cir.const #false
// LLVM-NONE: ret i1 false
// LLVM-ASAN: ret i1 false
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: call i1 @llvm.allow.sanitize.memory()
// LLVM-HWASAN: ret i1 false
_Bool test_memory(void) {
  return __builtin_allow_sanitize_check("memory");
}

// CHECK-LABEL: @test_kernel_memory
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.const #false
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.call_llvm_intrinsic "allow.sanitize.memory"
// CIR-HWASAN: cir.const #false
// LLVM-NONE: ret i1 false
// LLVM-ASAN: ret i1 false
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: call i1 @llvm.allow.sanitize.memory()
// LLVM-HWASAN: ret i1 false
_Bool test_kernel_memory(void) {
  return __builtin_allow_sanitize_check("kernel-memory");
}

// CHECK-LABEL: @test_hwaddress
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.const #false
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.const #false
// CIR-HWASAN: cir.call_llvm_intrinsic "allow.sanitize.hwaddress"
// LLVM-NONE: ret i1 false
// LLVM-ASAN: ret i1 false
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: ret i1 false
// LLVM-HWASAN: call i1 @llvm.allow.sanitize.hwaddress()
_Bool test_hwaddress(void) {
  return __builtin_allow_sanitize_check("hwaddress");
}

// CHECK-LABEL: @test_kernel_hwaddress
// CIR-NONE: cir.const #false
// CIR-ASAN: cir.const #false
// CIR-TSAN: cir.const #false
// CIR-MSAN: cir.const #false
// CIR-HWASAN: cir.call_llvm_intrinsic "allow.sanitize.hwaddress"
// LLVM-NONE: ret i1 false
// LLVM-ASAN: ret i1 false
// LLVM-TSAN: ret i1 false
// LLVM-MSAN: ret i1 false
// LLVM-HWASAN: call i1 @llvm.allow.sanitize.hwaddress()
_Bool test_kernel_hwaddress(void) {
  return __builtin_allow_sanitize_check("kernel-hwaddress");
}
