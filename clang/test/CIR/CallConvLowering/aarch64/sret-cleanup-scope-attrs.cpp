// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple aarch64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// A call whose argument is a temporary with a destructor runs inside a
// cir.cleanup.scope. Flattening that scope rewrites the throwing body call
// into a cir.try_call, which used to drop the call-site attributes. For an
// indirect call those attributes are the entire ABI contract: without
// cir.sret the return slot stops being the AAPCS64 indirect result (x8) and
// becomes an ordinary argument in x0, shifting every real argument by one.

struct Big {
  long a, b, c, d;
};

struct Key {
  long k;
  Key(long);
  ~Key();
};

struct Cache {
  virtual Big get(const Key &) = 0;
  virtual ~Cache();
};

Big viaVirtual(Cache *c, long x) { return c->get(Key(x)); }

// CIR-LABEL: define{{.*}} void @_Z10viaVirtualP5Cachel(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// OGCG-LABEL: define{{.*}} void @_Z10viaVirtualP5Cachel(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

using FnPtr = Big (*)(const Key &);

Big viaFnPtr(FnPtr fn, long x) { return fn(Key(x)); }

// CIR-LABEL: define{{.*}} void @_Z8viaFnPtrPF3BigRK3KeyEl(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// OGCG-LABEL: define{{.*}} void @_Z8viaFnPtrPF3BigRK3KeyEl(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

void sink(const Big &);

void feedSink(Cache *c, long x) { sink(c->get(Key(x))); }

// CIR-LABEL: define{{.*}} void @_Z8feedSinkP5Cachel(
// CIR:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// CIR:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])

// OGCG-LABEL: define{{.*}} void @_Z8feedSinkP5Cachel(
// OGCG:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// OGCG:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])
