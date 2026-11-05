// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.noccl.ll
// RUN: FileCheck --input-file=%t.noccl.ll %s --check-prefix=NOCCL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// A call that needs an EH cleanup is a cir.try_call, and its ABI lowering goes
// through a throwaway cir.call built from the try_call's operands. That
// temporary used to be handed no uses of the result, so the lowering saw a
// result nobody wanted: it routed the indirect (sret) return through a fresh
// temporary and then erased the store that was meant to publish the value --
// or, with nothing left to erase, replaced the value with undef. Either way
// the real destination was never written.

struct Big {
  long a, b, c, d;
};

struct Key {
  long k;
  Key(long);
  ~Key();
};

Big produce(const Key &);
Key makeKey(long);

// 1. Forwarded straight out of the return.
Big retCall(long x) { return produce(makeKey(x)); }

// CIR-LABEL: define{{.*}} void @_Z7retCalll(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// CIR-NOT:     store %struct.Big

// NOCCL-LABEL: define{{.*}} void @_Z7retCalll(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// NOCCL-NOT:     store %struct.Big

// OGCG-LABEL: define{{.*}} void @_Z7retCalll(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// OGCG-NOT:     store %struct.Big

// 2. Bound to a local first. This is the shape that used to end up storing
//    undef over the caller's slot.
Big localFromCall(long x) {
  Big r = produce(makeKey(x));
  return r;
}

// CIR-LABEL: define{{.*}} void @_Z13localFromCalll(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// CIR-NOT:     store %struct.Big

// NOCCL-LABEL: define{{.*}} void @_Z13localFromCalll(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// NOCCL-NOT:     store %struct.Big

// OGCG-LABEL: define{{.*}} void @_Z13localFromCalll(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],
// OGCG-NOT:     store %struct.Big

// 3. Consumed by another call rather than returned: the result legitimately
//    lives in a local temporary, which must still be the sret destination.
void sink(const Big &);
void feedSink(long x) { sink(produce(makeKey(x))); }

// CIR-LABEL: define{{.*}} void @_Z8feedSinkl(
// CIR:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// CIR:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// CIR:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])

// NOCCL-LABEL: define{{.*}} void @_Z8feedSinkl(
// NOCCL:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// NOCCL:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// NOCCL:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])

// OGCG-LABEL: define{{.*}} void @_Z8feedSinkl(
// OGCG:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// OGCG:         invoke void @_Z7produceRK3Key(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// OGCG:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])
