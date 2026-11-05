// A function one TU defines as an alias and another declares and calls. The
// alias cannot stand in for the declaration -- a cir.call names a cir.func --
// so the declaration becomes the alias, in either input order, and the call
// goes through it.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/alias.cpp -o %t/alias.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/call.cpp -o %t/call.o
// RUN: cir-link %t/alias.o %t/call.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/alias-first.ll
// RUN: FileCheck --input-file=%t/alias-first.ll %s
// RUN: cir-link %t/call.o %t/alias.o --emit-llvm --emit-llvm-text \
// RUN:     -o %t/call-first.ll
// RUN: FileCheck --input-file=%t/call-first.ll %s
//
// CHECK-DAG: @widen_alias = dso_local alias i16 (i16), ptr @widen
// CHECK-DAG: define dso_local i16 @widen(
// CHECK-DAG: call i16 @widen_alias(

//--- alias.cpp
extern "C" __attribute__((noinline)) short widen(short v) { return v + 1; }
extern "C" short widen_alias(short) __attribute__((alias("widen")));

//--- call.cpp
extern "C" short widen_alias(short);
int useAlias(short y) { return widen_alias(y) * 5; }
