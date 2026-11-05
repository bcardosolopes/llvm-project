// RUN: %clang_cc1 -x c++ -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CXX
// RUN: %clang_cc1 -x c -std=c2x -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=C

// Both CXX and C2X don't support no-prototype functions. They default to void.
int noProto();
int test(int x) {
  return noProto();
  // CXX: {{.+}} = cir.call @{{.*}}noProto{{.*}}() : () -> !s32i
  // C: {{.+}} = cir.call @{{.*}}noProto{{.*}}() : () -> !s32i
}
int noProto() { return 0; }
// CXX: cir.func {{.*}} @{{.*}}noProto{{.*}}() -> (!s32i {cir.noundef})
// C: cir.func {{.*}} @{{.*}}noProto{{.*}}() -> !s32i
