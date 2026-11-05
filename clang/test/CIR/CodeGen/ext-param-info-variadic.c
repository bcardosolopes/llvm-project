// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

// Test that variadic functions with extended parameter info (e.g.
// pass_object_size or noescape) don't crash. This was a crash in
// RequiredArgs::getFromProtoWithExtraSlots (CIRGenFunctionInfo.h).

// Variadic function with pass_object_size attribute.
void variadic_pass_object_size(void *__attribute__((pass_object_size(0))) p, ...);

void test_pass_object_size(void) {
  int buf[10];
  variadic_pass_object_size(buf);
}

// CIR: cir.func {{.*}} @test_pass_object_size()
// CIR:   cir.call @variadic_pass_object_size

// LLVM: define {{.*}} @test_pass_object_size()
// LLVM:   call void (ptr, i64, ...) @variadic_pass_object_size(ptr noundef {{%.*}}, i64 noundef 40)

// Variadic function with noescape attribute.
void variadic_noescape(__attribute__((noescape)) void *p, ...);

void test_noescape(void) {
  int buf[10];
  variadic_noescape(buf);
}

// CIR: cir.func {{.*}} @test_noescape()
// CIR:   cir.call @variadic_noescape

// LLVM: define {{.*}} @test_noescape()
// LLVM:   call void (ptr, ...) @variadic_noescape(ptr noundef captures(none) {{%.*}})
