// Anonymous records inside a recursive component of the record graph.
//
// Record digests are computed per strongly connected component. Two things
// about that have to hold for anonymous records, which are known by digest
// alone -- their names are per-TU counters, not identity.
//
// Each member needs a digest of its own. Handing every member the component's
// digest made two different anonymous unions in one component look the same:
// they were given one canonical name, and the second silently took the first
// one's layout. Here Opt<A>'s storage union came out holding a B. The same
// collision in a real library surfaced as "member type mismatch" instead, in
// libc++'s std::optional, because nothing but the merge noticed.
//
// And a component's digest must not depend on the counters either. Members
// were ordered by name before hashing, and an anonymous name is the counter,
// so the same component hashed differently in two TUs that happened to number
// their unions the other way round -- and every record in it was renamed as if
// the TUs disagreed about it.
//
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/one.cpp -o %t/one.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/other.cpp -o %t/other.cir

// The precondition for the second half: the two TUs really do number the two
// unions in opposite orders.
// RUN: FileCheck --input-file=%t/one.cir %s --check-prefix=ONE
// ONE-DAG: union "anon.0" {!u8i, !{{(rec_A|cir.record<struct "A")}}
// ONE-DAG: union "anon.1" {!u8i, !{{(rec_B|cir.record<struct "B")}}
// RUN: FileCheck --input-file=%t/other.cir %s --check-prefix=OTHER
// OTHER-DAG: union "anon.0" {!u8i, !{{(rec_B|cir.record<struct "B")}}
// OTHER-DAG: union "anon.1" {!u8i, !{{(rec_A|cir.record<struct "A")}}

// One TU on its own: two unions in, the same two out.
// RUN: cir-link %t/one.cir -o %t/single.cir
// RUN: FileCheck --input-file=%t/single.cir %s --check-prefix=UNIONS

// Both TUs: still exactly those two unions, and nothing renamed -- the TUs
// agree about every record, whatever their counters say.
// RUN: cir-link %t/one.cir %t/other.cir --warn-odr-divergence \
// RUN:     -o %t/both.cir 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: FileCheck --input-file=%t/both.cir %s --check-prefix=UNIONS
// STATS: 0 records renamed

// UNIONS-DAG: union "anon.m{{[0-9]+}}" {!u8i, !rec_A}
// UNIONS-DAG: union "anon.m{{[0-9]+}}" {!u8i, !rec_B}
// UNIONS-NOT: union "anon{{[^"]*}}" {!u8i, !rec_{{[AB]}}2E

// The other side of it: two anonymous records with the same body are the same
// type, and every input has to say so. Giving the second one a name of its own
// instead made each input spell Size's body with a different union, and the
// second input's Size was rejected as not matching the first's.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/same.cpp -o %t/same1.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/same.cpp -o %t/same2.cir
// RUN: FileCheck --input-file=%t/same1.cir %s --check-prefix=SAME-IN
// SAME-IN-DAG: union "anon.0" {!s32i, !s32i}
// SAME-IN-DAG: union "anon.1" {!s32i, !s32i}
// RUN: cir-link %t/same1.cir %t/same2.cir --warn-odr-divergence \
// RUN:     -o %t/same.cir 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: FileCheck --input-file=%t/same.cir %s --check-prefix=SAME
// SAME: [[U:!rec_anon[^ ]*]] = !cir.record<union "anon.m{{[0-9]+}}" {!s32i, !s32i}>
// SAME-NOT: union "anon
// SAME: !cir.record<struct "Size" {[[U]], [[U]]}>

// And a member has to be told apart from a sibling it differs from only in
// which anonymous record it holds. Inside a component a reference is cut to a
// bare name, and an anonymous name normalizes to "anon", so both unions below
// read as union {char, "anon"}: they were given one name, and H1 came out
// holding H2's payload -- pointer and int the other way round -- on a single
// TU, exit 0, with nothing said.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/sibling.cpp -o %t/sibling1.cir
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/sibling-other.cpp -o %t/sibling2.cir

// The precondition for the counters: the two TUs put opposite payloads in the
// same numbered slot, so telling the siblings apart must not make the TUs
// disagree about a record.
// RUN: FileCheck --input-file=%t/sibling1.cir %s --check-prefix=SIB-ONE
// SIB-ONE: = !cir.record<struct "anon.1" {!cir.ptr
// RUN: FileCheck --input-file=%t/sibling2.cir %s --check-prefix=SIB-OTHER
// SIB-OTHER: = !cir.record<struct "anon.1" {!s32i

// One TU on its own, which is the half that says nothing: four anonymous
// records in, four out.
// RUN: cir-link %t/sibling1.cir -o %t/sibling-single.cir
// RUN: FileCheck --input-file=%t/sibling-single.cir %s --check-prefix=SIB
// Both TUs, and nothing renamed.
// RUN: cir-link %t/sibling1.cir %t/sibling2.cir --warn-odr-divergence \
// RUN:     -o %t/sibling.cir 2>&1 | FileCheck %s --check-prefix=STATS
// RUN: FileCheck --input-file=%t/sibling.cir %s --check-prefix=SIB

// Both payload layouts survive, under two unions rather than one.
// SIB-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!cir.ptr<{{.+}}>, !s32i}>
// SIB-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!s32i, !cir.ptr<{{.+}}>}>
// SIB-DAG: = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, !rec_anon{{[0-9A-Za-z]*}}}>
// SIB-DAG: = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, !rec_anon{{[0-9A-Za-z]*}}}>

// And each union is still under the record that declared it. Ordered, because
// an alias is defined before it is used and `Node` lists `h1` first, so H1's
// union is the first of the two.
// RUN: FileCheck --input-file=%t/sibling.cir %s --check-prefix=SIB-WIRE
// SIB-WIRE: [[U1:!rec_anon[^ ]*]] = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, {{.+}}}>
// SIB-WIRE: [[U2:!rec_anon[^ ]*]] = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, {{.+}}}>
// SIB-WIRE: !rec_H1 = !cir.record<struct "H1" {[[U1]]}>
// SIB-WIRE: !rec_H2 = !cir.record<struct "H2" {[[U2]]}>

// One round of re-keying is not enough, which is why this is a loop. Nest the
// payload one deeper: the two innermost structs differ, but the structs
// holding them read alike until the innermost keys are in, and the unions
// holding *those* read alike for a round longer. Capped at a single round,
// six anonymous records came out as three and N1 took N2's payload.
// RUN: %clang --target=aarch64-unknown-linux-android -fclangir -Xclang -emit-cir \
// RUN:     -S %t/sibling-nested.cpp -o %t/nested.cir
// RUN: cir-link %t/nested.cir -o %t/nested-out.cir
// RUN: FileCheck --input-file=%t/nested-out.cir %s --check-prefix=NEST
// NEST-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!cir.ptr<{{.+}}>, !s32i}>
// NEST-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!s32i, !cir.ptr<{{.+}}>}>
// NEST-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!rec_anon{{[0-9A-Za-z]*}}}>
// NEST-DAG: = !cir.record<struct "anon.m{{[0-9]+}}" {!rec_anon{{[0-9A-Za-z]*}}}>
// NEST-DAG: = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, !rec_anon{{[0-9A-Za-z]*}}}>
// NEST-DAG: = !cir.record<union "anon.m{{[0-9]+}}" {!u8i, !rec_anon{{[0-9A-Za-z]*}}}>

//--- one.cpp
struct Node;
struct A { Node *n; };
struct B { Node *n; int x; };
// The shape of libc++'s optional storage: an anonymous union holding the
// payload, which here points back at the record that holds it, putting both
// unions in Node's component.
template <class T> struct Opt {
  union { char none; T val; };
  bool engaged;
  Opt() : none(0), engaged(false) {}
};
struct Node {
  Opt<A> a;
  Opt<B> b;
};
Node g1;

//--- other.cpp
struct Node;
struct A { Node *n; };
struct B { Node *n; int x; };
template <class T> struct Opt {
  union { char none; T val; };
  bool engaged;
  Opt() : none(0), engaged(false) {}
};
// Instantiated first, so this TU numbers the two unions the other way round.
Opt<B> ob;
struct Node {
  Opt<A> a;
  Opt<B> b;
};
Node g2;

//--- same.cpp
struct Size {
  union { int width; int w; };
  union { int height; int h; };
};
Size g;

//--- sibling.cpp
struct Node;
struct H1 { union { char c; struct { Node *n; int x; } s; } u; };
struct H2 { union { char c; struct { int y; Node *m; } s; } u; };
struct Node { H1 h1; H2 h2; };
Node g3;

//--- sibling-other.cpp
struct Node;
struct H1 { union { char c; struct { Node *n; int x; } s; } u; };
struct H2 { union { char c; struct { int y; Node *m; } s; } u; };
// Complete before Node is, so this TU numbers H2's union first.
H2 first;
struct Node { H1 h1; H2 h2; };
Node g4;

//--- sibling-nested.cpp
struct Node;
struct N1 { union { char c; struct { struct { Node *n; int x; } t; } s; } u; };
struct N2 { union { char c; struct { struct { int y; Node *m; } t; } s; } u; };
struct Node { N1 n1; N2 n2; };
Node g5;
