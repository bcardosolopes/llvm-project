// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:   -fsanitize=null,alignment -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu \
// RUN:   -fsanitize=null,alignment -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG

// An __attribute__((annotate)) local variable emits its translation-unit
// filename string into section "llvm.metadata" (as @.str.annot*). A UBSan
// null/type check on a pointer dereference emits a source-location descriptor
// that also needs the same filename string. These must NOT be merged: the
// UBSan descriptor is a real, emitted global, so if it referenced the
// "llvm.metadata" annotation string (which the AsmPrinter drops), assembly
// would fail with "Undefined temporary symbol .L.str.annot". The UBSan
// filename must be a separate, sectionless @.src global (matching OGCG).

int deref(int *p) {
  __attribute__((annotate("ann"))) int local = 0;
  return *p + local;
}

// The annotation filename string lives in llvm.metadata.
// LLVM: @.str.annot{{.*}} = private unnamed_addr constant {{.*}}, section "llvm.metadata"

// The UBSan source-location filename is a separate, sectionless @.src global.
// LLVM: @.src = private unnamed_addr constant {{.*}}
// LLVM-NOT: @.src = private unnamed_addr constant {{.*}}section "llvm.metadata"

// The UBSan type-mismatch descriptor must reference @.src, NOT @.str.annot.
// LLVM: @{{[0-9]+}} = private unnamed_addr global {{.*}}ptr @.src

// OGCG: @.src = private unnamed_addr constant
// OGCG: @{{[0-9]+}} = private unnamed_addr global {{.*}}ptr @.src
