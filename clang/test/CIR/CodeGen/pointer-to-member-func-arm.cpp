// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -std=c++17 -fclangir -emit-cir -mmlir -mlir-print-ir-before=cir-cxxabi-lowering %s -o %t.cir 2> %t-before.cir
// RUN: FileCheck --check-prefix=CIR-BEFORE --input-file=%t-before.cir %s
// RUN: FileCheck --check-prefix=CIR-AFTER --input-file=%t.cir %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -std=c++17 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64-none-linux-android23 -std=c++17 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

struct Foo {
  void m1(int);
  virtual void m2(int);
  virtual void m3(int);
};

auto make_virtual() -> void (Foo::*)(int) {
  return &Foo::m3;
}

// CIR-BEFORE-LABEL: @_Z12make_virtualv
// CIR-BEFORE: %[[METHOD_PTR:.*]] = cir.const #cir.method<vtable_offset = 8> : !cir.method<!cir.func<(!s32i)> in !rec_Foo>

// CIR-AFTER-LABEL: @_Z12make_virtualv
// ARM ABI encodes virtual method ptr as {ptr = offset, adj = 2*thisAdj + 1}.
// CIR-AFTER: %[[METHOD_PTR:.*]] = cir.const #cir.const_record<{#cir.int<8> : !s64i, #cir.int<1> : !s64i}> : !rec_anon_struct

// LLVM-LABEL: @_Z12make_virtualv
// LLVM: store { i64, i64 } { i64 8, i64 1 }

// OGCG-LABEL: @_Z12make_virtualv
// OGCG: store { i64, i64 } { i64 8, i64 1 }

void call(Foo *obj, void (Foo::*func)(int), int arg) {
  (obj->*func)(arg);
}

// CIR-AFTER-LABEL: @_Z4callP3FooMS_FviEi
// In ARM ABI, virtual bit is read from adj (field #1), and this-adjustment is adj>>1.
// CIR-AFTER: %[[ADJ:.*]] = cir.extract_member %{{.*}}[1] : !rec_anon_struct -> !s64i
// CIR-AFTER: %[[ADJ_SHIFTED:.*]] = cir.shift(right, %[[ADJ]] : !s64i, %{{.*}} : !s64i) -> !s64i
// CIR-AFTER: %[[METHOD_PTR:.*]] = cir.extract_member %{{.*}}[0] : !rec_anon_struct -> !s64i
// CIR-AFTER: %[[VIRT_BIT:.*]] = cir.binop(and, %[[ADJ]], %{{.*}}) : !s64i
// CIR-AFTER: cir.ptr_stride %{{.*}}, %[[METHOD_PTR]] : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>

// LLVM-LABEL: @_Z4callP3FooMS_FviEi
// LLVM: %[[THIS_ADJ_RAW:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// LLVM: %[[THIS_ADJ:.*]] = ashr i64 %[[THIS_ADJ_RAW]], 1
// LLVM: %[[LLVM_ARM_PTR_FIELD:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// LLVM: %[[VIRT_BIT:.*]] = and i64 %[[THIS_ADJ_RAW]], 1
// LLVM: %[[IS_VIRTUAL:.*]] = icmp eq i64 %[[VIRT_BIT]], 1
// LLVM: getelementptr i8, ptr %{{.*}}, i64 %[[LLVM_ARM_PTR_FIELD]]

// OGCG-LABEL: @_Z4callP3FooMS_FviEi
// OGCG: %[[THIS_ADJ_RAW:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG: %[[THIS_ADJ:.*]] = ashr i64 %[[THIS_ADJ_RAW]], 1
// OGCG: %[[OGCG_ARM_PTR_FIELD:.*]] = extractvalue { i64, i64 } %{{.*}}, 0
// OGCG: %[[VIRT_BIT:.*]] = and i64 %[[THIS_ADJ_RAW]], 1
// OGCG: %[[IS_VIRTUAL:.*]] = icmp ne i64 %[[VIRT_BIT]], 0
// OGCG: getelementptr i8, ptr %{{.*}}, i64 %[[OGCG_ARM_PTR_FIELD]]

// A base->derived member-function-pointer conversion crossing a nonzero-offset
// (multiple-inheritance, non-primary) base. Under the ARM method-pointer ABI
// the this-adjustment lives in the adj field as 2*thisAdjust, so the crossed
// base byte offset (8) must be scaled by <<1 (== 16) before being added to adj.
struct L { long l; void fl(int); };
struct R { long r; void fr(int); };
struct D : L, R { long d; };

auto conv_base_to_derived(void (R::*p)(int)) -> void (D::*)(int) {
  return p;
}

// CIR-BEFORE-LABEL: @_Z20conv_base_to_derived
// CIR-BEFORE: cir.derived_method(%{{.*}} : !cir.method<!cir.func<(!s32i)> in !rec_R>) [8] -> !cir.method<!cir.func<(!s32i)> in !rec_D>

// CIR-AFTER-LABEL: @_Z20conv_base_to_derived
// CIR-AFTER: %[[ADJ:.*]] = cir.extract_member %{{.*}}[1] : !rec_anon_struct -> !s64i
// The raw base offset is 8; the ARM ABI scales the this-adjustment by <<1 == 16.
// CIR-AFTER: %[[OFFSET:.*]] = cir.const #cir.int<16> : !s64i
// CIR-AFTER: cir.binop(add, %[[ADJ]], %[[OFFSET]]) nsw : !s64i

// LLVM-LABEL: @_Z20conv_base_to_derived
// LLVM: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// LLVM: add nsw i64 %[[ADJ]], 16

// OGCG-LABEL: @_Z20conv_base_to_derived
// OGCG: %[[ADJ:.*]] = extractvalue { i64, i64 } %{{.*}}, 1
// OGCG: add nsw i64 %[[ADJ]], 16

// A constant *virtual* member-function pointer on a nonzero-offset base whose
// vtable offset is 0. Under the ARM ABI its function-pointer field is 0 (the
// vtable offset) and adj bit 0 is the virtual flag (== 1); it must NOT be
// folded to the null member pointer. After conversion the adj becomes
// 1 + (16 << 1) == 33.
struct VL { long l; virtual void fl(int); };
struct VR { long r; virtual void fr(int); };
struct VD : VL, VR { long d; };

auto conv_virtual_const() -> void (VD::*)(int) {
  return &VR::fr;
}

// CIR-BEFORE-LABEL: @_Z18conv_virtual_const
// CIR-BEFORE: cir.const #cir.method<vtable_offset = 0> : !cir.method<!cir.func<(!s32i)> in !rec_VR>

// CIR-AFTER-LABEL: @_Z18conv_virtual_const
// The {ptr = 0, adj = 1} virtual method ptr is preserved, not folded to null.
// CIR-AFTER: cir.const #cir.const_record<{#cir.int<0> : !s64i, #cir.int<1> : !s64i}> : !rec_anon_struct

// LLVM-LABEL: @_Z18conv_virtual_const
// LLVM: store { i64, i64 } { i64 0, i64 33 }

// OGCG-LABEL: @_Z18conv_virtual_const
// OGCG: store { i64, i64 } { i64 0, i64 33 }
