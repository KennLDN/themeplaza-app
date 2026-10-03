// Background work. Each Worker owns one thread at a lower priority than the main thread, so it only
// gets the time the main loop leaves over and can never make it drop a frame by itself. Results come
// back through worker::toMain, which runs a callback on the main thread at its next update.
#pragma once
#include <3ds.h>
#include <deque>
#include <functional>

class Worker {
public:
    // stack: thread stack size in bytes.
    void start(size_t stack);
    // drain = true: run what is still queued first. Otherwise only the running task is waited for.
    // patienceNs: how long to wait for the thread; if it does not finish in time it is left behind.
    // Returns false if the thread was left behind still running.
    bool stop(bool drain = false, u64 patienceNs = U64_MAX);
    // front = true puts the task ahead of the ones already waiting (what the user is looking at now).
    void post(std::function<void()> fn, bool front = false);
    // An operation the user asked for (install, remove, delete). These run before everything posted with
    // post(), and in the order they were asked: a "remove" followed by an "install" must end installed.
    void postOp(std::function<void()> fn);
    void clear();                                  // drops the tasks that have not started
    // For the development log: how much is waiting, and for how long the task now running has run (ms; -1 if none).
    void state(int& ops, int& queued, int& runningMs);

private:
    static void entry(void* arg);
    void run();
    Thread thread_ = nullptr;
    LightLock lock_;
    LightEvent wake_;
    std::deque<std::function<void()>> queue_, ops_;
    volatile bool quit_ = false, running_ = false, drain_ = false;
    volatile u64 startedTick_ = 0;
};

namespace worker {

extern Worker disk;    // SD card: scanning, unpacking, decoding, installing
extern Worker net;     // Theme Plaza requests and downloads

void init();
bool fini();                             // false if the network thread had to be left behind, still inside a request
void toMain(std::function<void()> fn);   // from any thread
void pump();                             // main thread, once per frame: runs the queued callbacks

}  // namespace worker
