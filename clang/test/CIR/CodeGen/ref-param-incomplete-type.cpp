// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

// Regression test: reference parameters to forward-declared (incomplete) types
// must not crash when emitting parameter attributes. The align attribute
// requires getTypeAlignInChars which needs a complete type for record layout.
// For incomplete types, alignment 1 is used (matching OGCG).

class Incomplete;

struct Complete {
  int x;
  double y;
};

// Function with reference to forward-declared class - must not crash.
// Incomplete type gets align 1, no dereferenceable.
void takes_incomplete_ref(const Incomplete& c) {}

// CIR: cir.func {{.*}}@_Z20takes_incomplete_refRK10Incomplete(
// CIR-SAME: %arg0: !cir.ptr<!rec_Incomplete> {cir.align = 1 : i64, cir.callsite_nonnull, cir.nonnull, cir.noundef}

// LLVM: define {{.*}}void @_Z20takes_incomplete_refRK10Incomplete(ptr noundef nonnull align 1 %{{.*}})

// OGCG: define {{.*}}void @_Z20takes_incomplete_refRK10Incomplete(ptr noundef nonnull align 1 %{{.*}})

// Function with reference to complete class - should get proper align attribute.
void takes_complete_ref(const Complete& c) {}

// CIR: cir.func {{.*}}@_Z18takes_complete_refRK8Complete(
// CIR-SAME: %arg0: !cir.ptr<!rec_Complete> {cir.align = 8 : i64, cir.callsite_nonnull, cir.dereferenceable = 16 : i64, cir.nonnull, cir.noundef}

// LLVM: define {{.*}}void @_Z18takes_complete_refRK8Complete(ptr noundef nonnull align 8 dereferenceable(16) %{{.*}})

// OGCG: define {{.*}}void @_Z18takes_complete_refRK8Complete(ptr noundef nonnull align 8 dereferenceable(16) %{{.*}})

// Struct with reference members to incomplete types - must not crash.
struct HasIncompleteRefs {
  const Incomplete &c1;
  int i;
  const Incomplete &c2;
};

void init_struct_with_refs(const Incomplete& c) {
  HasIncompleteRefs s = {c, 42, c};
}

// CIR: cir.func {{.*}}@_Z21init_struct_with_refsRK10Incomplete(
// CIR-SAME: %arg0: !cir.ptr<!rec_Incomplete> {cir.align = 1 : i64, cir.callsite_nonnull, cir.nonnull, cir.noundef}

// LLVM: define {{.*}}void @_Z21init_struct_with_refsRK10Incomplete(ptr noundef nonnull align 1 %{{.*}})

// OGCG: define {{.*}}void @_Z21init_struct_with_refsRK10Incomplete(ptr noundef nonnull align 1 %{{.*}})
