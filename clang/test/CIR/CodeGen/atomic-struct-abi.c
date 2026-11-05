// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --check-prefix=OGCG %s --input-file=%t.og.ll
// _Atomic struct types must be passed indirectly (sret/byval),
// not by value in registers. CIR was stripping the AtomicType during
// type conversion, causing the ABI classifier to pass them by value.

struct Small { int x; };

_Atomic struct Small atomic_fn(_Atomic struct Small x) { return x; }

// Both CIR and OGCG should pass indirectly with sret/byval, not i32.
// LLVM: define dso_local void @atomic_fn(ptr dead_on_unwind noalias writable sret(%struct.Small) align 4 %{{.*}}, ptr noundef byval(%struct.Small) align 8 %{{.*}})
// OGCG: define dso_local void @atomic_fn(ptr dead_on_unwind noalias writable sret(%struct.Small) align 4 %{{.*}}, ptr noundef byval(%struct.Small) align 8 %{{.*}})
