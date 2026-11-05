// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -O0 -disable-llvm-passes \
// RUN:   -fsanitize=vptr -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -std=c++17 -O0 -disable-llvm-passes \
// RUN:   -fsanitize=vptr -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Verify that the vptr sanitizer check is guarded by a null pointer check.
// Without the guard, loading the vtable pointer from a null pointer would
// crash during the sanitizer check itself.

struct Base { virtual void f(); };
void test(Base *b) { b->f(); }

// CIR-LABEL: define {{.*}} @_Z4testP4Base
// CIR: %[[CIR_NONNULL:.*]] = icmp ne ptr %{{.*}}, null, !nosanitize ![[CIR_NOSAN:[0-9]+]]
// CIR-NEXT: br i1 %[[CIR_NONNULL]], label %[[CIR_NOTNULL:[^,]+]], label %[[CIR_NULL:[^,]+]], !nosanitize ![[CIR_NOSAN]]{{$}}
// CIR: [[CIR_NOTNULL]]:
// CIR: %vtable = load ptr, ptr %{{.*}}, align 8, !nosanitize ![[CIR_NOSAN]]
// CIR: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[CIR_PROF:[0-9]+]], !nosanitize ![[CIR_NOSAN]]

// OGCG-LABEL: define {{.*}} @_Z4testP4Base
// OGCG: %[[OG_NONNULL:.*]] = icmp ne ptr %{{.*}}, null, !nosanitize ![[OG_NOSAN:[0-9]+]]
// OGCG-NEXT: br i1 %[[OG_NONNULL]], label %[[OG_NOTNULL:[^,]+]], label %[[OG_NULL:[^,]+]], !nosanitize ![[OG_NOSAN]]{{$}}
// OGCG: [[OG_NOTNULL]]:
// OGCG: %vtable = load ptr, ptr %{{.*}}, align 8, !nosanitize ![[OG_NOSAN]]
// OGCG: br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[OG_PROF:[0-9]+]], !nosanitize ![[OG_NOSAN]]
