// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

// Compile-time regression coverage: x86_64 aggregate classification with many
// empty fields should still classify only the real data members.

struct Empty {};

#define MANY_EMPTY_FIELDS_A                                                  \
  Empty a00, a01, a02, a03, a04, a05, a06, a07, a08, a09, a10, a11, a12,   \
      a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25,      \
      a26, a27, a28, a29, a30, a31

#define MANY_EMPTY_FIELDS_B                                                  \
  Empty b00, b01, b02, b03, b04, b05, b06, b07, b08, b09, b10, b11, b12,   \
      b13, b14, b15, b16, b17, b18, b19, b20, b21, b22, b23, b24, b25,      \
      b26, b27, b28, b29, b30, b31

struct ManyEmptyFields {
  MANY_EMPTY_FIELDS_A;
  void *p;
  MANY_EMPTY_FIELDS_B;
};

const void *use_many_empty_fields(ManyEmptyFields m) { return m.p; }

struct ManyEmptyWithUnnamedBitfield {
  MANY_EMPTY_FIELDS_A;
  int : 0;
  void *p;
  MANY_EMPTY_FIELDS_B;
};

const void *use_many_empty_with_unnamed_bitfield(
    ManyEmptyWithUnnamedBitfield m) {
  return m.p;
}

struct BaseWithPtr {
  void *bp;
};

struct ManyEmptyWithBase : BaseWithPtr {
  MANY_EMPTY_FIELDS_A;
  MANY_EMPTY_FIELDS_B;
};

const void *use_many_empty_with_base(ManyEmptyWithBase m) { return m.bp; }

struct BaseA {
  void *a;
};
struct BaseB {
  void *b;
};
struct ManyEmptyTwoBases : BaseA, BaseB {
  MANY_EMPTY_FIELDS_A;
  MANY_EMPTY_FIELDS_B;
};

const void *use_many_empty_two_bases(ManyEmptyTwoBases m) { return m.b; }

struct MixedHeavy {
  BaseA a;
  MANY_EMPTY_FIELDS_A;
  BaseB b;
  MANY_EMPTY_FIELDS_B;
};

const void *use_mixed_heavy(MixedHeavy m) { return m.b.b; }

struct BigPad {
  long long words[20];
};
struct BiggerPad {
  long long words[64];
};

struct BigMixedHeavy {
  BaseA a;
  MANY_EMPTY_FIELDS_A;
  BaseB b;
  MANY_EMPTY_FIELDS_B;
  BigPad tail;
};

const void *use_big_mixed_heavy(BigMixedHeavy m) { return m.b.b; }

struct BiggerMixedHeavy {
  BaseA a;
  MANY_EMPTY_FIELDS_A;
  BaseB b;
  MANY_EMPTY_FIELDS_B;
  BiggerPad tail;
};

const void *use_bigger_mixed_heavy(BiggerMixedHeavy m) { return m.b.b; }

struct DenseNoEmpty {
  BaseA a;
  BaseB b;
  void *p;
  long long q;
};

const void *use_dense_no_empty(DenseNoEmpty m) { return m.p; }

struct DenseNoBaseNoEmpty {
  void *p0;
  long long w0;
  void *p1;
  long long w1;
};

const void *use_dense_no_base_no_empty(DenseNoBaseNoEmpty m) { return m.p1; }

struct DenseBasesOnly : BaseA, BaseB {};

const void *use_dense_bases_only(DenseBasesOnly m) { return m.b; }

// CHECK: define {{.*}} ptr @_Z21use_many_empty_fields15ManyEmptyFields(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z21use_many_empty_fields15ManyEmptyFields(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z36use_many_empty_with_unnamed_bitfield28ManyEmptyWithUnnamedBitfield(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z36use_many_empty_with_unnamed_bitfield28ManyEmptyWithUnnamedBitfield(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z24use_many_empty_with_base17ManyEmptyWithBase(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z24use_many_empty_with_base17ManyEmptyWithBase(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z24use_many_empty_two_bases17ManyEmptyTwoBases(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z24use_many_empty_two_bases17ManyEmptyTwoBases(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z15use_mixed_heavy10MixedHeavy(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z15use_mixed_heavy10MixedHeavy(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z19use_big_mixed_heavy13BigMixedHeavy(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z19use_big_mixed_heavy13BigMixedHeavy(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z22use_bigger_mixed_heavy16BiggerMixedHeavy(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z22use_bigger_mixed_heavy16BiggerMixedHeavy(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z18use_dense_no_empty12DenseNoEmpty(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z18use_dense_no_empty12DenseNoEmpty(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z26use_dense_no_base_no_empty18DenseNoBaseNoEmpty(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z26use_dense_no_base_no_empty18DenseNoBaseNoEmpty(ptr {{.*}} %{{.*}})

// CHECK: define {{.*}} ptr @_Z20use_dense_bases_only14DenseBasesOnly(ptr {{.*}} %{{.*}})
// OGCG: define {{.*}} ptr @_Z20use_dense_bases_only14DenseBasesOnly(ptr {{.*}} %{{.*}})
