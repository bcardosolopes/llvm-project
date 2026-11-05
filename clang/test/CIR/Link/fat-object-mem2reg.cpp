// A fat object's CIR gets mem2reg when cir-link lowers it, after its
// CallConvLowering: nothing else on this path promotes locals before the link,
// and every one left becomes a load and a store a later optimizer has to clean up. Like
// the driver, it does so only for code compiled at -O1 and up.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/sum.cpp -o %t/sum.o
// RUN: cir-link %t/sum.o --emit-llvm --emit-llvm-text -o %t/sum.ll
// RUN: FileCheck --input-file=%t/sum.ll %s
//
// cir-translate, which the wrapper's split lowering uses, lowers the same way.
// RUN: cir-link %t/sum.o -o %t/sum.cir
// RUN: cir-translate %t/sum.cir -cir-to-llvmir -o %t/sum.translated.ll
// RUN: FileCheck --input-file=%t/sum.translated.ll %s
//
// CHECK-LABEL: define {{.*}}i32 @_Z3sumii(
// CHECK-NOT: alloca
// CHECK: ret i32
//
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O0 -c %t/sum.cpp -o %t/sum-O0.o
// RUN: cir-link %t/sum-O0.o --emit-llvm --emit-llvm-text -o %t/sum-O0.ll
// RUN: FileCheck --input-file=%t/sum-O0.ll %s --check-prefix=O0
//
// O0-LABEL: define {{.*}}i32 @_Z3sumii(
// O0: alloca

//--- sum.cpp
int sum(int a, int b) {
  int total = a;
  for (int i = 0; i < b; ++i)
    total += i;
  return total;
}

