// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-obj %s -o %t.o
//
// Recursive union templates (like std::variant's VariadicUnion)
// previously crashed with infinite recursion in
// RecordType::getLargestMember → getTypeABIAlignment during layout.

template<typename... Ts> union VariadicUnion;
template<> union VariadicUnion<> {};
template<typename T, typename... Ts>
union VariadicUnion<T, Ts...> { T value; VariadicUnion<Ts...> rest; };

template<typename... Ts>
struct Variant { VariadicUnion<Ts...> storage; int index; };

int test() {
    Variant<int, double, char> v;
    v.storage.value = 42;
    v.index = 0;
    return v.storage.value;
}

// CIR: cir.func {{.*}}@_Z4testv
