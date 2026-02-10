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

// CHECK: cir.func {{.*}} @_Z4initj(%arg0: !u32i
// CHECK:   cir.alloca !u32i, !cir.ptr<!u32i>, ["numImages", init]
// CHECK:   cir.alloca !rec_{{.*}}, !cir.ptr<!rec_{{.*}}>, ["images", init]
// CHECK:   cir.store{{.*}} %arg0, %{{.+}} : !u32i, !cir.ptr<!u32i>
// CHECK:   cir.cast integral %{{.+}} : !u32i -> !u64i
// CHECK:   cir.call @_ZNSt6vectorI6tripleEC1Em(
// CHECK:   cir.scope {
// CHECK:     cir.alloca !cir.ptr<!rec_{{.*}}>, !cir.ptr<!cir.ptr<!rec_{{.*}}>>, ["__range1", init, const]
// CHECK:     cir.alloca !rec_{{.*}}, !cir.ptr<!rec_{{.*}}>, ["__begin1", init]
// CHECK:     cir.alloca !rec_{{.*}}, !cir.ptr<!rec_{{.*}}>, ["__end1", init]
// CHECK:     cir.alloca !cir.ptr<!rec_triple>, !cir.ptr<!cir.ptr<!rec_triple>>, ["image", init, const]
// CHECK:     cir.call @_ZNSt6vectorI6tripleE5beginEv(
// CHECK:     cir.call @_ZNSt6vectorI6tripleE3endEv(
// CHECK:     cir.for : cond {
// CHECK:       cir.call @_ZNK17__vector_iteratorI6triplePS0_RS0_EneERKS3_(
// CHECK:       cir.condition(
// CHECK:     } body {
// CHECK:       cir.call @_ZNK17__vector_iteratorI6triplePS0_RS0_EdeEv(
// CHECK:       cir.get_member %{{.+}}[0] {name = "type"} : !cir.ptr<!rec_triple> -> !cir.ptr<!u32i>
// CHECK:       cir.const #cir.int<1000024002> : !u32i
// CHECK:       cir.get_member %{{.+}}[1] {name = "next"} : !cir.ptr<!rec_triple> -> !cir.ptr<!cir.ptr<!void>>
// CHECK:       cir.get_member %{{.+}}[2] {name = "image"} : !cir.ptr<!rec_triple> -> !cir.ptr<!u32i>
// CHECK:       cir.call @_ZN6tripleaSEOS_(
// CHECK:       cir.yield
// CHECK:     } step {
// CHECK:       cir.call @_ZN17__vector_iteratorI6triplePS0_RS0_EppEv(
// CHECK:       cir.yield
// CHECK:     }
// CHECK:   }
// CHECK:   cir.return
