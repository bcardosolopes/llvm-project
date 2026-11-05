// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t-ogcg.ll
// RUN: FileCheck --input-file=%t-ogcg.ll --check-prefix=OGCG %s

// Test that _BitInt types use the correct storage type in memory
// (rounded up to the next power-of-two), matching OGCG behavior.

struct HasBitInts {
  _BitInt(35) a;
  _BitInt(7) b;
  _BitInt(17) c;
};

// Struct layout must use storage types, matching OGCG.
// CIR: !rec_HasBitInts = !cir.record<struct "HasBitInts" {!s64i, !s8i, !s32i}>
// LLVM: %struct.HasBitInts = type { i64, i8, i32 }
// OGCG: %struct.HasBitInts = type { i64, i8, i32 }

void store_members(HasBitInts *s) {
  s->a = 42;
  s->b = 3;
  s->c = 100;
}

// Struct member stores use storage-width types.
// CIR-LABEL: @_Z13store_membersP10HasBitInts
// CIR: cir.get_member {{.*}}[0] {name = "a"} : !cir.ptr<!rec_HasBitInts> -> !cir.ptr<!s64i>
// CIR: cir.store {{.*}} : !s64i, !cir.ptr<!s64i>
// CIR: cir.get_member {{.*}}[1] {name = "b"} : !cir.ptr<!rec_HasBitInts> -> !cir.ptr<!s8i>
// CIR: cir.store {{.*}} : !s8i, !cir.ptr<!s8i>
// CIR: cir.get_member {{.*}}[2] {name = "c"} : !cir.ptr<!rec_HasBitInts> -> !cir.ptr<!s32i>
// CIR: cir.store {{.*}} : !s32i, !cir.ptr<!s32i>

// LLVM-LABEL: @_Z13store_membersP10HasBitInts
// LLVM: store i64 42, ptr %{{.*}}, align 8
// LLVM: store i8 3, ptr %{{.*}}, align 8
// LLVM: store i32 100, ptr %{{.*}}, align 4

// OGCG-LABEL: @_Z13store_membersP10HasBitInts
// OGCG: store i64 42, ptr %{{.*}}, align 8
// OGCG: store i8 3, ptr %{{.*}}, align 8
// OGCG: store i32 100, ptr %{{.*}}, align 4

void local_init() {
  _BitInt(10) x = 42;
}

// Local alloca uses storage type. emitToMemory folds constant to storage width.
// CIR-LABEL: @_Z10local_initv
// CIR: cir.alloca !s16i, !cir.ptr<!s16i>, ["x", init]
// CIR: cir.const #cir.int<42> : !s16i
// CIR: cir.store {{.*}} : !s16i, !cir.ptr<!s16i>

// LLVM-LABEL: @_Z10local_initv
// LLVM: store i16 42, ptr %{{.*}}, align 2

// OGCG-LABEL: @_Z10local_initv
// OGCG: store i16 42, ptr %{{.*}}, align 2

_BitInt(10) load_local() {
  _BitInt(10) x = 99;
  return x;
}

// Load from storage-type alloca truncates back to semantic type via
// emitFromMemory. Arithmetic uses semantic width.
// CIR-LABEL: @_Z10load_localv
// CIR: %[[LOAD:.*]] = cir.load {{.*}} : !cir.ptr<!s16i>, !s16i
// CIR: %[[TRUNC:.*]] = cir.cast integral %[[LOAD]] : !s16i -> !cir.int<s, 10>

// LLVM-LABEL: @_Z10load_localv
// LLVM: %[[LOAD:.*]] = load i16, ptr %{{.*}}
// LLVM: trunc i16 %[[LOAD]] to i10

// OGCG-LABEL: @_Z10load_localv
// OGCG: %[[LOAD:.*]] = load i16, ptr %{{.*}}
// OGCG: trunc i16 %[[LOAD]] to i10

// Verify correct struct offsets with storage types.
struct OffsetCheck {
  _BitInt(17) a;
  _BitInt(10) b;
  _BitInt(17) c;
};

void check_offsets() {
  int a = __builtin_offsetof(struct OffsetCheck, a);
  int b = __builtin_offsetof(struct OffsetCheck, b);
  int c = __builtin_offsetof(struct OffsetCheck, c);
}

// CIR-LABEL: @_Z13check_offsetsv
// CIR: cir.const #cir.int<0> : !s32i
// CIR: cir.const #cir.int<4> : !s32i
// CIR: cir.const #cir.int<8> : !s32i

// OGCG-LABEL: @_Z13check_offsetsv
// OGCG: store i32 0
// OGCG: store i32 4
// OGCG: store i32 8
