// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Verify that cir.copy carries alignment attributes and that the lowered
// llvm.memcpy includes the correct alignment on the pointer arguments,
// matching OGCG output.

struct S { int x, y; };
struct __attribute__((packed)) P { int x, y; };
#pragma pack(push, 2)
struct P2 { int x, y; };
#pragma pack(pop)

// Non-packed struct: alignment 4
void copy_normal(struct S *a, struct S *b) { *b = *a; }

// CIR-LABEL: @copy_normal
// CIR:   cir.copy {{.*}} {dst_alignment = 4 : i64, src_alignment = 4 : i64} : !cir.ptr

// LLVM-LABEL: @copy_normal
// LLVM:   call void @llvm.memcpy.p0.p0.i64(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i64 8, i1 false)

// OGCG-LABEL: @copy_normal
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 4 {{.*}}, ptr align 4 {{.*}}, i64 8, i1 false)


// Packed struct: alignment 1
void copy_packed(struct P *a, struct P *b) { *b = *a; }

// CIR-LABEL: @copy_packed
// CIR:   cir.copy {{.*}} {dst_alignment = 1 : i64, src_alignment = 1 : i64} : !cir.ptr

// LLVM-LABEL: @copy_packed
// LLVM:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 8, i1 false)

// OGCG-LABEL: @copy_packed
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 1 {{.*}}, ptr align 1 {{.*}}, i64 8, i1 false)


// Pragma pack(2): alignment 2
void copy_pack2(struct P2 *a, struct P2 *b) { *b = *a; }

// CIR-LABEL: @copy_pack2
// CIR:   cir.copy {{.*}} {dst_alignment = 2 : i64, src_alignment = 2 : i64} : !cir.ptr

// LLVM-LABEL: @copy_pack2
// LLVM:   call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 2 {{.*}}, i64 8, i1 false)

// OGCG-LABEL: @copy_pack2
// OGCG:   call void @llvm.memcpy.p0.p0.i64(ptr align 2 {{.*}}, ptr align 2 {{.*}}, i64 8, i1 false)
