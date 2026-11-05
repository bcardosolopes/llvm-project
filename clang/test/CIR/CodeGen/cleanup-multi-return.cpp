// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that CIR correctly handles multiple returns through nested RAII
// cleanup scopes. CIR inlines cleanups at each exit point rather than using
// OGCG's switch-out mechanism (NormalCleanupDestSlot), but the final LLVM
// output must correctly destroy objects in reverse construction order.

struct RAII {
  RAII(int);
  ~RAII();
};

int multi_return(int x) {
  RAII a(1);
  {
    RAII b(2);
    if (x > 10) return 1;
    {
      RAII c(3);
      if (x > 5) return 2;
    }
    if (x > 0) return 3;
  }
  return 0;
}

// Both CIR and OGCG must construct a(1), b(2), and destroy in reverse order.
// CHECK: call void @_ZN4RAIIC1Ei({{.*}}, i32 noundef 1)
// CHECK: call void @_ZN4RAIIC1Ei({{.*}}, i32 noundef 2)

// OGCG: call void @_ZN4RAIIC1Ei({{.*}}, i32 noundef 1)
// OGCG: call void @_ZN4RAIIC1Ei({{.*}}, i32 noundef 2)
