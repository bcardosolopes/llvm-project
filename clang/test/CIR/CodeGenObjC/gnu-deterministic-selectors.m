// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gnustep-1.5 %s -fclangir -emit-llvm -o - | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-freebsd -fobjc-runtime=gcc %s -fclangir -emit-llvm -o - | FileCheck %s

// Check that legacy GNU selector globals are canonicalized to a deterministic
// .objc_selector_list, matching OGCG's sorted selector ordering.
// CHECK: target triple = "x86_64-unknown-freebsd"
// CHECK-NOT: section "__objc_selectors"
// CHECK: @.objc_selector_list = internal global [6 x { ptr, ptr }] [{ ptr, ptr } { ptr @.objc_sel_name.1, ptr null }, { ptr, ptr } { ptr @.objc_sel_name.2, ptr null }, { ptr, ptr } { ptr @.objc_sel_name.4, ptr null }, { ptr, ptr } { ptr @.objc_sel_name.3, ptr null }, { ptr, ptr } { ptr @.objc_sel_name, ptr null }, { ptr, ptr } zeroinitializer], align 8

// CHECK-LABEL: define dso_local void @f()
// CHECK: store ptr getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 64), ptr %{{.*}}, align 8
// CHECK: store ptr @.objc_selector_list, ptr %{{.*}}, align 8
// CHECK: store ptr getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 16), ptr %{{.*}}, align 8
// CHECK: store ptr getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 48), ptr %{{.*}}, align 8
// CHECK: store ptr getelementptr inbounds nuw (i8, ptr @.objc_selector_list, i64 32), ptr %{{.*}}, align 8

void f(void) {
  SEL a = @selector(z);
  SEL b = @selector(a);
  SEL c = @selector(g);
  SEL d = @selector(l);
  SEL e = @selector(j);
}
