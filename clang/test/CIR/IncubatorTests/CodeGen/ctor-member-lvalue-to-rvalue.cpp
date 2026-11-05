// RUN: %clang_cc1 -std=c++17 -mconstructor-aliases -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -mconstructor-aliases -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - | FileCheck %s --check-prefix=OGCG

// TODO: support -mno-constructor-aliases

struct String {
  long size;
  String(const String &s) : size{s.size} {}

  String() {}
};

void foo() {
  String s;
  String s1{s};
}

// String has no virtual base, so its complete-object constructors are folded
// into the base ones and the calls target C2, as in OGCG.

// CIR: cir.func {{.*}} @_Z3foov() {{.*}} {
// CIR:  %0 = cir.alloca !rec_String, !cir.ptr<!rec_String>, ["s", init] {alignment = 8 : i64}
// CIR:  %1 = cir.alloca !rec_String, !cir.ptr<!rec_String>, ["s1", init] {alignment = 8 : i64}
// CIR:  cir.call @_ZN6StringC2Ev(%0) : (!cir.ptr<!rec_String>) -> ()
// CIR:  cir.call @_ZN6StringC2ERKS_(%1, %0) : (!cir.ptr<!rec_String>, !cir.ptr<!rec_String>) -> ()
// CIR:  cir.return
// }

// CIR: cir.func {{.*}} @_ZN6StringC2ERKS_
// CIR:     %0 = cir.alloca !cir.ptr<!rec_String>, !cir.ptr<!cir.ptr<!rec_String>>, ["this", init] {alignment = 8 : i64}
// CIR:     %1 = cir.alloca !cir.ptr<!rec_String>, !cir.ptr<!cir.ptr<!rec_String>>, ["s", init, const] {alignment = 8 : i64}
// CIR:     cir.store{{.*}} %arg0, %0
// CIR:     cir.store{{.*}} %arg1, %1
// CIR:     %2 = cir.load{{.*}} %0
// CIR:     %3 = cir.get_member %2[0] {name = "size"}
// CIR:     %4 = cir.load{{.*}} %1
// CIR:     %5 = cir.get_member %4[0] {name = "size"}
// CIR:     %6 = cir.load{{.*}} %5 : !cir.ptr<!s64i>, !s64i
// CIR:     cir.store{{.*}} %6, %3 : !s64i, !cir.ptr<!s64i>
// CIR:     cir.return
// CIR:   }

// OGCG: define {{.*}} @_Z3foov()
// OGCG:   %[[S:.*]] = alloca %struct.String
// OGCG:   %[[S1:.*]] = alloca %struct.String
// OGCG:   call void @_ZN6StringC2Ev(ptr {{.*}} %[[S]])
// OGCG:   call void @_ZN6StringC2ERKS_(ptr {{.*}} %[[S1]], ptr {{.*}} %[[S]])
// OGCG:   ret void

// OGCG: define {{.*}} @_ZN6StringC2ERKS_(ptr {{.*}} %this, ptr {{.*}} %s)
// OGCG:   %[[THIS_ADDR:.*]] = alloca ptr
// OGCG:   %[[S_ADDR:.*]] = alloca ptr
// OGCG:   store ptr %this, ptr %[[THIS_ADDR]]
// OGCG:   store ptr %s, ptr %[[S_ADDR]]
// OGCG:   %[[THIS:.*]] = load ptr, ptr %[[THIS_ADDR]]
// OGCG:   %[[SIZE:.*]] = getelementptr inbounds {{.*}} %struct.String, ptr %[[THIS]], i32 0, i32 0
// OGCG:   %[[S:.*]] = load ptr, ptr %[[S_ADDR]]
// OGCG:   %[[SIZE2:.*]] = getelementptr inbounds {{.*}} %struct.String, ptr %[[S]], i32 0, i32 0
// OGCG:   %[[VAL:.*]] = load i64, ptr %[[SIZE2]]
// OGCG:   store i64 %[[VAL]], ptr %[[SIZE]]
// OGCG:   ret void
