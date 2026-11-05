// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify replaceable global allocation/deallocation function handling matches
// OGCG parity in CIR lowering:
//  - declaration carries nobuiltin
//  - new/delete expression call sites carry builtin (+allocsize for new)

void *operator new(__SIZE_TYPE__);
void operator delete(void *, __SIZE_TYPE__) noexcept;

struct S {
  int x;
  ~S();
};

S *make() { return new S; }
void destroy(S *p) { delete p; }

// CHECK-LABEL: define dso_local noundef ptr @_Z4makev()
// CHECK: call noalias noundef nonnull ptr @_Znwm(i64 noundef 4) #[[NEW_CALL:[0-9]+]]

// CHECK: declare noundef nonnull ptr @_Znwm(i64 noundef) #[[NEW_DECL:[0-9]+]]

// CHECK-LABEL: define dso_local void @_Z7destroyP1S
// CHECK: call void @_ZdlPvm(ptr noundef {{.*}}, i64 noundef 4) #[[DEL_CALL:[0-9]+]]

// CHECK: declare void @_ZdlPvm(ptr noundef, i64 noundef) #[[DEL_DECL:[0-9]+]]

// CHECK: attributes #[[NEW_DECL]] = { nobuiltin allocsize(0)
// CHECK: attributes #[[DEL_DECL]] = { nobuiltin nounwind
// CHECK: attributes #[[NEW_CALL]] = { builtin allocsize(0) }
// CHECK: attributes #[[DEL_CALL]] = { builtin nounwind }
