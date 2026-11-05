// RUN: %clang_cc1 -x objective-c -triple x86_64-apple-darwin10 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s
// RUN: %clang_cc1 -x objective-c -triple x86_64-apple-darwin10 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -x objective-c -triple x86_64-apple-darwin10 -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s

unsigned long strlen(const char *);

const char g_array[] = @encode(int);
const char *g_ptr = @encode(int);
char (*g_arrptr)[2] = &@encode(int);

const char *local_ptr(void) {
  return @encode(int);
}

unsigned long local_array_len(void) {
  const char array[] = @encode(int);
  return strlen(array);
}

// CIR: cir.global{{.*}} @g_array = #cir.const_array<"i\00" : !cir.array<!s8i x 2>> : !cir.array<!s8i x 2>
// CIR: cir.global "private" constant{{.*}} @"[[ENC:.+]]" = #cir.const_array<"i\00" : !cir.array<!s8i x 2>> : !cir.array<!s8i x 2>
// CIR: cir.global{{.*}} @g_ptr = #cir.global_view<@"[[ENC]]"> : !cir.ptr<!s8i>
// CIR: cir.global{{.*}} @g_arrptr = #cir.global_view<@"[[ENC]]"> : !cir.ptr<!cir.array<!s8i x 2>>

// LLVM: @g_array ={{.*}} constant [2 x i8] c"i\00"
// LLVM: @[[ENC:.+]] = private unnamed_addr constant [2 x i8] c"i\00"
// LLVM: @g_ptr ={{.*}} global ptr @[[ENC]], align 8
// LLVM: @g_arrptr ={{.*}} global ptr @[[ENC]], align 8
// LLVM-LABEL: define{{.*}} ptr @local_ptr()
// LLVM: ret ptr @[[ENC]]

// OGCG: @g_array ={{.*}} constant [2 x i8] c"i\00"
// OGCG: @[[ENC:.+]] = private unnamed_addr constant [2 x i8] c"i\00"
// OGCG: @g_ptr ={{.*}} global ptr @[[ENC]], align 8
// OGCG: @g_arrptr ={{.*}} global ptr @[[ENC]], align 8
// OGCG-LABEL: define{{.*}} ptr @local_ptr()
// OGCG: ret ptr @[[ENC]]
