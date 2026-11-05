// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-mlir=cir-abi %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Recursive record/function-pointer patterns should lower without
// placeholder function signatures in final record layout.

struct Node;
typedef int (*NodeCallback)(struct Node *);

struct Node {
  NodeCallback cb;
};

// CIR: !rec_Node = !cir.record<struct "Node" {!cir.ptr<!cir.func<(!cir.ptr<!cir.record<struct "Node">>) -> !s32i>>}
// CIR-NOT: !cir.ptr<!cir.func<()>>
// LLVM: %struct.Node = type { ptr }

int invoke(struct Node *n) {
  return n->cb(n);
}

// CIR: cir.func{{.*}} @invoke(%arg0: !cir.ptr<!rec_Node> {{.*}}) -> !s32i
// CIR: [[NODE:%.*]] = cir.load{{.*}} : !cir.ptr<!cir.ptr<!rec_Node>>, !cir.ptr<!rec_Node>
// CIR: [[CB_ADDR:%.*]] = cir.get_member [[NODE]][0] {name = "cb"} : !cir.ptr<!rec_Node> -> !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Node>) -> !s32i>>>
// CIR: [[CB:%.*]] = cir.load{{.*}} [[CB_ADDR]] : !cir.ptr<!cir.ptr<!cir.func<(!cir.ptr<!rec_Node>) -> !s32i>>>, !cir.ptr<!cir.func<(!cir.ptr<!rec_Node>) -> !s32i>>
// CIR: [[NODE2:%.*]] = cir.load{{.*}} : !cir.ptr<!cir.ptr<!rec_Node>>, !cir.ptr<!rec_Node>
// CIR: [[RET:%.*]] = cir.call [[CB]]([[NODE2]]) {{.*}} : (!cir.ptr<!cir.func<(!cir.ptr<!rec_Node>) -> !s32i>>, !cir.ptr<!rec_Node>) -> !s32i
// CIR: cir.return {{%.*}} : !s32i

// LLVM: define{{.*}} i32 @invoke(ptr{{.*}} %[[N:[0-9]+]])
// LLVM: [[NODE:%.*]] = load ptr, ptr {{%.*}}
// LLVM: [[CB_ADDR:%.*]] = getelementptr inbounds{{.*}} %struct.Node, ptr [[NODE]], i32 0, i32 0
// LLVM: [[CB:%.*]] = load ptr, ptr [[CB_ADDR]]
// LLVM: [[NODE2:%.*]] = load ptr, ptr {{%.*}}
// LLVM: [[RET:%.*]] = call i32 [[CB]](ptr{{.*}} [[NODE2]])
// LLVM: ret i32 {{%.*}}
