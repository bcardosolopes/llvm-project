// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -Wno-unused-value -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

struct Point {
  int x;
  int y;
  int z;
};
// CHECK-DAG: !rec_Point = !cir.record<struct "Point" {!s32i, !s32i, !s32i}

struct Incomplete;
// CHECK-DAG: !rec_Incomplete = !cir.record<struct "Incomplete" incomplete>

// In upstream, data member pointers are lowered to !s64i with byte offsets
int Point::*pt_member = &Point::x;
// CHECK: cir.global external @pt_member = #cir.int<0> : !s64i

auto test1() -> int Point::* {
  return &Point::y;
}
// CHECK: cir.func {{.*}} @_Z5test1v() -> !s64i
// CHECK:   %{{.+}} = cir.const #cir.int<4> : !s64i
// CHECK: }

int test2(const Point &pt, int Point::*member) {
  return pt.*member;
}
// CHECK: cir.func {{.*}} @_Z5test2RK5PointMS_i
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!rec_Point> -> !cir.ptr<!s8i>
// CHECK:   cir.ptr_stride %{{.+}}, %{{.+}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!s8i> -> !cir.ptr<!s32i>
// CHECK: }

int test3(const Point *pt, int Point::*member) {
  return pt->*member;
}
// CHECK: cir.func {{.*}} @_Z5test3PK5PointMS_i
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!rec_Point> -> !cir.ptr<!s8i>
// CHECK:   cir.ptr_stride %{{.+}}, %{{.+}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!s8i> -> !cir.ptr<!s32i>
// CHECK: }

auto test4(int Incomplete::*member) -> int Incomplete::* {
  return member;
}
// CHECK: cir.func {{.*}} @_Z5test4M10Incompletei(%arg0: !s64i loc({{.+}})) -> !s64i

int test5(Incomplete *ic, int Incomplete::*member) {
  return ic->*member;
}
// CHECK: cir.func {{.*}} @_Z5test5P10IncompleteMS_i
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!rec_Incomplete> -> !cir.ptr<!s8i>
// CHECK:   cir.ptr_stride %{{.+}}, %{{.+}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CHECK:   cir.cast bitcast %{{.+}} : !cir.ptr<!s8i> -> !cir.ptr<!s32i>
// CHECK: }

auto test_null() -> int Point::* {
  return nullptr;
}
// CHECK: cir.func {{.*}} @_Z9test_nullv
// CHECK:   %{{.+}} = cir.const #cir.int<-1> : !s64i
// CHECK: }

auto test_null_incomplete() -> int Incomplete::* {
  return nullptr;
}
// CHECK: cir.func {{.*}} @_Z20test_null_incompletev
// CHECK:   %{{.+}} = cir.const #cir.int<-1> : !s64i
// CHECK: }
