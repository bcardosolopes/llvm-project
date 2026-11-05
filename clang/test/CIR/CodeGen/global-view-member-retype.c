// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// A constant pointer to a struct member whose type differs from the first
// member forces an explicit alignment-padding member into the (anonymous) C
// record type. In C that record type is completed *after* the referencing
// pointer constant is first emitted, so the global-view rewriter
// (createNewGlobalView) re-indexes the view for the padded layout. It must
// preserve the view's member pointee type (`!cir.ptr<!s64i>`); previously it
// derived the type from the whole new record type (`!cir.ptr<!rec_anon_struct>`),
// which mistyped every indexed member view as a pointer to the entire record
// and broke CIR verification at the load/use sites.

struct S { int a; long c; };
struct S s = { 1, 42 };

// The completed record has an explicit [4 x i8] padding member, so `c` lands at
// member index [2]. The view's pointee type must stay `!s64i` (a `long`), not
// the record type.

// (1) File-scope global initializer path.
const long *const gp = &s.c;
// CIR-DAG: cir.global {{.*}}@gp = #cir.global_view<@s, [2 : i32]> : !cir.ptr<!s64i>
// LLVM-DAG: @gp = constant ptr getelementptr (i8, ptr @s, i64 8)

// (2) Function-body const-folded path.
int deref(void) {
  static const long *const p = &s.c;
  return (int)*p;
}
// CIR-DAG: cir.global {{.*}}@deref.p = #cir.global_view<@s, [2 : i32]> : !cir.ptr<!s64i>
// CIR-LABEL: cir.func{{.*}} @deref
// CIR: %[[V:.*]] = cir.const #cir.global_view<@s, [2 : i32]> : !cir.ptr<!s64i>
// CIR: cir.load align(8) %[[V]] : !cir.ptr<!s64i>, !s64i

// LLVM-LABEL: define {{.*}}@deref
// LLVM: load i64, ptr getelementptr {{.*}}(i8, ptr @s, i64 8)
