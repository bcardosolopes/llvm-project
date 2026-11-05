// Test for PR59221. Tests the compiler wouldn't misoptimize the final result.
//
// REQUIRES: x86-registered-target
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 %s -O1 -emit-llvm -o - | FileCheck %s
// RUN: %if cir-support %{ %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++20 %s -O1 -fclangir -emit-llvm -o - | FileCheck %s --check-prefix=CHECK-CIR %}

#include "Inputs/coroutine.h"

template <typename T> struct task {
	struct promise_type {
		T value{123};
		std::coroutine_handle<> caller{std::noop_coroutine()};
		
		struct final_awaiter: std::suspend_always {
			auto await_suspend(std::coroutine_handle<promise_type> me) const noexcept {
				return me.promise().caller;
			}
		};

		constexpr auto initial_suspend() const noexcept {
			return std::suspend_always();
		}
		constexpr auto final_suspend() const noexcept {
			return final_awaiter{};
		}
		auto unhandled_exception() noexcept {
			// ignore
		}
		constexpr void return_value(T v) noexcept {
			value = v;
		} 
		constexpr auto & get_return_object() noexcept {
			return *this;
		}
	};
	
	using coroutine_handle = std::coroutine_handle<promise_type>;
	
	promise_type & promise{nullptr};
	
	task(promise_type & p) noexcept: promise{p} { }
	
	~task() noexcept {
		coroutine_handle::from_promise(promise).destroy();
	}
	
	auto await_ready() noexcept {
        return false;
    }

    auto await_suspend(std::coroutine_handle<> caller) noexcept {
        promise.caller = caller;
        return coroutine_handle::from_promise(promise);
    }

    constexpr auto await_resume() const noexcept {
        return promise.value;
    }
	
	// non-coroutine access to result
	auto get() noexcept {
		const auto handle = coroutine_handle::from_promise(promise);
		
		if (!handle.done()) {
			handle.resume();
		}

        return promise.value;
	}
};


static inline auto a() noexcept -> task<int> {
	co_return 42;
}

static inline auto test() noexcept -> task<int> {
	co_return co_await a();
}

int foo() {
	return test().get();
}

// Checks that the store for the result value 42 is not misoptimized out.
// CHECK: define{{.*}}_Z3foov(
// CHECK: store i32 42, ptr %{{.*}}
// CHECK: }

// CHECK-CIR-LABEL: define internal fastcc void @_ZL1av.destroy(ptr noundef nonnull align 8 dereferenceable(48) %0)
// CHECK-CIR: {{(tail )?}}call void @_ZdlPvm(ptr noundef nonnull %0, i64 noundef 48)

// CHECK-CIR-LABEL: define internal fastcc void @_ZL4testv.destroy(ptr noundef nonnull align 8 dereferenceable(56) %0)
// CHECK-CIR: %index.addr = getelementptr inbounds nuw i8, ptr %0, i64 48
// CHECK-CIR: {{(tail )?}}call void @_ZdlPvm(ptr noundef nonnull %0, i64 noundef 56)
