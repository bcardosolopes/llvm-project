// RUN: llvm-profdata merge -o %t.profdata %S/../../CodeGen/Inputs/pgotestclang.profraw
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fno-clangir-direct-lowering -emit-llvm %s -o - \
// RUN:   -fprofile-instrument-use=clang \
// RUN:   -fprofile-instrument-use-path=%t.profdata \
// RUN:   -fprofile-remapping-file=%t.nonexistent.remap 2>&1 | FileCheck %s

// Match OGCG behavior on this path as well: remapping-file load failures are
// surfaced through err_reading_profile and reported against the profile path.
// CHECK: error: error in reading profile {{.*}}.profdata:
// CHECK-NOT: .nonexistent.remap
// CHECK-NOT: CIR-to-CIR transformation failed

int f(int x) { return x ? 1 : 2; }
