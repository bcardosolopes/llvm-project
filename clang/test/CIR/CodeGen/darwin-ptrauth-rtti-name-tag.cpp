// RUN: %clang_cc1 %s -x c++ -std=c++11 -I%S/../../CodeGenCXX \
// RUN:   -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-cir -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 %s -x c++ -std=c++11 -I%S/../../CodeGenCXX \
// RUN:   -triple arm64-apple-ios -fptrauth-calls -fptrauth-intrinsics \
// RUN:   -fclangir -emit-llvm -disable-llvm-passes -o - | FileCheck %s --check-prefix=LLVM

#include "typeinfo"

struct A { int a; };

auto ATI = typeid(A);

// CIR: cir.global hidden  constant linkonce_odr dso_local  @_ZTS1A = #cir.const_array<"1A\00"
// CIR: cir.global hidden  constant linkonce_odr dso_local  @_ZTI1A = #cir.typeinfo<{
// CIR-SAME: #cir.global_view<@_ZTVN10__cxxabiv117__class_type_infoE, [2 : i32], inbounds = true> : !cir.ptr<!u8i>
// CIR-SAME: #cir.global_view<@_ZTS1A, addr_adjustment = -9223372036854775808 : i64> : !cir.ptr<!u8i>

// LLVM: @_ZTS1A = linkonce_odr hidden constant [3 x i8] c"1A\00", align 1
// LLVM: @_ZTI1A = linkonce_odr hidden constant { ptr, ptr } { ptr ptrauth (ptr getelementptr inbounds (i8, ptr @_ZTVN10__cxxabiv117__class_type_infoE, i64 16), i32 2), ptr inttoptr (i64 add (i64 ptrtoint (ptr @_ZTS1A to i64), i64 -9223372036854775808) to ptr) }, align 8
