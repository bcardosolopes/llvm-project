// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fcuda-is-device -target-cpu sm_80 -target-feature +ptx70 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fcuda-is-device -target-cpu sm_80 -target-feature +ptx70 -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// A block-scope `__shared__` array is allocated in the shared address space
// (addrspace 3) but referenced through a generic pointer. ClangIR must
// materialize that reference with an `addrspacecast` — matching classic
// CodeGen — so LLVM's address-space inference (NVPTXInferAddressSpaces) can
// recover the shared access. A previous lowering emitted an integer round-trip
// (`inttoptr(ptrtoint ...)`), which is opaque to the inference pass and forced
// the access to stay generic (a machine-code pessimization).

__attribute__((global)) void k(int *out) {
  __attribute__((shared)) int s[64];
  int *p = s;
  p[0] = 1;
  out[0] = p[63];
}

// CHECK-LABEL: define{{.*}}@_Z1kPi
// CHECK: addrspacecast (ptr addrspace(3){{.*}} to ptr)
// CHECK-NOT: ptrtoint

// OGCG-LABEL: define{{.*}}@_Z1kPi
// OGCG: addrspacecast (ptr addrspace(3){{.*}} to ptr)
