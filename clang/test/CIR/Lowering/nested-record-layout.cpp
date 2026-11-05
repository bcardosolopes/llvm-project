// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck %s --input-file %t-cir.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck %s -check-prefix=OGCG --input-file %t.ll

// Verify that nested record types with empty subobjects are laid out
// correctly, matching OGCG's ABI layout.

struct Empty {};

struct HasEmpty {
  Empty e;
  int x;
};

struct Nested {
  HasEmpty h;
  double d;
};

// CHECK: %struct.HasEmpty = type { [4 x i8], i32 }
// CHECK: %struct.Nested = type { %struct.HasEmpty, double }

// OGCG: %struct.HasEmpty = type { [4 x i8], i32 }
// OGCG: %struct.Nested = type { %struct.HasEmpty, double }

void use_hasempty(HasEmpty *p) { p->x = 1; }
void use_nested(Nested *p) { p->d = 2.0; }
