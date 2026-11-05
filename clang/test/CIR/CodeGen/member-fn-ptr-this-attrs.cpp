// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Verify that member function pointer calls emit nonnull, align, and
// dereferenceable on the 'this' parameter, and !nosanitize metadata on
// virtual dispatch vtable GEP/load, matching OGCG.

struct S {
  int x;
  int get() const { return x; }
};

typedef int (S::*MemFnPtr)() const;

int call_memfnptr(S* s, MemFnPtr mfp) {
  return (s->*mfp)();
}
// LLVM-LABEL: @_Z13call_memfnptr
// LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 %{{[0-9]+}}, !nosanitize
// LLVM: load ptr, ptr %{{[0-9]+}}, align 8, !nosanitize
// LLVM: call noundef i32 %{{[0-9]+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}})
// OGCG-LABEL: @_Z13call_memfnptr
// OGCG: getelementptr i8, ptr %{{[a-z]+}}, i64 %{{[0-9]+}}, !nosanitize
// OGCG: load ptr, ptr %{{[0-9]+}}, align 8, !nosanitize
// OGCG: call noundef i32 %{{[0-9]+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{[0-9]+}})

struct Large {
  long a, b, c, d;
  int compute() { return a + d; }
};

typedef int (Large::*LargeMemFnPtr)();

int call_large(Large* l, LargeMemFnPtr mfp) {
  return (l->*mfp)();
}
// LLVM-LABEL: @_Z10call_large
// LLVM: getelementptr i8, ptr %{{[0-9]+}}, i64 %{{[0-9]+}}, !nosanitize
// LLVM: load ptr, ptr %{{[0-9]+}}, align 8, !nosanitize
// LLVM: call noundef i32 %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(32) %{{[0-9]+}})
// OGCG-LABEL: @_Z10call_large
// OGCG: getelementptr i8, ptr %{{[a-z]+}}, i64 %{{[0-9]+}}, !nosanitize
// OGCG: load ptr, ptr %{{[0-9]+}}, align 8, !nosanitize
// OGCG: call noundef i32 %{{[0-9]+}}(ptr noundef nonnull align 8 dereferenceable(32) %{{[0-9]+}})
