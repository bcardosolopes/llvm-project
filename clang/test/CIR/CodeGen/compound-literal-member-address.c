// RUN: %clang_cc1 -std=c2x -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR %s --input-file=%t.cir
// RUN: %clang_cc1 -std=c2x -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll

// The constant address of a non-first member of a file-scope compound literal
// must index the compound literal global's *padded* storage layout. The global
// is created with an explicit alignment-padding member ([4 x i8] between the int
// and the long), so `c` is at member index [2]; previously the global was typed
// without the padding, so the GlobalViewAttr index was computed against the
// unpadded layout ([1], the padding slot) and the pointer silently referenced
// the wrong field.

struct S { int a; long c; };

static const long *const p = &((const struct S){1, 42}).c;

int deref(void) { return (int)*p; }

// The compound literal global carries the explicit padding member...
// CIR: @{{.*}}compoundLiteral{{.*}} = #cir.const_record<{{.*}}!cir.array<!u8i x 4>{{.*}}!s64i{{.*}}>
// ...and the member view indexes past it, at [2], with the member pointee type.
// CIR: cir.const #cir.global_view<@{{.*}}compoundLiteral{{.*}}, [2 : i32]> : !cir.ptr<!s64i>

// LLVM: @{{.*}}compoundLiteral{{.*}} = internal constant { i32, [4 x i8], i64 }
// LLVM: load i64, ptr getelementptr {{.*}}(i8, ptr @{{.*}}compoundLiteral{{.*}}, i64 8)
