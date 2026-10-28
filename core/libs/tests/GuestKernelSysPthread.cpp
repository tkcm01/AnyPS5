#include "prx/libkernel/KernelErrors.hpp"
#include "prx/libkernel_sys/include/Api.hpp"
#include "prx/libkernel/include/Api.hpp"
#include "prx/libkernel/Time/include/Time.hpp"
#include "SceTypes.hpp"
#include "prx/libkernel/Pthread/include/Atfork.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <thread>

static constexpr int SCOPE_PROCESS = 0;
static constexpr int SCOPE_SYSTEM = 2;

static void Require(bool value) { if (!value) std::abort(); }

static void APS5_VABI Prepare() {}
static void APS5_VABI Parent() {}
static void APS5_VABI Child() {}

static void TestScope() {
    PthreadAttr attr{};
    Require(scePthreadAttrInit(&attr) == 0);
    int scope = -1;
    Require(scePthreadAttrGetscope(&attr, &scope) == 0 && scope == SCOPE_SYSTEM);
    Require(scePthreadAttrSetscope(&attr, SCOPE_PROCESS) == 0);
    Require(scePthreadAttrGetscope(&attr, &scope) == 0 && scope == SCOPE_PROCESS);
    for (const int invalid : {1, 3, -1}) {
        Require(scePthreadAttrSetscope(&attr, invalid) == SCE_KERNEL_ERROR_EINVAL);
        scope = -1;
        Require(scePthreadAttrGetscope(&attr, &scope) == 0 && scope == SCOPE_PROCESS);
    }
    Require(scePthreadAttrSetscope(&attr, SCOPE_SYSTEM) == 0);
    Require(scePthreadAttrGetscope(&attr, &scope) == 0 && scope == SCOPE_SYSTEM);
    Require(scePthreadAttrGetscope(&attr, nullptr) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadAttrGetscope(nullptr, &scope) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadAttrSetscope(nullptr, SCOPE_PROCESS) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadAttrDestroy(&attr) == 0);
}

static void TestConcurrency() {
    Require(scePthreadSetconcurrency(3) == 0 && scePthreadGetconcurrency() == 3);
    Require(scePthreadSetconcurrency(-1) == SCE_KERNEL_ERROR_EINVAL && scePthreadGetconcurrency() == 3);
    Require(scePthreadSetconcurrency(0) == 0 && scePthreadGetconcurrency() == 0);
}

static void TestAtfork() {
    const auto before = PthreadAtforkHandlers_nid_no_patch().size();
    Require(scePthreadAtfork(Prepare, Parent, Child) == 0);
    Require(scePthreadAtfork(nullptr, nullptr, nullptr) == 0);
    const auto handlers = PthreadAtforkHandlers_nid_no_patch();
    Require(handlers.size() == before + 2);
    Require(handlers[before].prepare == Prepare && handlers[before].parent == Parent && handlers[before].child == Child);
    Require(handlers[before + 1].prepare == nullptr && handlers[before + 1].parent == nullptr && handlers[before + 1].child == nullptr);
}

struct TimedjoinGate {
    std::atomic<bool> release{false};
    std::atomic<bool> entered{false};
};

static void* APS5_VABI TimedjoinWorker(void* argument) {
    auto& gate = *static_cast<TimedjoinGate*>(argument);
    gate.entered.store(true, std::memory_order_release);
    while (!gate.release.load(std::memory_order_acquire)) std::this_thread::yield();
    return reinterpret_cast<void*>(std::uintptr_t{0x12345678});
}

static KernelTimespec After(int milliseconds) {
    KernelTimespec result{};
    Require(clock_gettime_nid_postfix(0, &result) == 0);
    result.tv_nsec += static_cast<std::int64_t>(milliseconds) * 1000000;
    result.tv_sec += result.tv_nsec / 1000000000;
    result.tv_nsec %= 1000000000;
    return result;
}

static void TestTimedjoin() {
    const KernelTimespec valid = After(1000);
    for (const KernelTimespec invalid : {KernelTimespec{-1, 0}, KernelTimespec{0, -1}, KernelTimespec{0, 1000000000}}) {
        Require(scePthreadTimedjoin(nullptr, nullptr, &invalid) == SCE_KERNEL_ERROR_EINVAL);
        Require(scePthreadTimedjoin(scePthreadSelf(), nullptr, &invalid) == SCE_KERNEL_ERROR_EINVAL);
    }
    Require(scePthreadTimedjoin(nullptr, nullptr, nullptr) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadTimedjoin(nullptr, nullptr, &valid) == SCE_KERNEL_ERROR_EINVAL);
    Require(scePthreadTimedjoin(scePthreadSelf(), nullptr, &valid) == SCE_KERNEL_ERROR_EDEADLK);

    TimedjoinGate gate;
    Pthread thread = nullptr;
    Require(scePthreadCreate(&thread, nullptr, TimedjoinWorker, &gate, nullptr) == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!gate.entered.load(std::memory_order_acquire)) {
        Require(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }

    void* returnValue = reinterpret_cast<void*>(std::uintptr_t{0x55});
    const KernelTimespec past{0, 0};
    Require(scePthreadTimedjoin(thread, &returnValue, &past) == SCE_KERNEL_ERROR_ETIMEDOUT);
    Require(returnValue == reinterpret_cast<void*>(std::uintptr_t{0x55}));

    gate.release.store(true, std::memory_order_release);
    const KernelTimespec future = After(3000);
    Require(scePthreadTimedjoin(thread, &returnValue, &future) == 0);
    Require(returnValue == reinterpret_cast<void*>(std::uintptr_t{0x12345678}));
    Require(scePthreadTimedjoin(thread, nullptr, &future) == SCE_KERNEL_ERROR_ESRCH);
}

int main() {
    TestScope();
    TestConcurrency();
    TestAtfork();
    TestTimedjoin();
    return 0;
}
