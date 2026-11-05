// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// Test that transparent union arguments are collapsed to the first field type
// on x86_64. The union should be passed as its first member (i32), not
// expanded to multiple parameters.

typedef union {
  int i;
  float f;
} transparent_u __attribute__((__transparent_union__));

// CIR: define {{.*}}void @func_transparent_union(i32 %{{.*}})
// OGCG: define {{.*}}void @func_transparent_union(i32 %{{.*}})
void func_transparent_union(transparent_u u) { }

// Three-member union should also collapse to first field.
typedef union {
  long l;
  double d;
  void *p;
} transparent_u3 __attribute__((__transparent_union__));

// CIR: define {{.*}}void @func_transparent_union3(i64 %{{.*}})
// OGCG: define {{.*}}void @func_transparent_union3(i64 %{{.*}})
void func_transparent_union3(transparent_u3 u) { }
