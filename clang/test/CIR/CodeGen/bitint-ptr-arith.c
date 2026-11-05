// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -fclangir -emit-llvm %s -o %t.x64.cir.ll
// RUN: FileCheck --input-file=%t.x64.cir.ll %s --check-prefix=X64
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -emit-llvm %s -o %t.x64.ogcg.ll
// RUN: FileCheck --input-file=%t.x64.ogcg.ll %s --check-prefix=X64-OGCG
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -fclangir -emit-llvm %s -o %t.a64.cir.ll
// RUN: FileCheck --input-file=%t.a64.cir.ll %s --check-prefix=A64
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexperimental-max-bitint-width=1024 -emit-llvm %s -o %t.a64.ogcg.ll
// RUN: FileCheck --input-file=%t.a64.ogcg.ll %s --check-prefix=A64-OGCG

// Large _BitInt pointer arithmetic must use the ABI storage object as the GEP
// element type. On x86_64 that changes the stride for non-power-of-two widths;
// on AArch64 it should still match OGCG's widened storage integer.

_BitInt(129) *step129(_BitInt(129) *p) {
  return p + 1;
}

_BitInt(257) *step257(_BitInt(257) *p) {
  return p + 1;
}

// X64-LABEL: define {{.*}} ptr @step129(
// X64: getelementptr inbounds [24 x i8], ptr %{{.+}}, i64 1
// X64-LABEL: define {{.*}} ptr @step257(
// X64: getelementptr inbounds [40 x i8], ptr %{{.+}}, i64 1

// X64-OGCG-LABEL: define {{.*}} ptr @step129(
// X64-OGCG: getelementptr inbounds [24 x i8], ptr %{{.+}}, i64 1
// X64-OGCG-LABEL: define {{.*}} ptr @step257(
// X64-OGCG: getelementptr inbounds [40 x i8], ptr %{{.+}}, i64 1

// A64-LABEL: define {{.*}} ptr @step129(
// A64: getelementptr inbounds i256, ptr %{{.+}}, i64 1
// A64-LABEL: define {{.*}} ptr @step257(
// A64: getelementptr inbounds i384, ptr %{{.+}}, i64 1

// A64-OGCG-LABEL: define {{.*}} ptr @step129(
// A64-OGCG: getelementptr inbounds i256, ptr %{{.+}}, i64 1
// A64-OGCG-LABEL: define {{.*}} ptr @step257(
// A64-OGCG: getelementptr inbounds i384, ptr %{{.+}}, i64 1
