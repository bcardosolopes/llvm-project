// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG
//
// Swifterror parameters require special handling in call-conv lowering:
// - Function prolog: copy from swifterror param to temp alloca
// - Function epilog: write back from temp to swifterror param before return
// - Call site: create a swifterror-annotated alloca, copy-in, pass, copy-out

#define SWIFTCALL __attribute__((swiftcall))
#define SWIFTCONTEXT __attribute__((swift_context))
#define SWIFTERROR __attribute__((swift_error_result))

typedef struct { int val; } ErrorType;

// CIR: define swiftcc void @swifterror_fn({{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
// OGCG: define swiftcc void @swifterror_fn({{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
SWIFTCALL void swifterror_fn(void *ctx SWIFTCONTEXT, ErrorType **error SWIFTERROR) {
    *error = (ErrorType *)0;
}

// CIR-LABEL: define swiftcc void @caller(
// CIR-SAME: {{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
// CIR: alloca swifterror ptr
// CIR: call swiftcc void @swifterror_fn({{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
// OGCG-LABEL: define swiftcc void @caller(
// OGCG-SAME: {{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
// OGCG: alloca swifterror ptr
// OGCG: call swiftcc void @swifterror_fn({{.*}}swiftself %{{.*}}, {{.*}}swifterror %{{.*}})
SWIFTCALL void caller(void *ctx SWIFTCONTEXT, ErrorType **error SWIFTERROR) {
    swifterror_fn(ctx, error);
}
