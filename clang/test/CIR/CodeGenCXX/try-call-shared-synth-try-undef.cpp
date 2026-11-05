// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fexceptions \
// RUN:   -fcxx-exceptions -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Regression test: when multiple calls at the same EH cleanup depth share an
// unwind edge and return scalar results, each tmp.try.call.res load must stay
// after its store. Hoisting them makes the loads read uninitialized memory,
// producing undef arguments in the subsequent call. On ARM64 this causes
// SIGSEGV; on x86_64 the undef happens to be a harmless value.

struct Guard {
  Guard();
  ~Guard();
};

struct S {
  ~S();
  const char* getRawBuffer();
};

void handler(const char*, const char*, const char*);

void test(S& a, S& b, S& c) {
  Guard g;
  S sa, sb, sc;
  handler(sa.getRawBuffer(), sb.getRawBuffer(), sc.getRawBuffer());
}

// All four invokes sit at the same EH depth and share one landing pad. Each
// result is stored to its tmp.try.call.res slot before the next call, and the
// loads feeding handler() all come after their stores.
// CHECK-LABEL: define {{.*}} @_Z4testR1SS0_S0_
// CHECK:         %[[R1:.*]] = invoke noundef ptr @_ZN1S12getRawBufferEv
// CHECK:         store ptr %[[R1]], ptr %[[T1:.*]], align 8
// CHECK:         %[[R2:.*]] = invoke noundef ptr @_ZN1S12getRawBufferEv
// CHECK:         store ptr %[[R2]], ptr %[[T2:.*]], align 8
// CHECK:         %[[R3:.*]] = invoke noundef ptr @_ZN1S12getRawBufferEv
// CHECK:         store ptr %[[R3]], ptr %[[T3:.*]], align 8
// CHECK:         %[[L1:.*]] = load ptr, ptr %[[T1]], align 8
// CHECK:         %[[L2:.*]] = load ptr, ptr %[[T2]], align 8
// CHECK:         %[[L3:.*]] = load ptr, ptr %[[T3]], align 8
// CHECK:         invoke void @_Z7handlerPKcS0_S0_(ptr noundef %[[L1]], ptr noundef %[[L2]], ptr noundef %[[L3]])
// CHECK-NOT:     invoke void @_Z7handlerPKcS0_S0_({{.*}}undef
