// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-call-conv-lowering -emit-mlir=cir-flat -mmlir --mlir-print-ir-after=cir-call-conv-lowering %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s -check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll -fclangir-call-conv-lowering
// RUN: FileCheck --input-file=%t.ll %s -check-prefix=LLVM

typedef struct {
  int a, b;
} S;

// CIR: cir.func {{.*}} @init(%arg0: !u64i
// CIR: %[[#V0:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, [""] {alignment = 4 : i64}
// CIR: %[[#V1:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: cir.store{{.*}} %arg0, %[[#V1]] : !u64i, !cir.ptr<!u64i>
// CIR: %[[#V2:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["__retval"] {alignment = 4 : i64}
// CIR: %[[#V3:]] = cir.const #cir.int<1> : !s32i
// CIR: %[[#V4:]] = cir.get_member %[[#V0]][0] {name = "a"} : !cir.ptr<!rec_S> -> !cir.ptr<!s32i>
// CIR: cir.store{{.*}} %[[#V3]], %[[#V4]] : !s32i, !cir.ptr<!s32i>
// CIR: %[[#V5:]] = cir.const #cir.int<2> : !s32i
// CIR: %[[#V6:]] = cir.get_member %[[#V0]][1] {name = "b"} : !cir.ptr<!rec_S> -> !cir.ptr<!s32i>
// CIR: cir.store{{.*}} %[[#V5]], %[[#V6]] : !s32i, !cir.ptr<!s32i>
// CIR: cir.copy %[[#V0]] to %[[#V2]] {{.*}}: !cir.ptr<!rec_S>
// CIR: %[[#V7:]] = cir.cast bitcast %[[#V2]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: %[[#V8:]] = cir.load align(4) %[[#V7]] : !cir.ptr<!u64i>, !u64i
// CIR: cir.return %[[#V8]] : !u64i

// LLVM: @init(i64 %[[#V0:]])
// LLVM: %[[#V2:]] = alloca %struct.S, i64 1, align 4
// LLVM: store i64 %[[#V0]], ptr %[[#V2]], align 4
// LLVM: %[[#V3:]] = alloca %struct.S, i64 1, align 4
// LLVM: %[[#V4:]] = getelementptr inbounds nuw %struct.S, ptr %[[#V2]], i32 0, i32 0
// LLVM: store i32 1, ptr %[[#V4]], align 4
// LLVM: %[[#V5:]] = getelementptr inbounds nuw %struct.S, ptr %[[#V2]], i32 0, i32 1
// LLVM: store i32 2, ptr %[[#V5]], align 4
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V3]], ptr{{.*}} %[[#V2]], i64 8, i1 false)
// LLVM: %[[#V6:]] = load i64, ptr %[[#V3]], align 4
// LLVM: ret i64 %[[#V6]]
S init(S s) {
  s.a = 1;
  s.b = 2;
  return s;
}

// CIR: cir.func {{.*}} @foo1
// CIR: %[[#V0:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s"]
// CIR: %[[#V1:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR: %[[#V2:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: %[[#V3:]] = cir.load align(4) %[[#V2]] : !cir.ptr<!u64i>, !u64i
// CIR: %[[#V4:]] = cir.call @init(%[[#V3]]) : (!u64i) -> !u64i
// CIR: %[[#V5:]] = cir.cast bitcast %[[#V1]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: cir.store{{.*}} %[[#V4]], %[[#V5]] : !u64i, !cir.ptr<!u64i>
// CIR: cir.copy %[[#V1]] to %[[#V0]]
// CIR: cir.return

// LLVM: @foo1()
// LLVM: %[[#V1:]] = alloca %struct.S, i64 1, align 4
// LLVM: %[[#V2:]] = alloca %struct.S, i64 1, align 4
// LLVM: %[[#V4:]] = load i64, ptr %[[#V1]], align 4
// LLVM: %[[#V5:]] = call i64 @init(i64 %[[#V4]])
// LLVM: store i64 %[[#V5]], ptr %[[#V2]], align 4
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V1]], ptr{{.*}} %[[#V2]], i64 8, i1 false)
void foo1() {
  S s;
  s = init(s);
}

// CIR: cir.func {{.*}} @foo2(%arg0: !u64i
// CIR: %[[#V0:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, [""] {alignment = 4 : i64}
// CIR: %[[#V1:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: cir.store{{.*}} %arg0, %[[#V1]] : !u64i, !cir.ptr<!u64i>
// CIR: %[[#V2:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["__retval"] {alignment = 4 : i64}
// CIR: %[[#V3:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["s2", init]
// CIR: %[[#V4:]] = cir.alloca !rec_S, !cir.ptr<!rec_S>, ["tmp"]
// CIR: %[[#V6:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: %[[#V7:]] = cir.load align(4) %[[#V6]] : !cir.ptr<!u64i>, !u64i
// CIR: %[[#V8:]] = cir.call @foo2(%[[#V7]]) : (!u64i) -> !u64i
// CIR: %[[#V9:]] = cir.cast bitcast %[[#V4]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: cir.store{{.*}} %[[#V8]], %[[#V9]] : !u64i, !cir.ptr<!u64i>
// CIR: cir.copy %[[#V4]] to %[[#V0]]
// CIR: cir.copy %[[#V0]] to %[[#V2]] {{.*}}: !cir.ptr<!rec_S>
// CIR: %[[#V10:]] = cir.cast bitcast %[[#V2]] : !cir.ptr<!rec_S> -> !cir.ptr<!u64i>
// CIR: %[[#V11:]] = cir.load align(4) %[[#V10]] : !cir.ptr<!u64i>, !u64i
// CIR: cir.return %[[#V11]] : !u64i

// LLVM: @foo2(i64 %[[#V0:]])
// LLVM: %[[#V2:]] = alloca %struct.S, i64 1, align 4
// LLVM: store i64 %[[#V0]], ptr %[[#V2]], align 4
// LLVM: %[[#V3:]] = alloca %struct.S, i64 1, align 4
// LLVM: %[[#V4:]] = alloca %struct.S, i64 1, align 4
// LLVM: %[[#V5:]] = alloca %struct.S, i64 1, align 4
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V4]], ptr{{.*}} @__const.foo2.s2, i64 8, i1 false)
// LLVM: %[[#V7:]] = load i64, ptr %[[#V2]], align 4
// LLVM: %[[#V8:]] = call i64 @foo2(i64 %[[#V7]])
// LLVM: store i64 %[[#V8]], ptr %[[#V5]], align 4
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V2]], ptr{{.*}} %[[#V5]], i64 8, i1 false)
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V3]], ptr{{.*}} %[[#V2]], i64 8, i1 false)
// LLVM: %[[#V9:]] = load i64, ptr %[[#V3]], align 4
// LLVM: ret i64 %[[#V9]]
S foo2(S s1) {
  S s2 = {1, 2};
  s1 = foo2(s1);
  return s1;
}

typedef struct {
  char a;
  char b;
} S2;

// CIR: cir.func {{.*}} @init2(%arg0: !u16i
// CIR: %[[#V0:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, [""] {alignment = 1 : i64}
// CIR: %[[#V1:]] = cir.cast bitcast %[[#V0]] : !cir.ptr<!rec_S2> -> !cir.ptr<!u16i>
// CIR: cir.store{{.*}} %arg0, %[[#V1]] : !u16i, !cir.ptr<!u16i>
// CIR: %[[#V2:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["__retval"] {alignment = 1 : i64}
// CIR: %[[#V3:]] = cir.const #cir.int<1> : !s8i
// CIR: %[[#V4:]] = cir.get_member %[[#V0]][0] {name = "a"} : !cir.ptr<!rec_S2> -> !cir.ptr<!s8i>
// CIR: cir.store{{.*}} %[[#V3]], %[[#V4]] : !s8i, !cir.ptr<!s8i>
// CIR: %[[#V5:]] = cir.const #cir.int<2> : !s8i
// CIR: %[[#V6:]] = cir.get_member %[[#V0]][1] {name = "b"} : !cir.ptr<!rec_S2> -> !cir.ptr<!s8i>
// CIR: cir.store{{.*}} %[[#V5]], %[[#V6]] : !s8i, !cir.ptr<!s8i>
// CIR: cir.copy %[[#V0]] to %[[#V2]] {{.*}}: !cir.ptr<!rec_S2>
// CIR: %[[#V8:]] = cir.cast bitcast %[[#V2]] : !cir.ptr<!rec_S2> -> !cir.ptr<!u16i>
// CIR: %[[#V9:]] = cir.load align(1) %[[#V8]] : !cir.ptr<!u16i>, !u16i
// CIR: cir.return %[[#V9]] : !u16i

// LLVM: @init2(i16 %[[#V0:]])
// LLVM: %[[#V2:]] = alloca %struct.S2, i64 1, align 1
// LLVM: store i16 %[[#V0]], ptr %[[#V2]], align 1
// LLVM: %[[#V3:]] = alloca %struct.S2, i64 1, align 1
// LLVM: %[[#V4:]] = getelementptr inbounds nuw %struct.S2, ptr %[[#V2]], i32 0, i32 0
// LLVM: store i8 1, ptr %[[#V4]], align 1
// LLVM: %[[#V5:]] = getelementptr inbounds nuw %struct.S2, ptr %[[#V2]], i32 0, i32 1
// LLVM: store i8 2, ptr %[[#V5]], align 1
// LLVM: call void @llvm.memcpy.p0.p0.i64(ptr{{.*}} %[[#V3]], ptr{{.*}} %[[#V2]], i64 2, i1 false)
// LLVM: %[[#V6:]] = load i16, ptr %[[#V3]], align 1
// LLVM: ret i16 %[[#V6]]
S2 init2(S2 s) {
  s.a = 1;
  s.b = 2;
  return s;
}

// CIR: cir.func {{.*}} @foo3()
// CIR: %[[#V0:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["s"]
// CIR: %[[#V1:]] = cir.alloca !rec_S2, !cir.ptr<!rec_S2>, ["tmp"]
// CIR: cir.call @init2({{.*}})
// CIR: cir.cast bitcast %[[#V1]]
// CIR: cir.copy %[[#V1]] to %[[#V0]]
// CIR: cir.return

// LLVM: @foo3()
// LLVM: %[[#V1:]] = alloca %struct.S2, i64 1, align 1
// LLVM: %[[#V2:]] = alloca %struct.S2, i64 1, align 1
// LLVM: %[[#V4:]] = load i16, ptr %[[#V1]], align 1
// LLVM: %[[#V5:]] = call i16 @init2(i16 %[[#V4]])
// LLVM: store i16 %[[#V5]], ptr %[[#V2]], align 1
// LLVM: call void @llvm.memcpy{{.*}}(ptr{{.*}} %[[#V1]], ptr{{.*}} %[[#V2]], i64 2, i1 false)
void foo3() {
  S2 s;
  s = init2(s);
}
