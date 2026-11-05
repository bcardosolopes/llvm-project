// RUN: %clang_cc1 -triple arm64-apple-ios9 -fobjc-runtime=ios-9.0 -fobjc-arc -std=c++11 -O -disable-llvm-passes -fclangir -emit-mlir=cir-abi -o - %s | FileCheck %s -check-prefix=CIR
// RUN: %clang_cc1 -triple arm64-apple-ios9 -fobjc-runtime=ios-9.0 -fobjc-arc -std=c++11 -O -disable-llvm-passes -fclangir -emit-mlir=cir-abi -o %t.cir %s
// RUN: cir-translate -cir-to-llvmir %t.cir | FileCheck %s -check-prefixes=LLVM,LLVM-ARM64,LLVM-TRANSLATE
// RUN: %clang_cc1 -triple arm64-apple-ios9 -fobjc-runtime=ios-9.0 -fobjc-arc -std=c++11 -O -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefixes=LLVM,LLVM-ARM64,LLVM-DIRECT
// RUN: %clang_cc1 -triple x86_64-apple-macosx10.10 -fobjc-runtime=macosx-10.10 -fobjc-arc -std=c++11 -O -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefixes=LLVM,LLVM-X86_64,LLVM-DIRECT
// RUN: %clang_cc1 -triple arm64_32-apple-watchos7 -fobjc-runtime=watchos-7.0 -fobjc-arc -std=c++11 -O -disable-llvm-passes -fclangir -emit-llvm -o - %s | FileCheck %s -check-prefixes=LLVM,LLVM-ARM64,LLVM-WATCHOS,LLVM-DIRECT

id foo(void);

// CIR-LABEL: cir.func @_Z14test_list_initv(
// CIR: %[[CALL1:.*]] = cir.call @_Z3foov() {cir.arc.attachedcall = "llvm.objc.retainAutoreleasedReturnValue", cir.arc.no_objc_arc_exceptions} : () -> !cir.ptr<!void>
// CIR: cir.call @llvm.objc.clang.arc.noop.use(%[[CALL1]]) nothrow {{.*}}: (!cir.ptr<!void>) -> ()
// CIR: %[[RET1:.*]] = cir.call @objc_retain(%[[CALL1]]) nothrow : (!cir.ptr<!void>) -> !cir.ptr<!void>
// CIR-DAG: cir.store align(8) %[[RET1]]
// CIR-DAG: cir.call @objc_release(%[[CALL1]]) nothrow {cir.imprecise_release}

// LLVM-WATCHOS: target triple = "arm64_32-apple-watchos7"
// LLVM-LABEL: define{{.*}} void @_Z14test_list_initv(
// LLVM: %[[CALL1:.*]] = call noundef ptr @_Z3foov() [ "clang.arc.attachedcall"(ptr @llvm.objc.retainAutoreleasedReturnValue) ]
// LLVM-DIRECT-SAME: !clang.arc.no_objc_arc_exceptions
// LLVM-NEXT: call void {{\((ptr, )?\.\.\.\)}} @llvm.objc.clang.arc.noop.use(ptr{{( noundef)?}} %[[CALL1]])
// LLVM-NEXT: %[[RET1:.*]] = call ptr @llvm.objc.retain(ptr %[[CALL1]])
// LLVM-DIRECT-DAG: store ptr %[[RET1]]
// LLVM-DAG: call void @llvm.objc.release(ptr %[[CALL1]]){{.*}}!clang.imprecise_release
// cir-translate promotes t, since the module is optimized, so no store is left.
// LLVM-TRANSLATE-DAG: call void @llvm.objc.release(ptr %[[RET1]])
// LLVM-DIRECT-DAG: !{}

// LLVM-ARM64-DAG: !{i32 1, !"clang.arc.retainAutoreleasedReturnValueMarker", !"mov{{.*}}marker for objc_retainAutoreleaseReturnValue"}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Version", i32 2}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Image Info Version", i32 0}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Image Info Section", !"__DATA,__objc_imageinfo,regular,no_dead_strip"}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Garbage Collection", i8 0}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Class Properties", i32 64}
// LLVM-ARM64-DAG: !{i32 1, !"Objective-C Enforce ClassRO Pointer Signing", i8 0}
// LLVM-ARM64-DAG: !{i32 1, !"wchar_size", i32 4}

// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Version", i32 2}
// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Image Info Version", i32 0}
// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Image Info Section", !"__DATA,__objc_imageinfo,regular,no_dead_strip"}
// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Garbage Collection", i8 0}
// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Class Properties", i32 64}
// LLVM-X86_64-DAG: !{i32 1, !"Objective-C Enforce ClassRO Pointer Signing", i8 0}
// LLVM-X86_64-DAG: !{i32 1, !"wchar_size", i32 4}
// LLVM-X86_64-NOT: !"clang.arc.retainAutoreleasedReturnValueMarker"

void test_list_init() {
  auto t = id{foo()};
}
