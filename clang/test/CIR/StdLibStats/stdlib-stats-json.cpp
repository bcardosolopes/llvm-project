// Default (bare flag) and explicit =json both write the report to stderr.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fstd-lib-stats \
// RUN:   -emit-obj %s -o %t.o 2>&1 | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fstd-lib-stats=json \
// RUN:   -emit-obj %s -o %t.o 2>&1 | FileCheck %s

// An unknown mode is diagnosed by the driver-level option parser.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -fstd-lib-stats=bogus \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck %s -check-prefix=BADMODE

// Without the flag nothing is reported.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-obj %s -o %t.o 2>&1 \
// RUN:   | FileCheck %s -check-prefix=NOSTATS --allow-empty

namespace std {
struct string {
  unsigned size() const;
};
template <class T> struct vector {
  unsigned size() const;
  void push_back(const T &);
};
} // namespace std

extern "C" unsigned long strlen(const char *);

int notStd(int x);

int use(const std::string &s, std::vector<int> &v, const char *p) {
  v.push_back((int)s.size());
  return v.size() + (int)strlen(p) + notStd(1);
}

template <class T> unsigned tpl(std::vector<T> &v) { return v.size(); }

int main() {
  std::string s;
  std::vector<int> vi;
  std::vector<float> vf;
  return use(s, vi, "x") + (int)tpl(vi) + (int)tpl(vf);
}

// Report identity and the summary counters, including the two that used to be
// gathered into file-scope globals and never reported.
// CHECK: {"version":1,"kind":"stdlib_stats","tu":"{{.*}}stdlib-stats-json.cpp"
// CHECK-SAME: "summary":{"total":{{[0-9]+}},"std_calls":{{[0-9]+}},
// CHECK-SAME: "not_std":{{[0-9]+}},"indirect":{{[0-9]+}},"dependent":{{[0-9]+}},"unnamed":{{[0-9]+}},
// CHECK-SAME: "unevaluated":{{[0-9]+}},"pattern":{{[0-9]+}},"libs":{"c":1,"std":5},"namespaces":{

// A free C-stdlib call: no record, caller carries both names.
// CHECK-SAME: {"record":"","function":"strlen(const char *)","count":1,
// CHECK-SAME: "lib":"c","kind":"runtime","unevaluated":0,
// CHECK-SAME: "callers":[{"name":"use","mangled":"_Z3useRKSt6stringRSt6vectorIiEPKc","count":1}]

// A member call on a std type.
// CHECK-SAME: {"record":"string","function":"size()","count":1,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":0,
// CHECK-SAME: "callers":[{"name":"use","mangled":"_Z3useRKSt6stringRSt6vectorIiEPKc","count":1}]

// Each template instantiation is a distinct caller with its own linkage name,
// even though both share a source location.
// CHECK-SAME: {"record":"vector","function":"size()","count":3,
// CHECK-SAME: "lib":"std","kind":"runtime","unevaluated":0,
// CHECK-SAME: "callers":[{"name":"tpl","mangled":"_Z3tplIfEjRSt6vectorIT_E","count":1},
// CHECK-SAME: {"name":"tpl","mangled":"_Z3tplIiEjRSt6vectorIT_E","count":1},
// CHECK-SAME: {"name":"use","mangled":"_Z3useRKSt6stringRSt6vectorIiEPKc","count":1}]

// json mode keeps the per-call-site locations for human analysis.
// CHECK-SAME: "locations":[{"loc":"{{.*}}stdlib-stats-json.cpp:{{[0-9]+}}:{{[0-9]+}}","caller":"use"}

// BADMODE: error: invalid value 'bogus' in '-fstd-lib-stats=bogus'

// NOSTATS-NOT: stdlib_stats
