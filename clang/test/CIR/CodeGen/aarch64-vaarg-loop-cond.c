// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s -check-prefix=LLVM
// RUN: %clang_cc1 -triple aarch64-none-linux-android24 -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=OGCG

// Test that va_arg in loop condition regions (do-while, while, for) on AArch64
// AAPCS is correctly wrapped in a cir.scope to contain the multi-block
// expansion within the single-block constrained condition region.

typedef __builtin_va_list va_list;
#define va_start(ap, param) __builtin_va_start(ap, param)
#define va_arg(ap, type) __builtin_va_arg(ap, type)
#define va_end(ap) __builtin_va_end(ap)

void dowhile_vaarg(int n, ...) {
  va_list ap;
  va_start(ap, n);
  do {
    n--;
  } while (va_arg(ap, int));
  va_end(ap);
}

// CIR-LABEL: cir.func {{.*}} @dowhile_vaarg
// CIR:       cir.do {
// CIR:         cir.yield
// CIR:       } while {
// CIR-NEXT:    %[[SCOPE_RES:.+]] = cir.scope {
// CIR:           %[[GR_OFFS_P:.+]] = cir.get_member %{{.+}}[3] {name = "gr_offs"}
// CIR:           cir.brcond
// CIR:           cir.yield %{{.+}} : !s32i
// CIR:         } : !s32i
// CIR:         %[[BOOL:.+]] = cir.cast int_to_bool %[[SCOPE_RES]]
// CIR:         cir.condition(%[[BOOL]])
// CIR:       }

// LLVM-LABEL: define {{.*}} @dowhile_vaarg
// LLVM:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// LLVM:       icmp sge i32
// LLVM:       phi ptr
// LLVM:       load i32, ptr
// LLVM:       icmp ne i32 %{{.+}}, 0
// LLVM:       br i1 %{{.+}}, label %{{.+}}, label %{{.+}}, !llvm.loop
// LLVM:       call void @llvm.va_end

// OGCG-LABEL: define {{.*}} @dowhile_vaarg
// OGCG:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// OGCG:       icmp sge i32
// OGCG:       phi ptr
// OGCG:       load i32, ptr
// OGCG:       icmp ne i32 %{{.+}}, 0
// OGCG:       br i1 %{{.+}}, label %{{.+}}, label %{{.+}}, !llvm.loop
// OGCG:       call void @llvm.va_end

int while_vaarg(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int sum = 0;
  while (va_arg(ap, int))
    sum++;
  va_end(ap);
  return sum;
}

// CIR-LABEL: cir.func {{.*}} @while_vaarg
// CIR:       cir.while {
// CIR-NEXT:    %[[SCOPE_RES:.+]] = cir.scope {
// CIR:           %[[GR_OFFS_P:.+]] = cir.get_member %{{.+}}[3] {name = "gr_offs"}
// CIR:           cir.brcond
// CIR:           cir.yield %{{.+}} : !s32i
// CIR:         } : !s32i
// CIR:         %[[BOOL:.+]] = cir.cast int_to_bool %[[SCOPE_RES]]
// CIR:         cir.condition(%[[BOOL]])
// CIR:       } do {

// LLVM-LABEL: define {{.*}} @while_vaarg
// LLVM:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// LLVM:       phi ptr
// LLVM:       load i32, ptr
// LLVM:       icmp ne i32 %{{.+}}, 0
// LLVM:       call void @llvm.va_end

// OGCG-LABEL: define {{.*}} @while_vaarg
// OGCG:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// OGCG:       phi ptr
// OGCG:       load i32, ptr
// OGCG:       icmp ne i32 %{{.+}}, 0
// OGCG:       call void @llvm.va_end

int for_vaarg(int n, ...) {
  va_list ap;
  va_start(ap, n);
  int sum = 0;
  for (; va_arg(ap, int); )
    sum++;
  va_end(ap);
  return sum;
}

// CIR-LABEL: cir.func {{.*}} @for_vaarg
// CIR:       cir.for : cond {
// CIR-NEXT:    %[[SCOPE_RES:.+]] = cir.scope {
// CIR:           %[[GR_OFFS_P:.+]] = cir.get_member %{{.+}}[3] {name = "gr_offs"}
// CIR:           cir.brcond
// CIR:           cir.yield %{{.+}} : !s32i
// CIR:         } : !s32i
// CIR:         %[[BOOL:.+]] = cir.cast int_to_bool %[[SCOPE_RES]]
// CIR:         cir.condition(%[[BOOL]])
// CIR:       } body {

// LLVM-LABEL: define {{.*}} @for_vaarg
// LLVM:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// LLVM:       phi ptr
// LLVM:       load i32, ptr
// LLVM:       icmp ne i32 %{{.+}}, 0
// LLVM:       call void @llvm.va_end

// OGCG-LABEL: define {{.*}} @for_vaarg
// OGCG:       getelementptr inbounds nuw %struct.__va_list, ptr %{{.+}}, i32 0, i32 3
// OGCG:       phi ptr
// OGCG:       load i32, ptr
// OGCG:       icmp ne i32 %{{.+}}, 0
// OGCG:       call void @llvm.va_end
