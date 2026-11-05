// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s
// Union ABI coercion — ensure unions use largest member for ABI type

// Union with pointer as largest member — should return/pass as ptr
union PtrUnion { char c; void *p; int a, b, c2, d; };
// CHECK: define dso_local ptr @ret_ptr_union()
union PtrUnion ret_ptr_union(void) {
  union PtrUnion u;
  u.p = (void*)123;
  return u;
}
// CHECK: define dso_local void @take_ptr_union(ptr %{{.*}})
void take_ptr_union(union PtrUnion u) {}

// Union with double as largest member — classified SSE then coerced
union DblUnion { float f; double d; };
// CHECK: define dso_local double @ret_dbl_union()
union DblUnion ret_dbl_union(void) {
  union DblUnion u;
  u.d = 1.0;
  return u;
}
// CHECK: define dso_local void @take_dbl_union(double %{{.*}})
void take_dbl_union(union DblUnion u) {}

// Union with long as largest member — should return/pass as i64
union IntUnion { char c; short s; int i; long l; };
// CHECK: define dso_local i64 @ret_int_union()
union IntUnion ret_int_union(void) {
  union IntUnion u;
  u.l = 99;
  return u;
}
// CHECK: define dso_local void @take_int_union(i64 %{{.*}})
void take_int_union(union IntUnion u) {}

// Union with float — 4-byte, should return as i32 (Integer class)
union SmallUnion { char c; float f; };
// CHECK: define dso_local i32 @ret_small_union()
union SmallUnion ret_small_union(void) {
  union SmallUnion u;
  u.f = 1.0f;
  return u;
}
// CHECK: define dso_local void @take_small_union(i32 %{{.*}})
void take_small_union(union SmallUnion u) {}

// Union larger than 8 bytes — two-register pair
union BigUnion { char c; long a; long b[2]; };
// CHECK: define dso_local { i64, i64 } @ret_big_union()
union BigUnion ret_big_union(void) {
  union BigUnion u;
  u.b[0] = 1;
  u.b[1] = 2;
  return u;
}
// CHECK: define dso_local void @take_big_union(i64 %{{.*}}, i64 %{{.*}})
void take_big_union(union BigUnion u) {}
