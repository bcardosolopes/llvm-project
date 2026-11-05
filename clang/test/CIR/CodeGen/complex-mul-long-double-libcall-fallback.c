// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fclangir %s -o - | FileCheck %s --check-prefix=LLVMLD
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fno-clangir %s -o - | FileCheck %s --check-prefix=OGCGLD

_Complex long double f(_Complex long double a, _Complex long double b) {
  return a * b;
}

// LLVMLD-LABEL: define {{.*}} @f(
// LLVMLD: fcmp uno x86_fp80
// LLVMLD: br i1
// LLVMLD: fcmp uno x86_fp80
// LLVMLD: br i1
// LLVMLD: call { x86_fp80, x86_fp80 } @__mulxc3(
// LLVMLD: phi x86_fp80
// LLVMLD: phi x86_fp80
// LLVMLD: ret { x86_fp80, x86_fp80 }

// OGCGLD-LABEL: define {{.*}} @f(
// OGCGLD: fcmp uno x86_fp80
// OGCGLD: fcmp uno x86_fp80
// OGCGLD: br i1
// OGCGLD: call { x86_fp80, x86_fp80 } @__mulxc3(
// OGCGLD: phi x86_fp80
// OGCGLD: phi x86_fp80
// OGCGLD: ret { x86_fp80, x86_fp80 }
