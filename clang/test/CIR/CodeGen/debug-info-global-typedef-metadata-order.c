// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm -debug-info-kind=limited %s -o - | FileCheck %s

// Regression coverage for metadata-order convergence:
// keep OGCG textual ordering of debug-related named metadata and module flags.

typedef const int cint;
cint gc = 7;

// CHECK: !llvm.dbg.cu = !{![[CU:[0-9]+]]}
// CHECK-NEXT: !llvm.module.flags = !{![[DBGVER:[0-9]+]], ![[WCHAR:[0-9]+]]}
// CHECK-NEXT: !llvm.ident = !{![[IDENT:[0-9]+]]}
// CHECK: ![[DBGVER]] = !{i32 2, !"Debug Info Version", i32 3}
// CHECK: ![[WCHAR]] = !{i32 1, !"wchar_size", i32 4}
// CHECK: ![[IDENT]] = !{!"{{[^"]+}}"}
