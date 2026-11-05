// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll %s --check-prefix=OGCG
//
// _BitInt(160) is 24 bytes (3 eightbytes), too large for registers on x86-64.
// va_arg must use the overflow area, not the register save area.
// Function calls must use sret/byval indirect passing.

_BitInt(160) test_bitint160(int count, ...) {
  __builtin_va_list args;
  __builtin_va_start(args, count);
  _BitInt(160) A = __builtin_va_arg(args, _BitInt(160));
  __builtin_va_end(args);
  return A;
}

// CIR-LABEL: @test_bitint160
// CIR-NOT:     gp_offset
// CIR:         %[[OAP:.*]] = cir.get_member %{{.*}}[2] {name = "overflow_arg_area"}
// CIR:         %[[OA:.*]] = cir.load %[[OAP]]
// CIR:         cir.const #cir.int<24>
// CIR:         cir.store %{{.*}}, %[[OAP]]
// CIR:         %[[SLOTP:.*]] = cir.cast bitcast %[[OA]] : !cir.ptr<!void> -> !cir.ptr<!cir.int<s, 192>>
// CIR:         %[[RAW:.*]] = cir.load align(8) %[[SLOTP]] : !cir.ptr<!cir.int<s, 192>>, !cir.int<s, 192>
// CIR:         %[[SEM:.*]] = cir.cast integral %[[RAW]] : !cir.int<s, 192> -> !cir.int<s, 160>

// LLVM-LABEL: @test_bitint160
// LLVM-NOT:     gp_offset
// LLVM:         getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 2
// LLVM:         getelementptr inbounds i8, ptr %{{.*}}, i64 24
// LLVM:         %[[RAW:.*]] = load i192, ptr %{{.*}}, align 8
// LLVM:         %[[TRUNC:.*]] = trunc i192 %[[RAW]] to i160
// LLVM:         %[[SEXT:.*]] = sext i160 %[[TRUNC]] to i192

// OGCG-LABEL: @test_bitint160
// OGCG-NOT:     gp_offset
// OGCG:         getelementptr inbounds nuw %struct.__va_list_tag, ptr %{{.*}}, i32 0, i32 2
// OGCG:         getelementptr i8, ptr %{{.*}}, i32 24
// OGCG:         %[[RAW:.*]] = load i192, ptr %{{.*}}, align 8
// OGCG:         %[[TRUNC:.*]] = trunc i192 %[[RAW]] to i160
// OGCG:         %[[SEXT:.*]] = sext i160 %[[TRUNC]] to i192

// Verify _BitInt(160) uses sret for return and byval for argument passing.
_BitInt(160) return_bitint160(void);
void take_bitint160(_BitInt(160) x);
void test_bitint160_abi(void) {
  _BitInt(160) x = return_bitint160();
  take_bitint160(x);
}

// LLVM-LABEL: @test_bitint160_abi
// LLVM:         call void @return_bitint160(ptr {{.*}} sret([24 x i8]) align 8
// LLVM:         call void @take_bitint160(ptr {{.*}} byval([24 x i8]) align 8

// OGCG-LABEL: @test_bitint160_abi
// OGCG:         call void @return_bitint160(ptr {{.*}} sret([24 x i8]) align 8
// OGCG:         call void @take_bitint160(ptr {{.*}} byval([24 x i8]) align 8
