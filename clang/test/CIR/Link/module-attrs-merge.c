// Module attributes some inputs have and others do not merge as the classic
// link merges what they lower to, in either input order:
// - cir.uwtable, from TUs built with and without unwind tables, is the
//   "uwtable" module flag, which LLVM merges by Max with an absent flag having
//   no say: the strongest kind any input asked for; each function keeps its own.
// - cir.emit_errno_tbaa, set only where the TU had TBAA (not at -O0), is the
//   !llvm.errno.tbaa named metadata, which the IR linker appends from every
//   module that has it.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/async.c -o %t/async.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O0 -fno-unwind-tables \
// RUN:     -fno-asynchronous-unwind-tables -c %t/none.c -o %t/none.o
// RUN: cir-link %t/none.o %t/async.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/none-first.ll
// RUN: FileCheck --input-file=%t/none-first.ll %s
// RUN: cir-link %t/async.o %t/none.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/async-first.ll
// RUN: FileCheck --input-file=%t/async-first.ll %s
//
// CHECK-DAG: define {{.*}}i32 @with_tables({{.*}}) #[[WITH:[0-9]+]]
// CHECK-DAG: define {{.*}}i32 @without_tables({{.*}}) #[[WITHOUT:[0-9]+]]
// CHECK-DAG: attributes #[[WITH]] = {{{.*}}uwtable{{.*}}}
// CHECK-DAG: !{i32 7, !"uwtable", i32 2}
// CHECK-DAG: !llvm.errno.tbaa = !{

//--- async.c
int with_tables(int x) { return x + 1; }

//--- none.c
int without_tables(int x) { return x * 2; }
