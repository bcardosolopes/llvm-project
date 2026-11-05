// RUN: %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -emit-llvm %s -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-apple-darwin -std=c++11 -emit-llvm -fclangir %s -o - | FileCheck %s -check-prefix=CIR %}

extern "C" int printf(...);

int count;

struct S {
  S() : iS(++count) { printf("S::S(%d)\n", iS); }
  ~S() { printf("S::~S(%d)\n", iS); }
  int iS;
};


S arr[2][1];
S s1;
S arr1[3];
static S sarr[4];

int main () {}
S arr2[2];
static S sarr1[4];
S s2;
S arr3[3];

// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CHECK: call {{.*}} @__cxa_atexit
// CIR: define internal void @__cxx_global_array_dtor.1(
// CIR: define internal void @__cxx_global_var_init()
// CIR: call i32 @__cxa_atexit(ptr @__cxx_global_array_dtor.1, ptr null, ptr @__dso_handle)
// CIR: define internal void @__cxx_global_array_dtor.2(
// CIR: define internal void @__cxx_global_var_init.2()
// CIR: call i32 @__cxa_atexit(ptr @__cxx_global_array_dtor.2, ptr null, ptr @__dso_handle)

struct T {
  double d;
  int n;
  ~T();
};
T t[2][3] = { 1.0, 2, 3.0, 4, 5.0, 6, 7.0, 8, 9.0, 10, 11.0, 12 };

// CHECK: call {{.*}} @__cxa_atexit
// CHECK: getelementptr inbounds (%struct.T, ptr @t, i64 6)
// CHECK: call void @_ZN1TD1Ev
// CHECK: icmp eq {{.*}} @t
// CHECK: br i1 {{.*}}

static T t2[2][3] = { 1.0, 2, 3.0, 4, 5.0, 6, 7.0, 8, 9.0, 10, 11.0, 12 };

// CHECK: call {{.*}} @__cxa_atexit
// CHECK: getelementptr inbounds (%struct.T, ptr @_ZL2t2, i64 6)
// CHECK: call void @_ZN1TD1Ev
// CHECK: icmp eq {{.*}} @_ZL2t2
// CHECK: br i1 {{.*}}

using U = T[2][3];
U &&u = U{ {{1.0, 2}, {3.0, 4}, {5.0, 6}}, {{7.0, 8}, {9.0, 10}, {11.0, 12}} };

// CHECK: call {{.*}} @__cxa_atexit
// CHECK: getelementptr inbounds (%struct.T, ptr @_ZGR1u_, i64 6)
// CHECK: call void @_ZN1TD1Ev
// CHECK: icmp eq {{.*}} @_ZGR1u_
// CHECK: br i1 {{.*}}
// CIR: define internal void @__cxx_global_array_dtor.7(
// CIR: icmp ne ptr {{.*}}, @t
// CIR: getelementptr inbounds %struct.T, ptr {{.*}}, i64 -1
// CIR: call void @_ZN1TD1Ev
// CIR: define internal void @__cxx_global_array_dtor.8(
// CIR: icmp ne ptr {{.*}}, @_ZL2t2
// CIR: getelementptr inbounds %struct.T, ptr {{.*}}, i64 -1
// CIR: call void @_ZN1TD1Ev
// CIR: define internal void @__cxx_global_array_dtor(
// CIR: icmp ne ptr {{.*}}, @_ZGR1u_
// CIR: getelementptr inbounds %struct.T, ptr {{.*}}, i64 -1
// CIR: call void @_ZN1TD1Ev
