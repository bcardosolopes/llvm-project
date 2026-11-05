// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-cir -o - %s | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fblocks -emit-llvm -disable-llvm-passes -o - %s | FileCheck %s --check-prefix=OGCG

// A __block variable captured by an outer block and re-captured by a block
// nested inside it. The outer block must forward the byref BASE address it
// holds straight into the inner block's capture slot -- it must NOT project to
// the .value field first. The value projection (forwarding -> value) happens
// lazily in the innermost consumer that actually reads the variable. Storing a
// pre-projected &value into the inner capture slot made the inner invoke
// re-apply the forwarding+value GEPs to an already-projected pointer, i.e. a
// wild load / SIGSEGV at -O0 (corruption at -O2/-O3).

void test(void) {
  __block int outer = 0;
  int (^b)(void) = ^{
    void (^inner)(void) = ^{ outer += 10; };
    inner();
    return outer;
  };
  b();
}

// The outer block's invoke re-captures `outer` for the inner block. It loads
// the byref BASE out of its own literal (slot 5) and stores that exact pointer
// into the inner literal's capture slot (slot 5), with no intervening
// forwarding/value projection.
// CIR-LABEL: cir.func{{.*}}@__test_block_invoke.invoke
// CIR: %[[OUTER_CAP:.*]] = cir.get_member %{{.*}}[5] {name = ""} : !cir.ptr<[[LITTY:!rec_anon_struct[0-9]*]]> -> !cir.ptr<!cir.ptr<!void>>
// CIR: %[[BASE_VOID:.*]] = cir.load align(8) %[[OUTER_CAP]]
// CIR: %[[BASE_REC:.*]] = cir.cast bitcast %[[BASE_VOID]] : !cir.ptr<!void> -> !cir.ptr<[[BYREF:!rec_anon_struct[0-9]*]]>
// CIR: %[[INNER_CAP:.*]] = cir.get_member %{{.*}}[5]
// CIR: %[[BASE_VOID2:.*]] = cir.cast bitcast %[[BASE_REC]] : !cir.ptr<[[BYREF]]> -> !cir.ptr<!void>
// CIR: cir.store align(8) %[[BASE_VOID2]], %[[INNER_CAP]]

// The inner block's invoke is where the byref value is actually read: base ->
// forwarding (field 1) -> value (field 4), i.e. the lazy projection.
// CIR-LABEL: cir.func{{.*}}@__test_block_invoke_2.invoke
// CIR: %[[ICAP:.*]] = cir.get_member %{{.*}}[5]
// CIR: %[[IBASE:.*]] = cir.load align(8) %[[ICAP]]
// CIR: cir.cast bitcast %[[IBASE]] : !cir.ptr<!void> -> !cir.ptr<[[IBYREF:!rec_anon_struct[0-9]*]]>
// CIR: %[[IFWD:.*]] = cir.get_member %{{.*}}[1] {name = ""} : !cir.ptr<[[IBYREF]]> -> !cir.ptr<!cir.ptr<!void>>

// LLVM-LABEL: define internal i32 @__test_block_invoke.invoke(ptr noundef %{{.*}})
// LLVM: %[[OUTER_CAP:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[BYREF_BASE:.*]] = load ptr, ptr %[[OUTER_CAP]]
// LLVM: %[[INNER_CAP:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// LLVM: store ptr %[[BYREF_BASE]], ptr %[[INNER_CAP]]

// LLVM-LABEL: define internal void @__test_block_invoke_2.invoke(ptr noundef %{{.*}})
// LLVM: %[[ICAP:.*]] = getelementptr inbounds nuw { ptr, i32, i32, ptr, ptr, ptr }, ptr %{{.*}}, i32 0, i32 5
// LLVM: %[[IBASE:.*]] = load ptr, ptr %[[ICAP]]
// LLVM: %[[IFWD:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[IBASE]], i32 0, i32 1
// LLVM: %[[IFWDP:.*]] = load ptr, ptr %[[IFWD]]
// LLVM: %[[IVAL:.*]] = getelementptr inbounds nuw { ptr, ptr, i32, i32, i32 }, ptr %[[IFWDP]], i32 0, i32 4

// OGCG-LABEL: define internal i32 @__test_block_invoke(ptr noundef %.block_descriptor)
// OGCG: %[[INNER_CAP:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, ptr }>, ptr %block, i32 0, i32 5
// OGCG: %[[OUTER_CAP:.*]] = getelementptr inbounds nuw <{ ptr, i32, i32, ptr, ptr, ptr }>, ptr %.block_descriptor, i32 0, i32 5
// OGCG: %[[BYREF_BASE:.*]] = load ptr, ptr %[[OUTER_CAP]]
// OGCG: store ptr %[[BYREF_BASE]], ptr %[[INNER_CAP]]
