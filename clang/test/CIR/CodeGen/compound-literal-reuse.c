// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s < %t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll

// Test that file-scope compound literals are cached and reused when the same
// CompoundLiteralExpr AST node is referenced multiple times.

struct CompoundTy { int a; };

// MyCLH is a const pointer initialized to the address of a file-scope
// compound literal. Both loads in compareMyCLH should reference the same
// global (pointer identity preserved).
const struct CompoundTy * const MyCLH = &(struct CompoundTy){3};

// CIR-DAG: cir.global "private" internal @".compoundLiteral.0"
// CIR-DAG: cir.global constant {{.*}} @MyCLH = #cir.global_view<@".compoundLiteral.0">

// LLVM-DAG: @.compoundLiteral.0 = internal {{.*}} %struct.CompoundTy { i32 3 }
// LLVM-DAG: @MyCLH = constant ptr @.compoundLiteral.0

// File-scope compound literals used in multiple global initializers each get
// their own global (different AST nodes), but repeated use of the same
// compound literal reuses the cached global.
int *a = &(int){1};
struct CompoundTy *b = &(struct CompoundTy){1};

// CIR-DAG: cir.global "private" internal @".compoundLiteral.1"
// CIR-DAG: cir.global external @a = #cir.global_view<@".compoundLiteral.1">
// CIR-DAG: cir.global "private" internal @".compoundLiteral.2"
// CIR-DAG: cir.global external @b = #cir.global_view<@".compoundLiteral.2">

// LLVM-DAG: @.compoundLiteral.1 = internal {{.*}} i32 1
// LLVM-DAG: @a = global ptr @.compoundLiteral.1
// LLVM-DAG: @.compoundLiteral.2 = internal {{.*}} %struct.CompoundTy { i32 1 }
// LLVM-DAG: @b = global ptr @.compoundLiteral.2

int compareMyCLH(void) {
  const void *x = MyCLH;
  const void *y = MyCLH;
  return x == y;
}

// Both stores should reference the same compound literal global.
// LLVM-LABEL: define {{.*}} @compareMyCLH
// LLVM: store ptr @.compoundLiteral.0, ptr %{{.*}}
// LLVM: store ptr @.compoundLiteral.0, ptr %{{.*}}
// LLVM: icmp eq
// LLVM: ret i32
