// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.og.ll %s

using VFP = void (*)(int &);
void call_ref_arg(VFP fp, int &x) { fp(x); }

using RFP = int &(*)(int &);
int &call_ref_ret(RFP fp, int &x) { return fp(x); }

// CIR-LABEL: define dso_local void @_Z12call_ref_argPFvRiES_(
// CIR: call void %{{.+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{.+}})
//
// OGCG-LABEL: define dso_local void @_Z12call_ref_argPFvRiES_(
// OGCG: call void %{{.+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{.+}})

// CIR-LABEL: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z12call_ref_retPFRiS_ES_(
// CIR: call noundef nonnull align 4 dereferenceable(4) ptr %{{.+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{.+}})
//
// OGCG-LABEL: define dso_local noundef nonnull align 4 dereferenceable(4) ptr @_Z12call_ref_retPFRiS_ES_(
// OGCG: call noundef nonnull align 4 dereferenceable(4) ptr %{{.+}}(ptr noundef nonnull align 4 dereferenceable(4) %{{.+}})

