// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// Weakref pointer base in ConstantLValueEmitter

// Variable weakref
static int x __attribute__((weakref("y")));
int *p = &x;

// Variable weakref with memory type widening
static _BitInt(35) wb __attribute__((weakref("wext")));
_BitInt(35) *wp = &wb;

// CIR-DAG: cir.global "private" extern_weak @y : !s32i
// CIR-DAG: cir.global external @p = #cir.global_view<@y>
// CIR-DAG: cir.global "private" extern_weak @wext : !s64i
// CIR-DAG: cir.global external @wp = #cir.global_view<@wext> : !cir.ptr<!cir.int<s, 35>>

// LLVM-DAG: @y = extern_weak global i32
// LLVM-DAG: @p = global ptr @y
// LLVM-DAG: @wext = extern_weak global i64
// LLVM-DAG: @wp = global ptr @wext

// Function weakref
extern void B(void);
static __typeof(B) A __attribute__((__weakref__("B")));
int active(void) {
  static void *const q = __extension__(void *)&A;
  return q != 0;
}

int read_x(void) { return x; }
void write_x(int v) { x = v; }

// CIR-DAG: cir.func extern_weak private @B()
// CIR-DAG: cir.global "private" constant internal dso_local @active.q = #cir.global_view<@B>
// CIR-DAG: cir.func {{.*}} @read_x()
// CIR-DAG: cir.get_global @y : !cir.ptr<!s32i>
// CIR-DAG: cir.func {{.*}} @write_x
// CIR-DAG: cir.get_global @y : !cir.ptr<!s32i>

// LLVM-DAG: declare extern_weak void @B()
// LLVM-DAG: @active.q = internal constant ptr @B
// LLVM-DAG: define dso_local i32 @read_x()
// LLVM-DAG: load i32, ptr @y
// LLVM-DAG: define dso_local void @write_x(i32 noundef %0)
// LLVM-DAG: store i32 {{.*}}, ptr @y
