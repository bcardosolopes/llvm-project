// The calling convention of a fat object's CIR, lowered by cir-link.
//
// clang runs CallConvLowering while lowering to LLVM IR, and
// -fclangir-fat-objects captures the CIR before that. cir-link lowered the
// captured module without the pass, so every aggregate return stayed by value:
// no sret, where the TU's own native code has one. Inside the merged module
// that is merely consistent; against anything outside it, it is a different
// calling convention. It was also the cost that stopped one large library: a
// 192 KB std::array returned by value became one 144,000-instruction basic
// block, which instruction selection did not finish in an hour.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/one.cpp -o %t/one.o
//
// The captured module is at cir-prepare, with CallConvLowering still to run,
// and says so through the merge.
// RUN: cir-link %t/one.o -o %t/one.cir
// RUN: cir-opt %t/one.cir | FileCheck %s --check-prefix=STAGE
// STAGE: module {{.*}}cir.stage = "cir-prepare"
//
// Lowered by cir-link, the return goes through sret, as clang lowers the same
// TU.
// RUN: cir-link %t/one.o --emit-llvm --emit-llvm-text -o %t/one.ll
// RUN: FileCheck --input-file=%t/one.ll %s
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -O2 -S -emit-llvm %t/one.cpp -o %t/one.clang.ll
// RUN: FileCheck --input-file=%t/one.clang.ll %s
// CHECK: define {{.*}}void @_Z4makev(ptr {{.*}}sret(%struct.Big)
// CHECK-NOT: cir.stage
//
// A merged module is lowered as one, so inputs at different stages are
// refused: here one that has had CallConvLowering and one that has not.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -O2 -c %t/other.cpp -o %t/other.o
// RUN: cir-opt %t/one.cir -cir-call-conv-lowering -o %t/one.abi.cir
// RUN: not cir-link %t/other.o %t/one.abi.cir -o %t/mixed.cir 2>&1 \
// RUN:     | FileCheck %s --check-prefix=MIXED
// MIXED: one.abi.cir:0:0: error: input is at CIR stage cir-abi and earlier inputs are at cir-prepare; a merged module is lowered as one
//
// Whoever lowers a fat object runs CallConvLowering, so the pass cannot be
// turned off for one.
// RUN: not %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -fno-clangir-call-conv-lowering -O2 \
// RUN:     -c %t/one.cpp -o %t/off.o 2>&1 | FileCheck %s --check-prefix=OFF
// OFF: error: invalid argument '-fno-clangir-call-conv-lowering' not allowed with '-fclangir-fat-objects'

//--- one.cpp
struct Big { float v[64]; };
Big make() {
  Big b{};
  b.v[3] = 1;
  return b;
}

//--- other.cpp
int other(int x) { return x + 1; }
