// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -x objective-c %s -o - | FileCheck %s --check-prefix=OGCG
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin10 -emit-llvm -fobjc-arc -fclangir -x objective-c %s -o - | FileCheck %s --check-prefix=CIR-LLVM %}
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin10 -emit-mlir=cir-abi -fobjc-arc -fclangir -x objective-c %s -o - | FileCheck %s --check-prefix=CIR %}

__attribute__((objc_root_class))
@interface Root
@end

__attribute__((weak_import))
@interface WeakBase : Root
@end

@interface WeakDerived : WeakBase
+ (void) abort __attribute__((noreturn, objc_direct));
@end

void testWeakDirect(void) { [WeakDerived abort]; }

// OGCG-LABEL: define{{.*}} void @testWeakDirect()
// OGCG: call void @"\01+[WeakDerived abort]"(ptr {{.*}})
// OGCG-NEXT: ret void

// CIR-LLVM-LABEL: define{{.*}} void @testWeakDirect()
// CIR-LLVM: call void @"\01+[WeakDerived abort]"(ptr noundef {{.*}})
// CIR-LLVM-NEXT: ret void

// CIR: cir.func private @"\01+[WeakDerived abort]"(!cir.ptr<!void> {cir.noundef})
// CIR-NOT: noreturn = #cir.noreturn
// CIR-LABEL: cir.func no_inline optnone @testWeakDirect()
// CIR: cir.call @"\01+[WeakDerived abort]"(%{{.*}}) : (!cir.ptr<!void>) -> ()
// CIR-NEXT: cir.return
