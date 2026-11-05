// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// Packed bitfield structs should not be forced to MEMORY classification just
// because the CIR bitfield storage type is misaligned. OGCG skips the alignment
// check for bitfields (X86.cpp:2136: "if (!BitField && IsInMemory)"). CIR must
// replicate this behavior since bitfields are lowered to integer storage types
// that may appear misaligned in packed records.

// 3-byte packed bitfield struct: {u8 at offset 0, u16 at offset 1}.
// The u16 is at offset 8 bits (misaligned for 16-bit type), but since it's
// bitfield storage, the alignment check must be skipped. Returned as i24.
struct __attribute__((packed)) PBF3 {
  unsigned a : 1;
  unsigned b : 7;
  unsigned c : 16;
};

// CIR-LABEL: define dso_local i24 @make_pbf3(
// OGCG-LABEL: define dso_local i24 @make_pbf3(
struct PBF3 make_pbf3(unsigned a, unsigned b, unsigned c) {
  struct PBF3 s;
  s.a = a; s.b = b; s.c = c;
  return s;
}

// CIR-LABEL: define dso_local void @use_pbf3(i24 %
// OGCG-LABEL: define dso_local void @use_pbf3(i24 %
void use_pbf3(struct PBF3 s) {
  (void)s.c;
}

// 5-byte packed bitfield struct. Even though CIR storage u32 is misaligned at
// offset 1 byte, the alignment check is skipped for bitfield storage. Since
// 5 bytes fits in a single eightbyte, classified as INTEGER and coerced to i40.
struct __attribute__((packed)) PBF5 {
  unsigned a : 8;
  unsigned b : 32;
};

// CIR-LABEL: define dso_local i40 @make_pbf5(
// OGCG-LABEL: define dso_local i40 @make_pbf5(
struct PBF5 make_pbf5(unsigned a, unsigned b) {
  struct PBF5 s;
  s.a = a; s.b = b;
  return s;
}

// 4-byte packed bitfield struct with multiple small fields.
// Fits in 4 bytes, classified as INTEGER, coerced to i32.
struct __attribute__((packed)) PBF4 {
  unsigned a : 3;
  unsigned b : 13;
  unsigned c : 16;
};

// CIR-LABEL: define dso_local i32 @make_pbf4(
// OGCG-LABEL: define dso_local i32 @make_pbf4(
struct PBF4 make_pbf4(unsigned a, unsigned b, unsigned c) {
  struct PBF4 s;
  s.a = a; s.b = b; s.c = c;
  return s;
}
