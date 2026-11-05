// RUN: %clang_cc1 -std=c++20 -triple aarch64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// CHECK: @_Z4Voidv()
void Void(void) {
// CHECK:   cir.call @_Z4Voidv() : () -> ()
  Void();
}

// Test call conv lowering for trivial usinged integer cases.

// CHECK: @_Z4Boolb(%arg0: !cir.bool {cir.noundef} loc({{.+}})) -> (!cir.bool {cir.noundef})
bool Bool(bool a) {
// CHECK:   cir.call @_Z4Boolb({{.+}}) : (!cir.bool) -> !cir.bool
  return Bool(a);
}

// CHECK: cir.func {{.*}} @_Z5UCharh(%arg0: !u8i {cir.noundef} loc({{.+}})) -> (!u8i {cir.noundef})
unsigned char UChar(unsigned char c) {
  // CHECK: cir.call @_Z5UCharh(%{{.+}}) : (!u8i) -> !u8i
  return UChar(c);
}
// CHECK: cir.func {{.*}} @_Z6UShortt(%arg0: !u16i {cir.noundef} loc({{.+}})) -> (!u16i {cir.noundef})
unsigned short UShort(unsigned short s) {
  // CHECK: cir.call @_Z6UShortt(%{{.+}}) : (!u16i) -> !u16i
  return UShort(s);
}
// CHECK: cir.func {{.*}} @_Z4UIntj(%arg0: !u32i {cir.noundef} loc({{.+}})) -> (!u32i {cir.noundef})
unsigned int UInt(unsigned int i) {
  // CHECK: cir.call @_Z4UIntj(%{{.+}}) : (!u32i) -> !u32i
  return UInt(i);
}
// CHECK: cir.func {{.*}} @_Z5ULongm(%arg0: !u64i {cir.noundef} loc({{.+}})) -> (!u64i {cir.noundef})
unsigned long ULong(unsigned long l) {
  // CHECK: cir.call @_Z5ULongm(%{{.+}}) : (!u64i) -> !u64i
  return ULong(l);
}
// CHECK: cir.func {{.*}} @_Z9ULongLongy(%arg0: !u64i {cir.noundef} loc({{.+}})) -> (!u64i {cir.noundef})
unsigned long long ULongLong(unsigned long long l) {
  // CHECK: cir.call @_Z9ULongLongy(%{{.+}}) : (!u64i) -> !u64i
  return ULongLong(l);
}


/// Test call conv lowering for trivial signed cases. ///

// CHECK: cir.func {{.*}} @_Z4Chara(%arg0: !s8i {cir.noundef} loc({{.+}})) -> (!s8i {cir.noundef})
char Char(signed char c) {
  // CHECK: cir.call @_Z4Chara(%{{.+}}) : (!s8i) -> !s8i
  return Char(c);
}
// CHECK: cir.func {{.*}} @_Z5Shorts(%arg0: !s16i {cir.noundef} loc({{.+}})) -> (!s16i {cir.noundef})
short Short(short s) {
  // CHECK: cir.call @_Z5Shorts(%{{.+}}) : (!s16i) -> !s16i
  return Short(s);
}
// CHECK: cir.func {{.*}} @_Z3Inti(%arg0: !s32i {cir.noundef} loc({{.+}})) -> (!s32i {cir.noundef})
int Int(int i) {
  // CHECK: cir.call @_Z3Inti(%{{.+}}) : (!s32i) -> !s32i
  return Int(i);
}
// CHECK: cir.func {{.*}} @_Z4Longl(%arg0: !s64i {cir.noundef} loc({{.+}})) -> (!s64i {cir.noundef})
long Long(long l) {
  // CHECK: cir.call @_Z4Longl(%{{.+}}) : (!s64i) -> !s64i
  return Long(l);
}
// CHECK: cir.func {{.*}} @_Z8LongLongx(%arg0: !s64i {cir.noundef} loc({{.+}})) -> (!s64i {cir.noundef})
long long LongLong(long long l) {
  // CHECK: cir.call @_Z8LongLongx(%{{.+}}) : (!s64i) -> !s64i
  return LongLong(l);
}


/// Test call conv lowering for floating point. ///

// CHECK: cir.func {{.*}} @_Z5Floatf(%arg0: !cir.float {cir.noundef} loc({{.+}})) -> (!cir.float {cir.noundef})
float Float(float f) {
  // cir.call @_Z5Floatf(%{{.+}}) : (!cir.float) -> !cir.float
  return Float(f);
}
// CHECK: cir.func {{.*}} @_Z6Doubled(%arg0: !cir.double {cir.noundef} loc({{.+}})) -> (!cir.double {cir.noundef})
double Double(double d) {
  // cir.call @_Z6Doubled(%{{.+}}) : (!cir.double) -> !cir.double
  return Double(d);
}
