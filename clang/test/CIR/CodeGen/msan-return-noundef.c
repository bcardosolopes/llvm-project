// REQUIRES: x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fsanitize=memory -x c %s -o %t.cir.c.ll
// RUN: FileCheck --input-file=%t.cir.c.ll %s --check-prefix=C-CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fsanitize=memory -x c %s -o %t.og.c.ll
// RUN: FileCheck --input-file=%t.og.c.ll %s --check-prefix=C-CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fclangir -fsanitize=memory -x c++ %s -o %t.cir.cxx.ll
// RUN: FileCheck --input-file=%t.cir.cxx.ll %s --check-prefix=CXX-CHECK
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -fsanitize=memory -x c++ %s -o %t.og.cxx.ll
// RUN: FileCheck --input-file=%t.og.cxx.ll %s --check-prefix=CXX-CHECK

#ifndef __cplusplus

int c_decl(void);
int c_def(void) { return 1; }
int c_call(void) { return c_decl() + c_def(); }

// C-CHECK-LABEL: define dso_local noundef i32 @c_def()
// C-CHECK-LABEL: define dso_local noundef i32 @c_call()
// C-CHECK: call noundef i32 @c_decl()
// C-CHECK: call noundef i32 @c_def()
// C-CHECK: declare noundef i32 @c_decl()

#else

extern "C" int extern_c_decl(void);
extern "C" int extern_c_def(void) { return 2; }
int cpp_call_extern_c(void) { return extern_c_decl() + extern_c_def(); }

// CXX-CHECK-LABEL: define dso_local noundef i32 @extern_c_def()
// CXX-CHECK-LABEL: define dso_local noundef i32 @_Z17cpp_call_extern_cv()
// CXX-CHECK: call noundef i32 @extern_c_decl()
// CXX-CHECK: call noundef i32 @extern_c_def()
// CXX-CHECK: declare noundef i32 @extern_c_decl()

#endif
