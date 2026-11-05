// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fno-clangir-call-conv-lowering -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

void VLATest(_BitInt(3) A, _BitInt(42) B, _BitInt(17) C) {
  int AR1[A];
  int AR2[B];
  int AR3[C];
}

//      CHECK: cir.func {{.*}} @VLATest
//      CHECK:   %[[#A_STORAGE:]] = cir.load{{.*}} %{{.+}} : !cir.ptr<!s8i>, !s8i
// CHECK-NEXT:   %[[#A:]] = cir.cast integral %[[#A_STORAGE]] : !s8i -> !cir.int<s, 3>
// CHECK-NEXT:   %[[#A_UNSIGNED:]] = cir.cast integral %[[#A]] : !cir.int<s, 3> -> !cir.int<u, 3>
// CHECK-NEXT:   %[[#A_PROMOTED:]] = cir.cast integral %[[#A_UNSIGNED]] : !cir.int<u, 3> -> !u64i
// CHECK-NEXT:   %[[#SP:]] = cir.stacksave : !cir.ptr<!u8i>
// CHECK-NEXT:   cir.store{{.*}} %[[#SP]], %{{.+}} : !cir.ptr<!u8i>, !cir.ptr<!cir.ptr<!u8i>>
// CHECK-NEXT:   %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, %[[#A_PROMOTED]] : !u64i
// CHECK-NEXT:   %[[#B_STORAGE:]] = cir.load{{.*}} %1 : !cir.ptr<!s64i>, !s64i
// CHECK-NEXT:   %[[#B:]] = cir.cast integral %[[#B_STORAGE]] : !s64i -> !cir.int<s, 42>
// CHECK-NEXT:   %[[#B_UNSIGNED:]] = cir.cast integral %[[#B]] : !cir.int<s, 42> -> !cir.int<u, 42>
// CHECK-NEXT:   %[[#B_PROMOTED:]] = cir.cast integral %[[#B_UNSIGNED]] : !cir.int<u, 42> -> !u64i
// CHECK-NEXT:   %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, %[[#B_PROMOTED]] : !u64i
// CHECK-NEXT:   %[[#C_STORAGE:]] = cir.load{{.*}} %2 : !cir.ptr<!s32i>, !s32i
// CHECK-NEXT:   %[[#C:]] = cir.cast integral %[[#C_STORAGE]] : !s32i -> !cir.int<s, 17>
// CHECK-NEXT:   %[[#C_UNSIGNED:]] = cir.cast integral %[[#C]] : !cir.int<s, 17> -> !cir.int<u, 17>
// CHECK-NEXT:   %[[#C_PROMOTED:]] = cir.cast integral %[[#C_UNSIGNED]] : !cir.int<u, 17> -> !u64i
// CHECK-NEXT:   %{{.+}} = cir.alloca !s32i, !cir.ptr<!s32i>, %[[#C_PROMOTED]] : !u64i
//      CHECK: }
