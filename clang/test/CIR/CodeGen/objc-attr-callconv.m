// RUN: %clang_cc1 -triple i386-unknown-unknown -fclangir -emit-cir -o %t.cir %s
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -fclangir -emit-llvm -o %t.ll %s
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple i386-unknown-unknown -emit-llvm -o %t.ogcg.ll %s
// RUN: FileCheck --input-file=%t.ogcg.ll --check-prefix=OGCG %s

@interface Test
- (void)test;
@end

@implementation Test
- (void)test __attribute__((stdcall)) {}
// CIR: cir.func {{.*}}internal {{.*}}@"\01-[Test test]"({{.*}}) cc(x86_stdcall)
// LLVM: define internal x86_stdcallcc void @"\01-[Test test]"
// OGCG: define internal x86_stdcallcc void @"\01-[Test test]"

- (void)test2 __attribute__((ms_abi)) {}
// CIR: cir.func {{.*}}internal {{.*}}@"\01-[Test test2]"({{.*}}) cc(win64)
// LLVM: define internal win64cc void @"\01-[Test test2]"
// OGCG: define internal win64cc void @"\01-[Test test2]"
@end
