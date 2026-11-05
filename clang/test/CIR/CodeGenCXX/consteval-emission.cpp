// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o /dev/null 2>&1
//
// Previously crashed: consteval function returning a reference triggered the
// "consteval function should never be emitted" assertion in
// CIRGenModule::getAddrOfFunction. Now correctly folds to the constant.

constexpr int i_const = 5;

consteval const int &retI() {
    return i_const;
}

const int &test() {
    return retI();
}
