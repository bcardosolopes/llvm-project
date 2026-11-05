// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fclangir %s -o - | FileCheck %s --check-prefix=LLVMF
// RUN: %clang --target=x86_64-unknown-linux-gnu -S -emit-llvm -O2 -fno-clangir %s -o - | FileCheck %s --check-prefix=OGCGF

_Complex float f(_Complex float a, _Complex float b) { return a * b; }

// LLVMF-LABEL: define {{.*}} @f(
// LLVMF: fcmp uno float
// LLVMF: br i1
// LLVMF: fcmp uno float
// LLVMF: br i1
// LLVMF: call <2 x float> @__mulsc3(
// LLVMF: phi <2 x float>
// LLVMF: ret <2 x float>

// OGCGF-LABEL: define {{.*}} @f(
// OGCGF: fcmp uno float
// OGCGF: fcmp uno float
// OGCGF: br i1
// OGCGF: call <2 x float> @__mulsc3(
// OGCGF: phi <2 x float>
// OGCGF: ret <2 x float>
