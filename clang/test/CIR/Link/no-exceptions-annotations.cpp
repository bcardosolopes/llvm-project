// A -fno-exceptions TU's hidden inline functions get their own name in the
// merge (.cirlink.noexcept), and the module's per-TU lists have to follow:
// they name their symbols with plain strings, not symbol references. An
// __attribute__((annotate)) on such a function kept the original name, which
// nothing in the merged module defined, and lowering the annotation asserted
// on an address-of an undefined function (libwebrtcLatest.so).
//
// RUN: rm -rf %t && split-file %s %t
// The marker that a TU was built without exceptions travels in fat objects.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir \
// RUN:     -fclangir-fat-objects -fno-exceptions -O1 -c %t/a.cpp -o %t/a.o
// RUN: cir-link %t/a.o -o %t/a.merged.cir --emit-bytecode=false
// RUN: FileCheck --input-file=%t/a.merged.cir %s --check-prefix=CIR
// RUN: cir-link %t/a.o --emit-llvm --emit-llvm-text -o %t/a.ll
// RUN: FileCheck --input-file=%t/a.ll %s
//
// CIR: cir.global_annotations = #cir<global_annotations [
// CIR-SAME: ["_Z7inlinevv.cirlink.noexcept", #cir.annotation<name = "inline-me"
//
// CHECK: @llvm.global.annotations = appending global
// CHECK-SAME: ptr @_Z7inlinevv.cirlink.noexcept,

//--- a.cpp
__attribute__((visibility("hidden"), annotate("inline-me"))) inline int
inlinev() {
  return 42;
}
int use() { return inlinev(); }
