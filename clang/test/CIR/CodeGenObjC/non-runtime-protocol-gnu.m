// RUN: %clang_cc1 -emit-llvm -triple x86_64-linux-gnu -fobjc-runtime=gnustep -fclangir -o - %s | FileCheck %s --check-prefix=LEGACY
// RUN: %clang_cc1 -emit-llvm -triple x86_64-linux-gnu -fobjc-runtime=gnustep-2 -fclangir -o - %s | FileCheck %s --check-prefix=GNUSTEP2

__attribute__((objc_root_class))
@interface Root
@end

@implementation Root
@end

@protocol R1
@end

@protocol R2
@end

@protocol R3 <R1>
@end

__attribute__((objc_non_runtime_protocol)) @protocol N3
@end

__attribute__((objc_non_runtime_protocol)) @protocol N1 <R3, R2, N3>
@end

__attribute__((objc_non_runtime_protocol)) @protocol N2 <N1, R2>
@end

@interface Implementer : Root <N2, R2>
@end

@implementation Implementer
@end

// LEGACY-DAG: @[[R2NAME:\.objc_protocol_name(\.[0-9]+)?]] = private unnamed_addr constant [3 x i8] c"R2\00", align 1
// LEGACY-DAG: @[[R2PROTO:\.objc_protocol(\.[0-9]+)?]] = internal global {{.*}} ptr @[[R2NAME]]{{.*}}, align 8
// LEGACY-DAG: @[[R3NAME:\.objc_protocol_name(\.[0-9]+)?]] = private unnamed_addr constant [3 x i8] c"R3\00", align 1
// LEGACY-DAG: @[[R3PROTO:\.objc_protocol(\.[0-9]+)?]] = internal global {{.*}} ptr @[[R3NAME]]{{.*}}, align 8
// LEGACY-DAG: @[[LEGACY_CLASS_PROTOS:\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [2 x ptr] } { ptr null, i64 2, [2 x ptr] [ptr @[[R2PROTO]], ptr @[[R3PROTO]]] }, align 8
// LEGACY: @_OBJC_CLASS_Implementer = global {{.*}} ptr @[[LEGACY_CLASS_PROTOS]]{{.*}}, align 8

// GNUSTEP2-DAG: @[[R2PROTO:\._OBJC_PROTOCOL_R2]] = global {{.*}}, align 8
// GNUSTEP2-DAG: @[[R3PROTO:\._OBJC_PROTOCOL_R3]] = global {{.*}}, align 8
// GNUSTEP2-DAG: @[[GNUSTEP2_CLASS_PROTOS:\.objc_protocol_list\.[0-9]+]] = internal global { ptr, i64, [2 x ptr] } { ptr null, i64 2, [2 x ptr] [ptr @[[R2PROTO]], ptr @[[R3PROTO]]] }, align 8
// GNUSTEP2: @._OBJC_CLASS_Implementer = global {{.*}} ptr @[[GNUSTEP2_CLASS_PROTOS]]{{.*}}, align 8
