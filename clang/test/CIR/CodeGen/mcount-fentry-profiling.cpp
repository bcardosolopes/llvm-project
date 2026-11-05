// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -pg %s -o %t.pg.ll
// RUN: FileCheck --check-prefix=PG --input-file=%t.pg.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -pg %s -o %t.pg.ogcg.ll
// RUN: FileCheck --check-prefix=PG-OGCG --input-file=%t.pg.ogcg.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -pg -mfentry %s -o %t.fentry.ll
// RUN: FileCheck --check-prefix=FENTRY --input-file=%t.fentry.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -pg -mfentry %s -o %t.fentry.ogcg.ll
// RUN: FileCheck --check-prefix=FENTRY-OGCG --input-file=%t.fentry.ogcg.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -pg -mfentry -mnop-mcount %s -o %t.nop.ll
// RUN: FileCheck --check-prefix=NOP --input-file=%t.nop.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -pg -mfentry -mnop-mcount %s -o %t.nop.ogcg.ll
// RUN: FileCheck --check-prefix=NOP-OGCG --input-file=%t.nop.ogcg.ll %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm \
// RUN:   -pg -mfentry -mrecord-mcount %s -o %t.rec.ll
// RUN: FileCheck --check-prefix=REC --input-file=%t.rec.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm \
// RUN:   -pg -mfentry -mrecord-mcount %s -o %t.rec.ogcg.ll
// RUN: FileCheck --check-prefix=REC-OGCG --input-file=%t.rec.ogcg.ll %s

void fn() {}

// -pg: instrument-function-entry-inlined="mcount"
// PG: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// PG: attributes #[[ATTRS]] = {{{.*}}"instrument-function-entry-inlined"="mcount"{{.*}}}
// PG-OGCG: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// PG-OGCG: attributes #[[ATTRS]] = {{{.*}}"instrument-function-entry-inlined"="mcount"{{.*}}}

// -pg -mfentry: fentry-call="true"
// FENTRY: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// FENTRY: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}}
// FENTRY-OGCG: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// FENTRY-OGCG: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}}

// -pg -mfentry -mnop-mcount: fentry-call + mnop-mcount
// NOP: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// NOP: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}"mnop-mcount"{{.*}}}
// NOP-OGCG: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// NOP-OGCG: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}"mnop-mcount"{{.*}}}

// -pg -mfentry -mrecord-mcount: fentry-call + mrecord-mcount
// REC: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// REC: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}"mrecord-mcount"{{.*}}}
// REC-OGCG: define {{.*}} @_Z2fnv() #[[ATTRS:[0-9]+]]
// REC-OGCG: attributes #[[ATTRS]] = {{{.*}}"fentry-call"="true"{{.*}}"mrecord-mcount"{{.*}}}
