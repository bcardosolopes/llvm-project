// cir-link --inline runs the CIR inliner on the merged module -- across TU
// boundaries -- and cir-translate --inline lowers the merged module the same
// way. Without it, or with --cir-inline-threshold=0, nothing is inlined.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/a.cpp -o %t/a.o
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/b.cpp -o %t/b.o
//
// RUN: cir-link %t/a.o %t/b.o --inline --emit-llvm --emit-llvm-text \
// RUN:     -o %t/inlined.ll
// RUN: FileCheck --input-file=%t/inlined.ll %s --check-prefix=INLINED
// RUN: cir-link %t/a.o %t/b.o -o %t/merged.cir
// RUN: cir-translate %t/merged.cir -cir-to-llvmir --inline \
// RUN:     -o %t/inlined.translated.ll
// RUN: FileCheck --input-file=%t/inlined.translated.ll %s --check-prefix=INLINED
//
// RUN: cir-link %t/a.o %t/b.o --emit-llvm --emit-llvm-text -o %t/plain.ll
// RUN: FileCheck --input-file=%t/plain.ll %s --check-prefix=CALL
// RUN: cir-link %t/a.o %t/b.o --inline --cir-inline-threshold=0 --emit-llvm \
// RUN:     --emit-llvm-text -o %t/threshold0.ll
// RUN: FileCheck --input-file=%t/threshold0.ll %s --check-prefix=CALL
// RUN: cir-translate %t/merged.cir -cir-to-llvmir --inline \
// RUN:     --cir-inline-threshold=0 -o %t/threshold0.translated.ll
// RUN: FileCheck --input-file=%t/threshold0.translated.ll %s --check-prefix=CALL
//
// INLINED-LABEL: define {{.*}}i32 @_Z3usei(
// INLINED-NOT:     call
// INLINED:         ret i32
// CALL-LABEL:    define {{.*}}i32 @_Z3usei(
// CALL:            call {{.*}}@_Z5scalei(

//--- a.cpp
int scale(int x) { return x * 3; }

//--- b.cpp
int scale(int);
int use(int y) { return scale(y) + 1; }
