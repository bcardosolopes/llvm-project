// RUN: %clang_cc1 -fobjc-gc -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -o - %s | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -fclangir -fobjc-gc -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-cir -o - %s | FileCheck %s --check-prefix=CIR %}
// RUN: %if cir-support %{ %clang_cc1 -fclangir -fobjc-gc -triple x86_64-apple-darwin10 -fobjc-runtime=macosx-fragile-10.5 -emit-llvm -o - %s | FileCheck %s --check-prefix=CIR-LLVM %}

struct POD {
  int array[3][4];
  id objc_obj;
};

struct D  { 
  POD pod_array[2][3];
};

@interface I
{
  D Property1;
}
@property D Property1;
- (D) val;
- (void) set : (D) d1;
@end

@implementation I
@synthesize Property1;
- (D) val { return Property1; }
- (void) set : (D) d1 { Property1 = d1; }
@end
// CHECK-COUNT-2: call ptr @objc_memmove_collectable

// CIR-LABEL: @"\01-[I val]"(
// CIR: cir.call @objc_memmove_collectable{{.*}} nothrow : (!cir.ptr<!void>, !cir.ptr<!void>, !u64i) -> !cir.ptr<!void>
// CIR: cir.func private @objc_memmove_collectable(!cir.ptr<!void>, !cir.ptr<!void>, !u64i) -> !cir.ptr<!void>
// CIR-LABEL: @"\01-[I set:]"(
// CIR: cir.call @objc_memmove_collectable{{.*}} nothrow : (!cir.ptr<!void>, !cir.ptr<!void>, !u64i) -> !cir.ptr<!void>

// CIR-LLVM-LABEL: define internal void @"\01-[I val]"
// CIR-LLVM: call ptr @objc_memmove_collectable(ptr {{.*}}, ptr {{.*}}, i64 336) #[[NOUNWIND:[0-9]+]]
// CIR-LLVM: declare ptr @objc_memmove_collectable(ptr, ptr, i64)
// CIR-LLVM-LABEL: define internal void @"\01-[I set:]"
// CIR-LLVM: call ptr @objc_memmove_collectable(ptr {{.*}}, ptr {{.*}}, i64 336) #[[NOUNWIND]]
// CIR-LLVM: attributes #[[NOUNWIND]] = { nounwind }
