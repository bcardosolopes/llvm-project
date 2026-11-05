// RUN: %clang_cc1 -triple aarch64_be-unknown-linux-gnu -fclangir -emit-llvm -disable-llvm-passes %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple aarch64_be-unknown-linux-gnu -emit-llvm -disable-llvm-passes %s -o %t.ogcg.ll
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

// Big-endian coercion must use zext (not sext) when widening a signed
// integer to a larger ABI type, matching OGCG's unconditional CreateZExt
// at CGCall.cpp:1299.

typedef struct { signed char x; } SmallSChar;
typedef struct { short x; } SmallShort;

void take_schar(SmallSChar s);
void take_short(SmallShort s);

// LLVM-LABEL: define {{.*}}void @call_schar
// LLVM: zext i8 %{{.*}} to i64
// LLVM-NEXT: shl i64
// LLVM: call void @take_schar(i64
//
// OGCG-LABEL: define {{.*}}void @call_schar
// OGCG: zext i8 %{{.*}} to i64
// OGCG-NEXT: shl i64
// OGCG: call void @take_schar(i64
void call_schar(signed char v) {
  SmallSChar s;
  s.x = v;
  take_schar(s);
}

// LLVM-LABEL: define {{.*}}void @call_short
// LLVM: zext i16 %{{.*}} to i64
// LLVM-NEXT: shl i64
// LLVM: call void @take_short(i64
//
// OGCG-LABEL: define {{.*}}void @call_short
// OGCG: zext i16 %{{.*}} to i64
// OGCG-NEXT: shl i64
// OGCG: call void @take_short(i64
void call_short(short v) {
  SmallShort s;
  s.x = v;
  take_short(s);
}
