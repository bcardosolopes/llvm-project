// RUN: %clang -S -emit-llvm -O2 -fclangir %s -o - | FileCheck %s --check-prefix=LLVMD
// RUN: %clang -S -emit-llvm -O2 -fno-clangir %s -o - | FileCheck %s --check-prefix=OGCGD

_Complex double f(_Complex double a, _Complex double b) { return a * b; }

// LLVMD-LABEL: define {{.*}} @_Z1fCdS_(
// LLVMD: fcmp uno double
// LLVMD: br i1
// LLVMD: fcmp uno double
// LLVMD: br i1
// LLVMD: call{{.*}} { double, double } @__muldc3(
// LLVMD-NOT: phi { double, double }
// LLVMD: phi double
// LLVMD: phi double
// LLVMD: ret { double, double }

// OGCGD-LABEL: define {{.*}} @_Z1fCdS_(
// OGCGD: fcmp uno double
// OGCGD: fcmp uno double
// OGCGD: br i1
// OGCGD: call{{.*}} { double, double } @__muldc3(
// OGCGD: phi double
// OGCGD: phi double
// OGCGD: ret { double, double }
