// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// A swiftcall function with a scalar (Direct-ABI) return and a
// swift_error_result parameter must still copy its local error slot back to
// the swifterror parameter before returning. The Direct/Extend return path in
// call-conv lowering used to early-return before emitting that write-back, so
// the callee's error write was silently dropped and never reached the caller.

#define SWIFTCALL __attribute__((swiftcall))
#define SWIFTCONTEXT __attribute__((swift_context))
#define SWIFTERROR __attribute__((swift_error_result))

typedef struct { int val; } ErrorType;
static ErrorType g_err = {42};

SWIFTCALL int callee(int x, void *ctx SWIFTCONTEXT, ErrorType **error SWIFTERROR) {
  if (x < 0)
    *error = &g_err;
  return x * 2;
}

// The prolog copies the incoming error pointer from the swifterror param into a
// local temp (a plain load of the param). The epilog must then store the local
// error back into the swifterror param right before the return. That write-back
// store is the only store into the param and is what the fix restores; without
// it the callee's error write never reaches the caller.

// CIR: define swiftcc i32 @callee(i32 noundef %{{.*}}, ptr noundef swiftself %{{.*}}, ptr noundef swifterror %[[ERR:.*]])
// CIR: alloca ptr
// CIR: load ptr, ptr %[[ERR]]
// CIR: store ptr %{{.*}}, ptr %[[ERR]]
// CIR-NEXT: ret i32

// OGCG: define swiftcc i32 @callee(i32 noundef %{{.*}}, ptr noundef swiftself %{{.*}}, ptr noundef swifterror %[[ERR:.*]])
// OGCG: alloca ptr
// OGCG: load ptr, ptr %[[ERR]]
// OGCG: store ptr %{{.*}}, ptr %[[ERR]]
// OGCG-NEXT: ret i32
