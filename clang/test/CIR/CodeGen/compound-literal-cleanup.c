// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Block-scope compound literals in C are destroyed at end of the
// enclosing scope. When the type has a cleanup attribute, the
// cleanup function must be called.

typedef struct { int x; } S;
void cleanup_s(S *s);

void test(void) {
  S __attribute__((cleanup(cleanup_s))) s = (S){42};
}

// Both CIR and OGCG should call cleanup_s at end of the function.
// CIR: define {{.*}} void @test()
// CIR: call void @cleanup_s(ptr noundef %{{.*}})
// CIR: ret void

// OGCG: define {{.*}} void @test()
// OGCG: call void @cleanup_s(ptr noundef %{{.*}})
// OGCG: ret void
