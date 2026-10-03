#include "worker.h"
#include <vector>

void Worker::start(size_t stack) {
    LightLock_Init(&lock_);
    LightEvent_Init(&wake_, RESET_ONESHOT);
    quit_ = false;
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    prio = prio + 2 > 0x3f ? 0x3f : prio + 2;   // below the music stream thread, which sits between main and the workers
    thread_ = threadCreate(entry, this, stack, prio, -2, false);
}

bool Worker::stop(bool drain, u64 patienceNs) {
    if (!thread_) return true;
    drain_ = drain;
    quit_ = true;
    LightEvent_Signal(&wake_);
    bool ended = R_SUCCEEDED(threadJoin(thread_, patienceNs)) && !running_;
    if (ended) threadFree(thread_);
    thread_ = nullptr;
    return ended;
}

void Worker::post(std::function<void()> fn, bool front) {
    LightLock_Lock(&lock_);
    if (front) queue_.push_front(std::move(fn)); else queue_.push_back(std::move(fn));
    LightLock_Unlock(&lock_);
    LightEvent_Signal(&wake_);
}

void Worker::postOp(std::function<void()> fn) {
    LightLock_Lock(&lock_);
    ops_.push_back(std::move(fn));
    LightLock_Unlock(&lock_);
    LightEvent_Signal(&wake_);
}

void Worker::clear() {
    LightLock_Lock(&lock_);
    queue_.clear(); ops_.clear();
    LightLock_Unlock(&lock_);
}

void Worker::state(int& ops, int& queued, int& runningMs) {
    LightLock_Lock(&lock_);
    ops = (int)ops_.size(); queued = (int)queue_.size();
    runningMs = running_ ? (int)((svcGetSystemTick() - startedTick_) * 1000 / SYSCLOCK_ARM11) : -1;
    LightLock_Unlock(&lock_);
}

void Worker::entry(void* arg) { ((Worker*)arg)->run(); }

void Worker::run() {
    for (;;) {
        std::function<void()> fn;
        LightLock_Lock(&lock_);
        if (!ops_.empty() && (!quit_ || drain_)) { fn = std::move(ops_.front()); ops_.pop_front(); running_ = true; }
        else if (!queue_.empty() && (!quit_ || drain_)) { fn = std::move(queue_.front()); queue_.pop_front(); running_ = true; }
        LightLock_Unlock(&lock_);
        if (!fn) { if (quit_) break; LightEvent_Wait(&wake_); continue; }
        startedTick_ = svcGetSystemTick();
        fn();
        LightLock_Lock(&lock_);
        running_ = false;
        LightLock_Unlock(&lock_);
    }
}

namespace worker {

Worker disk, net;

namespace {
LightLock g_mainLock;
std::vector<std::function<void()>> g_main;
}

void init() {
    LightLock_Init(&g_mainLock);
    disk.start(96 * 1024);
    net.start(128 * 1024);
}

bool fini() {
    // Network work is dropped. Disk work is finished: it may be a write the user asked for a moment ago.
    net.clear();
    bool netEnded = net.stop(false, 1500ull * 1000 * 1000);    // a request stuck in a name lookup or a connect is not waited for
    disk.stop(true);
    pump();    // results that arrived while stopping (they may record that a restart is needed)
    return netEnded;
}

void toMain(std::function<void()> fn) {
    LightLock_Lock(&g_mainLock);
    g_main.push_back(std::move(fn));
    LightLock_Unlock(&g_mainLock);
}

void pump() {
    std::vector<std::function<void()>> run;
    LightLock_Lock(&g_mainLock);
    run.swap(g_main);
    LightLock_Unlock(&g_mainLock);
    for (auto& fn : run) fn();
}

}  // namespace worker
