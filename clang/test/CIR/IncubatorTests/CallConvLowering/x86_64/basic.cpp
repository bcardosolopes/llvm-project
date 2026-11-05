// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// Test call conv lowering for trivial cases. //

// CHECK: @_Z4Voidv()
void Void(void) {
// CHECK:   cir.call @_Z4Voidv() : () -> ()
  Void();
}

// Test call conv lowering for trivial zeroext cases.

// Bools are a bit of an odd case in CIR's x86_64 representation: they are considered i8
// everywhere except in the function return/arguments, where they are considered i1. To
// match LLVM's behavior, we need to zero-extend them when passing them as arguments.

// CHECK: @_Z4Boolb(%arg0: !cir.bool {cir.noundef, cir.zeroext} loc({{.+}})) -> (!cir.bool {cir.noundef, cir.zeroext})
bool Bool(bool a) {
// CHECK:   cir.call @_Z4Boolb({{.+}}) : (!cir.bool) -> !cir.bool
  return Bool(a);
}

// CHECK: cir.func {{.*}} @_Z5UCharh(%arg0: !u8i {cir.noundef, cir.zeroext} loc({{.+}})) -> (!u8i {cir.noundef, cir.zeroext})
unsigned char UChar(unsigned char c) {
  // CHECK: cir.call @_Z5UCharh(%1) : (!u8i) -> !u8i
  return UChar(c);
}
// CHECK: cir.func {{.*}} @_Z6UShortt(%arg0: !u16i {cir.noundef, cir.zeroext} loc({{.+}})) -> (!u16i {cir.noundef, cir.zeroext})
unsigned short UShort(unsigned short s) {
  // CHECK: cir.call @_Z6UShortt(%1) : (!u16i) -> !u16i
  return UShort(s);
}
// CHECK: cir.func {{.*}} @_Z4UIntj(%arg0: !u32i {cir.noundef} loc({{.+}})) -> (!u32i {cir.noundef})
unsigned int UInt(unsigned int i) {
  // CHECK: cir.call @_Z4UIntj(%1) : (!u32i) -> !u32i
  return UInt(i);
}
// CHECK: cir.func {{.*}} @_Z5ULongm(%arg0: !u64i {cir.noundef} loc({{.+}})) -> (!u64i {cir.noundef})
unsigned long ULong(unsigned long l) {
  // CHECK: cir.call @_Z5ULongm(%1) : (!u64i) -> !u64i
  return ULong(l);
}
// CHECK: cir.func {{.*}} @_Z9ULongLongy(%arg0: !u64i {cir.noundef} loc({{.+}})) -> (!u64i {cir.noundef})
unsigned long long ULongLong(unsigned long long l) {
  // CHECK: cir.call @_Z9ULongLongy(%1) : (!u64i) -> !u64i
  return ULongLong(l);
}

/// Test call conv lowering for trivial signext cases. ///

// CHECK: cir.func {{.*}} @_Z4Chara(%arg0: !s8i {cir.noundef, cir.signext} loc({{.+}})) -> (!s8i {cir.noundef, cir.signext})
char Char(signed char c) {
  // CHECK: cir.call @_Z4Chara(%{{.+}}) : (!s8i) -> !s8i
  return Char(c);
}
// CHECK: cir.func {{.*}} @_Z5Shorts(%arg0: !s16i {cir.noundef, cir.signext} loc({{.+}})) -> (!s16i {cir.noundef, cir.signext})
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


/// Test call conv lowering for struct type coercion scenarios. ///

struct S1 {
  int a, b;
};


/// Validate coerced argument and cast it to the expected type.

/// Cast arguments to the expected type.
// CHECK: %[[#V0:]] = cir.alloca !rec_S1, !cir.ptr<!rec_S1>, [""] {alignment = 4 : i64}
// CHECK: %[[#V1:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S1> -> !cir.ptr<!u64i>
// CHECK: cir.store{{.*}} %arg0, %[[#V1]] : !u64i, !cir.ptr<!u64i>
// CHECK: %[[#V2:]] = cir.alloca !rec_S1, !cir.ptr<!rec_S1>, ["__retval"] {alignment = 4 : i64}
// CHECK: %[[#V3:]] = cir.alloca !rec_S1, !cir.ptr<!rec_S1>, ["agg.tmp0"] {alignment = 4 : i64}
// CHECK: %[[#V4:]] = cir.alloca !rec_S1, !cir.ptr<!rec_S1>, ["agg.tmp1"] {alignment = 4 : i64}
S1 s1(S1 arg) {

  /// Cast argument and result of the function call to the expected types.
  // CHECK: %[[#V9:]] = cir.cast bitcast %[[#V3]] : !cir.ptr<!rec_S1> -> !cir.ptr<!u64i>
  // CHECK: %[[#V10:]] = cir.load{{.*}} %[[#V9]] : !cir.ptr<!u64i>, !u64i
  // CHECK: %[[#V11:]] = cir.call @_Z2s12S1(%[[#V10]]) : (!u64i) -> !u64i
  // CHECK: %[[#V12:]] = cir.cast bitcast %[[#V4]] : !cir.ptr<!rec_S1> -> !cir.ptr<!u64i>
  // CHECK: cir.store{{.*}} %[[#V11]], %[[#V12]] : !u64i, !cir.ptr<!u64i>
  s1({1, 2});

  // CHECK: %[[#V13:]] = cir.get_member %[[#V2]][0] {name = "a"} : !cir.ptr<!rec_S1> -> !cir.ptr<!s32i>
  // CHECK: %[[#V14:]] = cir.const #cir.int<1> : !s32i
  // CHECK: cir.store{{.*}} %[[#V14]], %[[#V13]] : !s32i, !cir.ptr<!s32i>
  // CHECK: %[[#V15:]] = cir.get_member %[[#V2]][1] {name = "b"} : !cir.ptr<!rec_S1> -> !cir.ptr<!s32i>
  // CHECK: %[[#V16:]] = cir.const #cir.int<2> : !s32i
  // CHECK: cir.store{{.*}} %[[#V16]], %[[#V15]] : !s32i, !cir.ptr<!s32i>
  // CHECK: %[[#V17:]] = cir.cast bitcast %[[#V2]] : !cir.ptr<!rec_S1> -> !cir.ptr<!u64i>
  // CHECK: %[[#V18:]] = cir.load{{.*}} %[[#V17]] : !cir.ptr<!u64i>, !u64i
  // CHECK: cir.return %[[#V18]] : !u64i
  return {1, 2};
}

/// Test call conv lowering for flattened structs. ///

struct S2 {
  int x, y, z;
};

// COM: Function prologue

// CHECK: cir.func {{.*}} @_Z2s22S2(%[[ARG0:[a-z0-9]+]]: !u64i {{.*}}, %[[ARG1:[a-z0-9]+]]: !s32i {{.*}}) -> !rec_anon_struct
// CHECK: %[[#RET_SLOT:]] = cir.alloca !rec_anon_struct, !cir.ptr<!rec_anon_struct>, ["tmp"]
// CHECK: %[[#A1:]] = cir.alloca !rec_anon_struct, !cir.ptr<!rec_anon_struct>, ["tmp"]
// CHECK: %[[#A2:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>
// CHECK: %[[#A3:]] = cir.get_member %[[#A1]][0]{{.*}} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!u64i>
// CHECK: cir.store{{.*}} %[[ARG0]], %[[#A3]] : !u64i, !cir.ptr<!u64i>
// CHECK: %[[#A4:]] = cir.get_member %[[#A1]][1]{{.*}} : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!s32i>
// CHECK: cir.store{{.*}} %[[ARG1]], %[[#A4]] : !s32i, !cir.ptr<!s32i>
// CHECK: cir.cast bitcast %[[#A1]] : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!void>
// CHECK: cir.cast bitcast %[[#A2]] : !cir.ptr<!rec_S2> -> !cir.ptr<!void>
// CHECK: cir.libc.memcpy
S2 s2(S2 arg) {
  // CHECK: %[[#B0:]] = cir.alloca !rec_anon_struct, !cir.ptr<!rec_anon_struct>, ["tmp"]
  // CHECK: %[[#B1:]] = cir.alloca !rec_anon_struct, !cir.ptr<!rec_anon_struct>, ["tmp"]
  // CHECK: %[[#B2:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["__retval"]
  // CHECK: %[[#B3:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["agg.tmp0"]
  // CHECK: %[[#B4:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["agg.tmp1"]

  // COM: Construction of S2 { 1, 2, 3 }.

  // CHECK: cir.get_member %[[#B3]][0]
  // CHECK: cir.const #cir.int<1>
  // CHECK: cir.get_member %[[#B3]][1]
  // CHECK: cir.const #cir.int<2>
  // CHECK: cir.get_member %[[#B3]][2]
  // CHECK: cir.const #cir.int<3>

  // COM: Flattening of the struct.
  // COM: { i32, i32, i32 } -> { i64, i32 }.

  // CHECK: cir.cast bitcast %[[#B3]] : !cir.ptr<!rec_S2> -> !cir.ptr<!void>
  // CHECK: cir.cast bitcast %[[#B1]] : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!void>
  // CHECK: cir.libc.memcpy

  // COM: Function call.
  // COM: Retrieve the two values in { i64, i32 }.

  // CHECK: cir.get_member %[[#B1]][0]
  // CHECK: cir.load
  // CHECK: cir.get_member %[[#B1]][1]
  // CHECK: cir.load
  // CHECK: cir.call @_Z2s22S2
  // CHECK: cir.store{{.*}} %{{.*}}, %[[#B0]] : !rec_anon_struct, !cir.ptr<!rec_anon_struct>

  // CHECK: cir.cast bitcast %[[#B0]] : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!void>
  // CHECK: cir.cast bitcast %[[#B4]] : !cir.ptr<!rec_S2> -> !cir.ptr<!void>
  // CHECK: cir.libc.memcpy

  // COM: Construct S2 { 1, 2, 3 } again.
  // COM: It has been tested above, so no duplication here.

  // COM: For return, the first two fields of S2 is also coerced.

  // CHECK: cir.cast bitcast %[[#B2]] : !cir.ptr<!rec_S2> -> !cir.ptr<!void>
  // CHECK: cir.cast bitcast %[[#RET_SLOT]] : !cir.ptr<!rec_anon_struct> -> !cir.ptr<!void>
  // CHECK: cir.libc.memcpy
  // CHECK: cir.load %[[#RET_SLOT]] : !cir.ptr<!rec_anon_struct>, !rec_anon_struct
  // cir.return %[[#]] : !rec_anon_struct
  s2({ 1, 2, 3 });
  return { 1, 2, 3 };
}
