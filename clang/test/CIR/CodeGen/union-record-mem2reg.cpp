// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -fclangir-mem2reg \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM %s --input-file=%t.ll
// REQUIRES: x86-registered-target

// A record containing a union must not be promoted by CIR mem2reg.
// Promotion introduces whole-record `cir.load`/`cir.store` SSA values, which
// lower to LLVM `load`/`store` of the union-as-first-member type. LLVM
// load/store do not preserve padding bytes, but a union member's live data
// can sit in another member's padding (here the `bw` word lives in the
// padding of the first member's `{i32, ptr, ptr}` layout), so the round trip
// silently drops it. The slots must survive with byte-wise copies; LLVM's
// own passes still optimize the lowered code.

struct First {
  unsigned kind : 31;
  unsigned flag : 1;
  void *a;
  void *b;
};

struct Second {
  unsigned kind : 31;
  unsigned flag : 1;
  unsigned bw : 31;
  unsigned isUnsigned : 1;
  unsigned long val;
  void *t;
};

struct Record {
  union {
    struct First first;
    struct Second second;
  };
};

__attribute__((noinline)) void consume(struct Record x, unsigned wantBw,
                                       unsigned long wantVal);

int main(void) {
  struct Record arr[2] = {0};
  arr[0].second.kind = 4;
  arr[0].second.bw = 32;
  arr[0].second.val = 1;
  arr[1].second.kind = 4;
  arr[1].second.bw = 64;
  arr[1].second.val = 0x1234;
  unsigned wantBw[2] = {32, 64};
  unsigned long wantVal[2] = {1, 0x1234};
  for (unsigned i = 0; i < 2; ++i) {
    struct Record x = arr[i];
    consume(x, wantBw[i], wantVal[i]);
  }
  return 0;
}

__attribute__((noinline)) void consume(struct Record x, unsigned wantBw,
                                       unsigned long wantVal) {
  if (x.second.kind != 4 || x.second.bw != wantBw ||
      x.second.val != wantVal)
    __builtin_trap();
}

// LLVM-LABEL: @main
// The loop-carried record copies must survive CIR mem2reg as byte-wise
// memcpys covering the full 24 bytes; promoting them would introduce a
// lossy whole-record load/store through the first member's type.
// LLVM: call void @llvm.memcpy{{.*}}i64 24
// LLVM: call void @llvm.memcpy{{.*}}i64 24
// LLVM: call void @_Z7consume6Recordjm({{.*}}byval(%struct.Record)
