// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.cir.ll
// RUN: FileCheck --input-file=%t.cir.ll %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -fclangir -fclangir-mem2reg -emit-llvm %s -o %t.noccl.ll
// RUN: FileCheck --input-file=%t.noccl.ll %s --check-prefix=NOCCL
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fexceptions -fcxx-exceptions \
// RUN:   -emit-llvm %s -o %t.og.ll
// RUN: FileCheck --input-file=%t.og.ll %s --check-prefix=OGCG

// Call-conv lowering used to bucket only the try_calls that name a callee
// symbol, so a virtual or function-pointer call that also needs an EH cleanup
// was left un-lowered: the indirect return kept a by-value record result with
// no sret attribute. The backend then passed the result pointer in the first
// argument register and shifted every real argument by one, while the callee
// still read its arguments from their ABI positions.

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

// 1. Virtual call, result forwarded straight out of the return.
Big viaVirtual(Cache *c, long x) { return c->get(Key(x)); }

// CIR-LABEL: define{{.*}} void @_Z10viaVirtualP5Cachel(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// NOCCL-LABEL: define{{.*}} void @_Z10viaVirtualP5Cachel(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// OGCG-LABEL: define{{.*}} void @_Z10viaVirtualP5Cachel(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// 2. Same shape through a plain function pointer.
using FnPtr = Big (*)(const Key &);
Big viaFnPtr(FnPtr fn, long x) { return fn(Key(x)); }

// CIR-LABEL: define{{.*}} void @_Z8viaFnPtrPF3BigRK3KeyEl(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// NOCCL-LABEL: define{{.*}} void @_Z8viaFnPtrPF3BigRK3KeyEl(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// OGCG-LABEL: define{{.*}} void @_Z8viaFnPtrPF3BigRK3KeyEl(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// 3. Result bound to a local before being returned.
Big viaVirtualLocal(Cache *c, long x) {
  Big r = c->get(Key(x));
  return r;
}

// CIR-LABEL: define{{.*}} void @_Z15viaVirtualLocalP5Cachel(
// CIR-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// NOCCL-LABEL: define{{.*}} void @_Z15viaVirtualLocalP5Cachel(
// NOCCL-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// NOCCL:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// OGCG-LABEL: define{{.*}} void @_Z15viaVirtualLocalP5Cachel(
// OGCG-SAME:    sret(%struct.Big) align 8 [[RET:%[a-zA-Z0-9_.]+]],
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[RET]],

// 4. Result consumed by another call: it lives in a local temporary, which
//    must still be the sret destination.
void sink(const Big &);
void feedSink(Cache *c, long x) { sink(c->get(Key(x))); }

// CIR-LABEL: define{{.*}} void @_Z8feedSinkP5Cachel(
// CIR:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// CIR:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// CIR:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])

// NOCCL-LABEL: define{{.*}} void @_Z8feedSinkP5Cachel(
// NOCCL:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// NOCCL:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// NOCCL:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])

// OGCG-LABEL: define{{.*}} void @_Z8feedSinkP5Cachel(
// OGCG:         [[TMP:%[a-zA-Z0-9_.]+]] = alloca %struct.Big
// OGCG:         invoke void {{%[a-zA-Z0-9_.]+}}(ptr{{.*}} sret(%struct.Big) align 8 [[TMP]],
// OGCG:         invoke void @_Z4sinkRK3Big(ptr{{.*}} [[TMP]])
