// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// A constant pointer to a data member of a polymorphic class, dereferenced in a
// function body (the const-folded ConstantLValueEmitter path). The class record
// begins with an unsized `!cir.vptr` member, so the flat-offset-to-member-index
// walker cannot decompose the offset into structured indices; it must fall back
// to a raw byte offset (matching classic CodeGen's `getelementptr i8`). Before
// the fix, the walker called getTypeAllocSize on the unsized vptr member and
// aborted (assertion `cir::isSized`), crashing the compiler on valid C++.

struct S {
  virtual ~S();
  int a;
  long c;
};

S s;

int deref() {
  static const long *const p = &s.c; // &s.c is at byte offset 16 (past vptr+a).
  return (int)*p;
}

// The member pointer is a raw byte-offset view; the pointee type stays `!s64i`.
// CIR-DAG: cir.global {{.*}}@_ZZ5derefvE1p = #cir.global_view<@s, byte_offset = 16 : i64> : !cir.ptr<!s64i>
// CIR-LABEL: cir.func{{.*}} @_Z5derefv
// CIR: %[[V:.*]] = cir.const #cir.global_view<@s, byte_offset = 16 : i64> : !cir.ptr<!s64i>
// CIR: cir.load align(8) %[[V]] : !cir.ptr<!s64i>, !s64i

// LLVM-DAG: @_ZZ5derefvE1p = internal constant ptr getelementptr (i8, ptr @s, i64 16)
// LLVM-LABEL: define {{.*}}@_Z5derefv
// LLVM: load i64, ptr getelementptr {{.*}}(i8, ptr @s, i64 16)
