// RUN: %clang_cc1 -triple x86_64-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM %s < %t.ll
// RUN: %clang_cc1 -triple x86_64-unknown-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG %s < %t.ogcg.ll

// Verify that __builtin_counted_by_ref emits a GEP to the counted_by field
// instead of a null pointer.

struct a {
  char x;
  short count;
  int array[] __attribute__((counted_by(count)));
};

// Simple case: counted_by field is a direct member.
struct a *test1(int size) {
  struct a *p = __builtin_malloc(sizeof(struct a) + sizeof(int) * size);
  *__builtin_counted_by_ref(p->array) = size;
  return p;
}
// LLVM-LABEL: define {{.*}} @test1(
// LLVM: [[GEP:%.*]] = getelementptr inbounds {{(nuw )?}}%struct.a, ptr %{{.*}}, i32 0, i32 1
// LLVM: store i16 %{{.*}}, ptr [[GEP]], align 2
// OGCG-LABEL: define {{.*}} @test1(
// OGCG: [[GEP:%.*]] = getelementptr inbounds {{(nuw )?}}%struct.a, ptr %{{.*}}, i32 0, i32 1
// OGCG: store i16 %{{.*}}, ptr [[GEP]], align 2

// Nested anonymous structs: counted_by field in deeply nested anonymous struct.
struct b {
  int _filler;
  struct {
    int __filler;
    struct {
      int ___filler;
      struct {
        char count;
      };
    };
  };
  struct {
    int filler_;
    struct {
      int filler__;
      struct {
        long array[] __attribute__((counted_by(count)));
      };
    };
  };
};

struct b *test2(int size) {
  struct b *p = __builtin_malloc(sizeof(struct a) + sizeof(int) * size);
  *__builtin_counted_by_ref(p->array) = size;
  return p;
}
// Both should store i8 to a non-null GEP address (not store to null).
// LLVM-LABEL: define {{.*}} @test2(
// LLVM-NOT: store i8 %{{.*}}, ptr null
// LLVM: store i8 %{{.*}}, ptr %{{.*}}, align 1
// OGCG-LABEL: define {{.*}} @test2(
// OGCG-NOT: store i8 %{{.*}}, ptr null
// OGCG: store i8 %{{.*}}, ptr %{{.*}}, align 1

// No counted_by attribute: should use _Generic fallback to __ignored.
struct c {
  char x;
  short count;
  int array[];
};

struct c *test3(int size) {
  struct c *p = __builtin_malloc(sizeof(struct c) + sizeof(int) * size);
  unsigned long int __ignored;
  *_Generic(
    __builtin_counted_by_ref(p->array),
      void *: &__ignored,
      default: __builtin_counted_by_ref(p->array)) = size;
  return p;
}
// The _Generic resolves to &__ignored (void* case), so no GEP to a struct field.
// LLVM-LABEL: define {{.*}} @test3(
// LLVM-NOT: getelementptr{{.*}}%struct.c
// OGCG-LABEL: define {{.*}} @test3(
// OGCG-NOT: getelementptr{{.*}}%struct.c

// Pointer member with counted_by attribute.
struct d {
  int x;
  short count;
  int *ptr __attribute__((counted_by(count)));
};

struct d *test4(int size, int *data) {
  struct d *p = __builtin_malloc(sizeof(struct d));
  p->ptr = data;
  *__builtin_counted_by_ref(p->ptr) = size;
  return p;
}
// LLVM-LABEL: define {{.*}} @test4(
// LLVM: [[GEP:%.*]] = getelementptr inbounds {{(nuw )?}}%struct.d, ptr %{{.*}}, i32 0, i32 1
// LLVM: store i16 %{{.*}}, ptr [[GEP]], align 2
// OGCG-LABEL: define {{.*}} @test4(
// OGCG: [[GEP:%.*]] = getelementptr inbounds {{(nuw )?}}%struct.d, ptr %{{.*}}, i32 0, i32 1
// OGCG: store i16 %{{.*}}, ptr [[GEP]], align 2
