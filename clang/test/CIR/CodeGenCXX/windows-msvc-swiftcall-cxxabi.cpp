// RUN: %clang_cc1 -triple x86_64-unknown-windows -target-cpu core2 -fclangir \
// RUN:   -clangir-disable-passes -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-windows -target-cpu core2 -fclangir \
// RUN:   -fno-clangir-call-conv-lowering -emit-cir %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-windows -target-cpu core2 -fclangir \
// RUN:   -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

#define SWIFTCALL __attribute__((swiftcall))

struct NonTrivial {
  NonTrivial();
  NonTrivial(const NonTrivial &);
  int o;
};

SWIFTCALL int receiveNonTrivial(NonTrivial o) { return o.o; }

int passNonTrivial() {
  return receiveNonTrivial({});
}

// CHECK-LABEL: cir.func no_inline optnone dso_local @"?passNonTrivial@@YAHXZ"()
// CHECK: %[[TMP:.+]] = cir.alloca !rec_NonTrivial, !cir.ptr<!rec_NonTrivial>, ["agg.tmp"] {alignment = 4 : i64}
// CHECK: %[[CTOR:.+]] = cir.call @"??0NonTrivial@@QEAA@XZ"(%[[TMP]]) : (!cir.ptr<!rec_NonTrivial>) -> !cir.ptr<!rec_NonTrivial>
// CHECK: %[[ARG:.+]] = cir.load align(4) %[[TMP]] : !cir.ptr<!rec_NonTrivial>, !rec_NonTrivial
// CHECK: %[[CALL:.+]] = cir.call @"?receiveNonTrivial@@YSHUNonTrivial@@@Z"(%[[ARG]]) : (!rec_NonTrivial) -> !s32i cc(swift)

// CHECK: cir.func private dso_local @"??0NonTrivial@@QEAA@XZ"(!cir.ptr<!rec_NonTrivial> {cir.align = 4 : i64, cir.callsite_nonnull, cir.dereferenceable = 4 : i64, cir.nonnull, cir.noundef, cir.returned}) -> !cir.ptr<!rec_NonTrivial>

// LLVM-LABEL: define dso_local swiftcc noundef i32 @"?receiveNonTrivial@@YSHUNonTrivial@@@Z"(ptr dead_on_return noundef %{{.*}})
// LLVM-LABEL: define dso_local noundef i32 @"?passNonTrivial@@YAHXZ"()
// LLVM-NOT: stacksave
// LLVM: call swiftcc noundef i32 @"?receiveNonTrivial@@YSHUNonTrivial@@@Z"(ptr dead_on_return noundef %{{.*}})
