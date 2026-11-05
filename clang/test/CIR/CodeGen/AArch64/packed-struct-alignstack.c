// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM

// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=OGCG

// Test that packed structs get correct alignstack attributes.
// Packed structs with alignment < 16 should use alignstack(8), not alignstack(16).

typedef unsigned short uint16x8_t __attribute__((vector_size(16)));

struct non_packed {
  uint16x8_t M0;
};

struct __attribute__((packed)) packed_s {
  uint16x8_t M0;
};

struct packed_member {
  uint16x8_t M0 __attribute__((packed));
};

#pragma pack(8)
struct pragma_packed_8 {
  uint16x8_t M0;
};
#pragma pack()

#pragma pack(4)
struct pragma_packed_4 {
  uint16x8_t M0;
};
#pragma pack()

// Non-packed: natural alignment 16 -> alignstack(16)
// LLVM: define {{.*}} @non_packed_func({{.*}}[1 x <8 x i16>] alignstack(16)
// OGCG: define {{.*}} @non_packed_func({{.*}}[1 x <8 x i16>] alignstack(16)
void non_packed_func(double d0, double d1, double d2, double d3,
                     double d4, double d5, double d6, double d7,
                     double d8, struct non_packed s) {
}

// Packed struct: natural alignment 1 -> alignstack(8) (capped minimum)
// LLVM: define {{.*}} @packed_func({{.*}}[1 x <8 x i16>] alignstack(8)
// OGCG: define {{.*}} @packed_func({{.*}}[1 x <8 x i16>] alignstack(8)
void packed_func(double d0, double d1, double d2, double d3,
                 double d4, double d5, double d6, double d7,
                 double d8, struct packed_s s) {
}

// Packed member: natural alignment 1 -> alignstack(8)
// LLVM: define {{.*}} @packed_member_func({{.*}}[1 x <8 x i16>] alignstack(8)
// OGCG: define {{.*}} @packed_member_func({{.*}}[1 x <8 x i16>] alignstack(8)
void packed_member_func(double d0, double d1, double d2, double d3,
                        double d4, double d5, double d6, double d7,
                        double d8, struct packed_member s) {
}

// Pragma pack(8): natural alignment 8 -> alignstack(8)
// LLVM: define {{.*}} @pragma_packed_8_func({{.*}}[1 x <8 x i16>] alignstack(8)
// OGCG: define {{.*}} @pragma_packed_8_func({{.*}}[1 x <8 x i16>] alignstack(8)
void pragma_packed_8_func(double d0, double d1, double d2, double d3,
                          double d4, double d5, double d6, double d7,
                          double d8, struct pragma_packed_8 s) {
}

// Pragma pack(4): natural alignment 4 -> alignstack(8) (capped minimum)
// LLVM: define {{.*}} @pragma_packed_4_func({{.*}}[1 x <8 x i16>] alignstack(8)
// OGCG: define {{.*}} @pragma_packed_4_func({{.*}}[1 x <8 x i16>] alignstack(8)
void pragma_packed_4_func(double d0, double d1, double d2, double d3,
                          double d4, double d5, double d6, double d7,
                          double d8, struct pragma_packed_4 s) {
}
