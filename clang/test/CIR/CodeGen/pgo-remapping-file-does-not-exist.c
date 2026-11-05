// RUN: llvm-profdata merge -o %t.profdata %S/../../CodeGen/Inputs/pgotestclang.profraw
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   %s -o - -fprofile-instrument-use=clang \
// RUN:   -fprofile-instrument-use-path=%t.profdata \
// RUN:   -fprofile-remapping-file=%t.nonexistent.remap 2>&1 | FileCheck %s

// Match OGCG behavior: on remapping-file load failure, the user-visible
// clang diagnostic still reports the profile path in err_reading_profile.
// CHECK: error: error in reading profile {{.*}}.profdata:
// CHECK-NOT: .nonexistent.remap
// CHECK-NOT: CIR-to-CIR transformation failed

int f(int x) { return x ? 1 : 2; }
