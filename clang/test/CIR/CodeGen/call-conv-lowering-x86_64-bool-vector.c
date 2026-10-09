// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM,LLVMCIR --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM,OGCG --input-file=%t.ll %s

// Packed boolean vectors classify as the integer clang packs them into: one
// bit per lane, at least a byte, rounded up to a power of two. The signatures
// below are anchored: the packed value passes with no extension attribute.
// Classic omits noundef on the rounded-up 32-bit coerce; CIR adds it per its
// convention.

// CIR: cir.func{{.*}} @take_b4(%arg0: !u8i {{.*}}) -> !u8i
// LLVM: define dso_local i8 @take_b4(i8 noundef %{{.*}}){{.*}}
typedef _Bool b4 __attribute__((ext_vector_type(4)));
b4 take_b4(b4 x) { return x; }

// CIR: cir.func{{.*}} @take_b8(%arg0: !u8i {{.*}}) -> !u8i
// LLVM: define dso_local i8 @take_b8(i8 noundef %{{.*}}){{.*}}
typedef _Bool b8 __attribute__((ext_vector_type(8)));
b8 take_b8(b8 x) { return x; }

// CIR: cir.func{{.*}} @take_b16(%arg0: !u16i {{.*}}) -> !u16i
// LLVM: define dso_local i16 @take_b16(i16 noundef %{{.*}}){{.*}}
typedef _Bool b16 __attribute__((ext_vector_type(16)));
b16 take_b16(b16 x) { return x; }

// CIR: cir.func{{.*}} @take_b20(%arg0: !u32i {{.*}}) -> !u32i
// LLVMCIR: define dso_local i32 @take_b20(i32 noundef %{{.*}}){{.*}}
// OGCG: define dso_local i32 @take_b20(i32 %{{.*}}){{.*}}
typedef _Bool b20 __attribute__((ext_vector_type(20)));
b20 take_b20(b20 x) { return x; }

// CIR: cir.func{{.*}} @take_b32(%arg0: !u32i {{.*}}) -> !u32i
// LLVM: define dso_local i32 @take_b32(i32 noundef %{{.*}}){{.*}}
typedef _Bool b32 __attribute__((ext_vector_type(32)));
b32 take_b32(b32 x) { return x; }

// CIR: cir.func{{.*}} @take_two(%arg0: !u8i {{.*}}, %arg1: !u8i {{.*}}) -> !u8i
// LLVM: define dso_local i8 @take_two(i8 noundef %{{.*}}, i8 noundef %{{.*}}){{.*}}
b8 take_two(b8 a, b8 b) { return a; }

// CIR: cir.func{{.*}} @call_b8() -> !u8i
// CIR: cir.call @take_b8(%{{.*}}) : (!u8i {{.*}}) -> !u8i
// LLVM: define dso_local i8 @call_b8()
// LLVMCIR: call i8 @take_b8(i8 noundef %{{.*}})
// OGCG: call {{.*}} @take_b8(i8 noundef %{{.*}})
b8 call_b8(void) {
  b8 v = (b8){1, 0, 1, 0, 1, 0, 1, 0};
  return take_b8(v);
}
