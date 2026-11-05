// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c89 -w -fclangir -clangir-disable-passes -emit-cir %s -o %t.raw.cir
// RUN: FileCheck --input-file=%t.raw.cir %s --check-prefix=RAW
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c89 -w -fclangir -emit-mlir=cir-abi %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c89 -w -fclangir -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c89 -w -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

#pragma pack(push, 2)
struct Small {
  int field;
};
#pragma pack(pop)

struct Node;
typedef long (*Callback)(struct Node *, struct Small);

struct Node {
  Callback cb;
};

long call_small(struct Node *node, struct Small value) {
  return node->cb(node, value);
}

// RAW-LABEL: cir.func{{.*}} @call_small
// RAW: %[[VALUE_ADDR:.+]] = cir.alloca !rec_Small, !cir.ptr<!rec_Small>, ["value", init] {alignment = 2 : i64}
// RAW: %[[VALUE:.+]] = cir.load align(2) %[[VALUE_ADDR]] : !cir.ptr<!rec_Small>, !rec_Small
// RAW: cir.call %{{.+}}(%{{.+}}, %[[VALUE]]) : (!cir.ptr<!cir.func<(!cir.ptr<!rec_Node>, !rec_Small) -> !s64i>>, !cir.ptr<!rec_Node>, !rec_Small) -> !s64i

// CIR-LABEL: cir.func{{.*}} @call_small
// CIR: %[[VALUE_ADDR:.+]] = cir.alloca !rec_Small, !cir.ptr<!rec_Small>, [""] {alignment = 2 : i64}
// CIR: %[[STORE_ADDR:.+]] = cir.cast bitcast %[[VALUE_ADDR]] : !cir.ptr<!rec_Small> -> !cir.ptr<!s32i>
// CIR: cir.store align(2) %arg1, %[[STORE_ADDR]] : !s32i, !cir.ptr<!s32i>
// CIR: %[[RELOAD_ADDR:.+]] = cir.cast bitcast %[[VALUE_ADDR]] : !cir.ptr<!rec_Small> -> !cir.ptr<!s32i>
// CIR: %[[COERCE_VAL:.+]] = cir.load align(2) %[[RELOAD_ADDR]] : !cir.ptr<!s32i>, !s32i
// CIR: cir.call %{{.+}}(%{{.+}}, %[[COERCE_VAL]]) {cir.call_arg_attrs = [{cir.noundef}, {}]} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_Node>, !s32i) -> !s64i>>, !cir.ptr<!rec_Node>, !s32i) -> !s64i

// LLVM-LABEL: define dso_local i64 @call_small(
// LLVM: %[[VALUE:.+]] = load i32, ptr %{{.+}}, align 2
// LLVM: %[[CALL:.+]] = call i64 %{{.+}}(ptr noundef %{{.+}}, i32 %[[VALUE]])

// OGCG-LABEL: define dso_local i64 @call_small(
// OGCG: %[[STORE_PTR:.+]] = getelementptr inbounds nuw %struct.Small, ptr %{{.+}}, i32 0, i32 0
// OGCG: store i32 %value.coerce, ptr %[[STORE_PTR]], align 2
// OGCG: %[[RELOAD_PTR:.+]] = getelementptr inbounds nuw %struct.Small, ptr %{{.+}}, i32 0, i32 0
// OGCG: %[[VALUE:.+]] = load i32, ptr %[[RELOAD_PTR]], align 2
// OGCG: %[[CALL:.+]] = call i64 %{{.+}}(ptr noundef %{{.+}}, i32 %[[VALUE]])
