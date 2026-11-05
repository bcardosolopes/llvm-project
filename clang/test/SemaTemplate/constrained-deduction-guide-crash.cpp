// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// Test that non-template constructors with trailing requires clauses
// don't crash during deduction guide synthesis.

// Anonymous union with constrained defaulted move constructor.
template <typename T>
struct AnonymousUnionConstrainedDefaultedMove {
    union {
        T value;
    };
    AnonymousUnionConstrainedDefaultedMove(
        AnonymousUnionConstrainedDefaultedMove&& other)
    requires requires { other.value; } = default;
};

AnonymousUnionConstrainedDefaultedMove(int)
    -> AnonymousUnionConstrainedDefaultedMove<int>;

// Direct member with constrained constructor.
template <typename T>
struct DirectMemberConstrainedCtor {
    T value;
    DirectMemberConstrainedCtor(DirectMemberConstrainedCtor&& other)
    requires requires { other.value; } = default;
};

DirectMemberConstrainedCtor(int) -> DirectMemberConstrainedCtor<int>;

// Non-defaulted constrained constructor.
template <typename T>
struct NonDefaultedConstrainedCtor {
    T data;
    NonDefaultedConstrainedCtor(NonDefaultedConstrainedCtor&& other)
    requires requires { other.data; } : data(static_cast<T&&>(other.data)) {}
};

NonDefaultedConstrainedCtor(int) -> NonDefaultedConstrainedCtor<int>;

// CTAD triggering deduction guide synthesis.
template <typename T>
struct CTADTrigger {
    union {
        T value;
    };
    CTADTrigger(T v) : value(v) {}
    CTADTrigger(CTADTrigger&& other)
    requires requires { other.value; } = default;
};

void testCTAD() {
    CTADTrigger c(42);
}

// Ensure template constructors with requires clauses still work (CWG 2628).
template <typename T>
struct TemplateCtorWithRequiresStillWorks {
    T value;
    template <typename U>
    requires requires { static_cast<T>(U{}); }
    TemplateCtorWithRequiresStillWorks(U u) : value(static_cast<T>(u)) {}
};

TemplateCtorWithRequiresStillWorks(int)
    -> TemplateCtorWithRequiresStillWorks<int>;

void testTemplateCtor() {
    TemplateCtorWithRequiresStillWorks t(42);
}
