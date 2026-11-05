// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -fclangir -emit-mlir=cir-abi -o %t.cir %s
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s
// RUN: %clang_cc1 -triple x86_64-apple-darwin10 -Wno-objc-root-class -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ogcg.ll %s

@interface I
- (id) f;
@end

@implementation I
- (id) f { return (id)0; }
@end

// CIR-LABEL: cir.func no_inline optnone internal private{{( dso_local)?}} @"\01-[I f]"
// CIR: %[[NULL:.*]] = cir.const #cir.ptr<null> : !cir.ptr<!void>
// CIR: cir.return %[[NULL]] : !cir.ptr<!void>

// LLVM-LABEL: define internal ptr @"\01-[I f]"
// LLVM: ret ptr null

// OGCG-LABEL: define internal ptr @"\01-[I f]"
// OGCG: ret ptr null
