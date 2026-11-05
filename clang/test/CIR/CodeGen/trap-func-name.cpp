// RUN: %clang_cc1 -O0 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -Wno-error=return-type -ftrapv -ftrap-function=mytrap %s -o - | FileCheck %s -check-prefix=TRAPFUNC
// RUN: %clang_cc1 -O0 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -Wno-error=return-type -ftrapv %s -o - | FileCheck %s -check-prefix=NOOPTION
// RUN: %clang_cc1 -O0 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -Wno-error=return-type -ftrapv -ftrap-function=mytrap %s -o - | FileCheck %s -check-prefix=CIR-TRAPFUNC

// TRAPFUNC-LABEL: define {{(dso_local )?}}void @_Z12test_builtinv
// TRAPFUNC: call void @llvm.trap() [[ATTR0:#[0-9]+]]

// NOOPTION-LABEL: define {{(dso_local )?}}void @_Z12test_builtinv
// NOOPTION: call void @llvm.trap(){{$}}

void test_builtin(void) {
  __builtin_trap();
}

// TRAPFUNC-LABEL: define {{.*}}i32 @_Z13test_noreturnv
// TRAPFUNC: call void @llvm.trap() [[ATTR0]]

// NOOPTION-LABEL: define {{.*}}i32 @_Z13test_noreturnv
// NOOPTION: call void @llvm.trap(){{$}}

int test_noreturn(void) {
}

// TRAPFUNC-LABEL: define {{.*}}i32 @_Z17test_add_overflowii
// TRAPFUNC: call void @llvm.ubsantrap({{.*}}) [[ATTR1:#[0-9]+]]
// TRAPFUNC-SAME: !nosanitize

// NOOPTION-LABEL: define {{.*}}i32 @_Z17test_add_overflowii
// NOOPTION: call void @llvm.ubsantrap({{.*}})
// NOOPTION-SAME: !nosanitize

int test_add_overflow(int a, int b) {
  return a + b;
}

// TRAPFUNC: attributes [[ATTR0]] = { "trap-func-name"="mytrap" }
// TRAPFUNC: attributes [[ATTR1]] = { nomerge noreturn nounwind "trap-func-name"="mytrap" }

// NOOPTION-NOT: trap-func-name

// CIR-TRAPFUNC-LABEL: cir.func{{.*}} @_Z17test_add_overflowii
// CIR-TRAPFUNC: cir.trap
// CIR-TRAPFUNC-SAME: cir.trap_func_name = "mytrap"
