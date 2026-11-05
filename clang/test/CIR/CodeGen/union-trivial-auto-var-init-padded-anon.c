// RUN: %clang_cc1 -triple aarch64-none-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=pattern %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Regression test for a CIRGen crash with -ftrivial-auto-var-init=pattern on a
// union whose member is a struct that contains internal padding.
//
// In `getPatternInitAttr` (CIRGenDecl.cpp), the union/unsized fast path
// recursively builds member init attrs and then calls
// `cir::ConstRecordAttr::get(recordTy, members)` reusing the union's
// *original* RecordType. When a member is a struct with implicit padding, the
// recursive struct path synthesizes a NEW RecordType with `padded=true` and
// explicit padding-byte members, and returns a ConstRecordAttr whose type is
// that padded variant — not the unpadded type recorded in the parent union's
// member list. ConstRecordAttr::verify then asserts:
//
//   element at index 0 has type
//     '!cir.record<struct  padded {!s32i, !cir.array<!u8i x 4>, !s64i}>'
//   but the expected type for this element is
//     '!cir.record<struct "anon.0" {!s32i, !s64i}>'
//
// The fix is to rebuild the union's RecordType in `getPatternInitAttr` from
// the actual member-attr types whenever a recursive call returned a different
// type, mirroring what the struct path already does.

union U {
  struct {
    int i;
    long l;
  } s;
};

void make(void) { union U params; }

// The fixed compiler synthesizes a parallel "padded" union type alongside the
// declared union, populates a private global with the pattern constant in the
// padded shape, and copies it into the unpadded alloca via a bitcast.

// CHECK-DAG: !rec_anon_struct = !cir.record<struct  padded {!s32i, !cir.array<!u8i x 4>, !s64i}>
// CHECK-DAG: !rec_anon_union = !cir.record<union  {!rec_anon_struct}>
// CHECK-DAG: !rec_U = !cir.record<union "U" {{.*}}>

// CHECK:      cir.global "private" constant cir_private @__const.make.params
// CHECK-SAME: = #cir.const_record<{#cir.const_record<{
// CHECK-SAME: #cir.int<-1431655766> : !s32i,
// CHECK-SAME: #cir.const_array<[#cir.int<170> : !u8i, #cir.int<170> : !u8i, #cir.int<170> : !u8i, #cir.int<170> : !u8i]> : !cir.array<!u8i x 4>,
// CHECK-SAME: #cir.int<-6148914691236517206> : !s64i
// CHECK-SAME: }> : !rec_anon_struct}> : !rec_anon_union

// CHECK-LABEL: cir.func{{.*}} @make
// CHECK:         %[[PARAMS:.*]] = cir.alloca !rec_U, !cir.ptr<!rec_U>, ["params"]
// CHECK:         %[[CAST:.*]] = cir.cast bitcast %[[PARAMS]] : !cir.ptr<!rec_U> -> !cir.ptr<!rec_anon_union>
// CHECK:         %[[INIT:.*]] = cir.get_global @__const.make.params : !cir.ptr<!rec_anon_union>
// CHECK:         cir.copy %[[INIT]] to %[[CAST]] {{.*}}cir.auto_init{{.*}}
// CHECK:         cir.return
