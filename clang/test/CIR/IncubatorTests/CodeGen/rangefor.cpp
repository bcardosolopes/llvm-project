// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -I%S/../Inputs -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

#include "std-cxx.h"

typedef enum enumy {
  Unknown = 0,
  Some = 1000024002,
} enumy;

typedef struct triple {
  enumy type;
  void* __attribute__((__may_alias__)) next;
  unsigned image;
} triple;

void init(unsigned numImages) {
  std::vector<triple> images(numImages);
  for (auto& image : images) {
    image = {Some};
  }
}

// CHECK-DAG: !rec_triple = !cir.record<struct "triple" {!u32i, !cir.ptr<!void>, !u32i}>
// CHECK-DAG: ![[VEC:.*]] = !cir.record<class "std::vector<triple>" {!cir.ptr<!rec_triple>, !cir.ptr<!rec_triple>, !cir.ptr<!rec_triple>}>
// CHECK-DAG: ![[VEC_IT:.*]] = !cir.record<struct "__vector_iterator<triple, triple *, triple &>" {!cir.ptr<!rec_triple>}>

// CHECK: cir.func {{.*}} @_Z4initj(%arg0: !u32i
// CHECK:   %0 = cir.alloca !u32i, !cir.ptr<!u32i>, ["numImages", init]
// CHECK:   %1 = cir.alloca ![[VEC]], !cir.ptr<![[VEC]]>, ["images", init]
// CHECK:   cir.store{{.*}} %arg0, %0 : !u32i, !cir.ptr<!u32i>
// CHECK:   %2 = cir.load{{.*}} %0 : !cir.ptr<!u32i>, !u32i
// CHECK:   %3 = cir.cast integral %2 : !u32i -> !u64i
// CHECK:   cir.call @_ZNSt6vectorI6tripleEC1Em(%1, %3) : (!cir.ptr<![[VEC]]>, !u64i) -> ()
// CHECK:   cir.scope {
// CHECK:     %4 = cir.alloca !cir.ptr<![[VEC]]>, !cir.ptr<!cir.ptr<![[VEC]]>>, ["__range1", init, const]
// CHECK:     %5 = cir.alloca ![[VEC_IT]], !cir.ptr<![[VEC_IT]]>, ["__begin1", init]
// CHECK:     %6 = cir.alloca ![[VEC_IT]], !cir.ptr<![[VEC_IT]]>, ["__end1", init]
// CHECK:     %7 = cir.alloca !cir.ptr<!rec_triple>, !cir.ptr<!cir.ptr<!rec_triple>>, ["image", init, const]
// CHECK:     cir.call @_ZNSt6vectorI6tripleE5beginEv(
// CHECK:     cir.call @_ZNSt6vectorI6tripleE3endEv(
// CHECK:     cir.for : cond {
// CHECK:       cir.call @_ZNK17__vector_iteratorI6triplePS0_RS0_EneERKS3_(%5, %6) : (!cir.ptr<![[VEC_IT]]>, !cir.ptr<![[VEC_IT]]>) -> !cir.bool
// CHECK:       cir.condition(
// CHECK:     } body {
// CHECK:       %{{.+}} = cir.call @_ZNK17__vector_iteratorI6triplePS0_RS0_EdeEv(%5) : (!cir.ptr<![[VEC_IT]]>) -> !cir.ptr<!rec_triple>
// CHECK:       cir.get_member %{{.+}}[0] {name = "type"} : !cir.ptr<!rec_triple> -> !cir.ptr<!u32i>
// CHECK:       cir.const #cir.int<1000024002> : !u32i
// CHECK:       cir.get_member %{{.+}}[1] {name = "next"} : !cir.ptr<!rec_triple> -> !cir.ptr<!cir.ptr<!void>>
// CHECK:       cir.get_member %{{.+}}[2] {name = "image"} : !cir.ptr<!rec_triple> -> !cir.ptr<!u32i>
// CHECK:       cir.call @_ZN6tripleaSEOS_(
// CHECK:       cir.yield
// CHECK:     } step {
// CHECK:       %{{.+}} = cir.call @_ZN17__vector_iteratorI6triplePS0_RS0_EppEv(%5) : (!cir.ptr<![[VEC_IT]]>) -> !cir.ptr<![[VEC_IT]]>
// CHECK:       cir.yield
// CHECK:     }
// CHECK:   }
// CHECK:   cir.return
