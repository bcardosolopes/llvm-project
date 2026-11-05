// RUN: %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -fblocks -fclangir -emit-llvm -o - %s | FileCheck %s --check-prefix=LLVM --check-prefix=COMMON
// RUN: %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -fblocks -emit-llvm -o - %s | FileCheck %s --check-prefix=OGCG --check-prefix=COMMON

extern int e(void);

void (^b)() = ^{
  static int i = 0;
};

void f(void) {
  ^{ static int j = e(); }();
}

// COMMON-DAG: @_ZZUb_E1i = internal global i32 0, align 4
// COMMON-DAG: @_ZZZ1fvEUb0_E1j = internal global i32 0, align 4
// COMMON-DAG: @_ZGVZZ1fvEUb0_E1j = internal global i64 0, align 8

// LLVM-LABEL: define internal void @___Z1fv_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: call i32 @__cxa_guard_acquire(ptr @_ZGVZZ1fvEUb0_E1j)
// LLVM: call noundef i32 @_Z1ev()
// LLVM: store i32 %{{.*}}, ptr @_ZZZ1fvEUb0_E1j, align 4
// LLVM: call void @__cxa_guard_release(ptr @_ZGVZZ1fvEUb0_E1j)

// OGCG-LABEL: define internal void @___Z1fv_block_invoke(ptr noundef %{{.*}})
// OGCG: call i32 @__cxa_guard_acquire(ptr @_ZGVZZ1fvEUb0_E1j)
// OGCG: call noundef i32 @_Z1ev()
// OGCG: store i32 %{{.*}}, ptr @_ZZZ1fvEUb0_E1j, align 4
// OGCG: call void @__cxa_guard_release(ptr @_ZGVZZ1fvEUb0_E1j)
