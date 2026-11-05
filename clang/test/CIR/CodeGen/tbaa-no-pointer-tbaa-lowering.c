// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -O2 -disable-llvm-passes -no-pointer-tbaa %s -o %t.cir.off.ll
// RUN: FileCheck --input-file=%t.cir.off.ll %s --check-prefix=CIR-OFF
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -O2 -disable-llvm-passes -no-pointer-tbaa %s -o %t.og.off.ll
// RUN: FileCheck --input-file=%t.og.off.ll %s --check-prefix=OGCG-OFF

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm -O2 -disable-llvm-passes %s -o %t.cir.on.ll
// RUN: FileCheck --input-file=%t.cir.on.ll %s --check-prefix=CIR-ON
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm -O2 -disable-llvm-passes %s -o %t.og.on.ll
// RUN: FileCheck --input-file=%t.og.on.ll %s --check-prefix=OGCG-ON

// The !tbaa.struct on this byval copy is built by the CIR->LLVM call-conv pass,
// which reconstructs its own CodeGenOptions from module attributes. -no-pointer-tbaa
// reached CIRGen but never that pass, so PointerTBAA stayed at its default of 1 and
// each pointer field was tagged "p1 int" while OGCG used "any pointer" -- two TBAA
// nodes describing one pointer type. The -ON prefixes pin that the default is
// untouched.

struct Big { int *p; int *q; int *r; };
void takeBig(struct Big);
void f(struct Big *b) { takeBig(*b); }

// CIR-OFF-LABEL: define{{.*}} void @f(
// CIR-OFF: call void @llvm.memcpy{{.*}}(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 24, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// CIR-OFF: !{!"Simple C/C++ TBAA"}
// CIR-OFF: ![[TAG:[0-9]+]] = !{![[ANYPTR:[0-9]+]], ![[ANYPTR]], i64 0}
// CIR-OFF: ![[ANYPTR]] = !{!"any pointer", !{{[0-9]+}}, i64 0}
// CIR-OFF: ![[TS]] = !{i64 0, i64 8, ![[TAG]], i64 8, i64 8, ![[TAG]], i64 16, i64 8, ![[TAG]]}

// OGCG-OFF-LABEL: define{{.*}} void @f(
// OGCG-OFF: call void @llvm.memcpy{{.*}}(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 24, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// OGCG-OFF: !{!"Simple C/C++ TBAA"}
// OGCG-OFF: ![[TAG:[0-9]+]] = !{![[ANYPTR:[0-9]+]], ![[ANYPTR]], i64 0}
// OGCG-OFF: ![[ANYPTR]] = !{!"any pointer", !{{[0-9]+}}, i64 0}
// OGCG-OFF: ![[TS]] = !{i64 0, i64 8, ![[TAG]], i64 8, i64 8, ![[TAG]], i64 16, i64 8, ![[TAG]]}

// CIR-ON-LABEL: define{{.*}} void @f(
// CIR-ON: call void @llvm.memcpy{{.*}}(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 24, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// CIR-ON: ![[TS]] = !{i64 0, i64 8, ![[TAG:[0-9]+]], i64 8, i64 8, ![[TAG]], i64 16, i64 8, ![[TAG]]}
// CIR-ON: ![[TAG]] = !{![[P1INT:[0-9]+]], ![[P1INT]], i64 0}
// CIR-ON: ![[P1INT]] = !{!"p1 int", !{{[0-9]+}}, i64 0}

// OGCG-ON-LABEL: define{{.*}} void @f(
// OGCG-ON: call void @llvm.memcpy{{.*}}(ptr align 8 {{.*}}, ptr align 8 {{.*}}, i64 24, i1 false), !tbaa.struct ![[TS:[0-9]+]]
// OGCG-ON: ![[TS]] = !{i64 0, i64 8, ![[TAG:[0-9]+]], i64 8, i64 8, ![[TAG]], i64 16, i64 8, ![[TAG]]}
// OGCG-ON: ![[TAG]] = !{![[P1INT:[0-9]+]], ![[P1INT]], i64 0}
// OGCG-ON: ![[P1INT]] = !{!"p1 int", !{{[0-9]+}}, i64 0}
