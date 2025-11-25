#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>    // usleep
#include <pthread.h>

#include "buffer_base.hpp"
#include "buffer_monitor.hpp"
#include "buffer_semaphore.hpp"

// -------------------- Logging --------------------

struct LogEvent {
    long long timestamp_ns;
    int thread_index;      // 0..P-1 or 0..C-1 depending on role
    char role;             // 'P' or 'C'
    std::string event;     // "P_REQ", "P_DONE", "C_REQ", "C_DONE"
    long item_id;          // -1 if not applicable
};

std::mutex g_log_mutex;
std::vector<LogEvent> g_log;
std::chrono::steady_clock::time_point g_start_time;

void init_logging() {
    g_start_time = std::chrono::steady_clock::now();
}

void log_event(int thread_index, char role,
               const std::string &ev, long item_id) {
    auto now = std::chrono::steady_clock::now();
    long long ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                       now - g_start_time)
                       .count();

    std::lock_guard<std::mutex> lock(g_log_mutex);
    g_log.push_back(LogEvent{ns, thread_index, role, ev, item_id});
}

void write_log_to_csv(const std::string &filename) {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to open " << filename << " for writing\n";
        return;
    }
    out << "timestamp_ns,thread_index,role,event,item_id\n";
    for (const auto &e : g_log) {
        out << e.timestamp_ns << ","
            << e.thread_index << ","
            << e.role << ","
            << e.event << ","
            << e.item_id << "\n";
    }
    out.close();
    std::cout << "Wrote log to " << filename << "\n";
}

// -------------------- Shared State --------------------

std::atomic<long> next_id{0};

struct ThreadArgs {
    IBoundedBuffer *buffer;
    std::atomic<bool> *stopFlag;
    int threadIndex;
    bool isProducer;
};

// simple random sleep helper (0–9 ms)
void random_sleep() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 9);
    int ms = dist(rng);
    usleep(ms * 1000);
}

// -------------------- Thread Functions --------------------

void *producer_func(void *arg) {
    auto *args = static_cast<ThreadArgs *>(arg);
    while (!args->stopFlag->load()) {
        long id = next_id.fetch_add(1);
        Item it{id};

        // producer is ready to insert
        log_event(args->threadIndex, 'P', "P_REQ", it.id);

        args->buffer->put(it);

        // producer finished inserting
        log_event(args->threadIndex, 'P', "P_DONE", it.id);

        random_sleep();
    }
    return nullptr;
}

void *consumer_func(void *arg) {
    auto *args = static_cast<ThreadArgs *>(arg);
    while (!args->stopFlag->load()) {
        // consumer wants an item
        log_event(args->threadIndex, 'C', "C_REQ", -1);

        Item it = args->buffer->get();

        // consumer got an item
        log_event(args->threadIndex, 'C', "C_DONE", it.id);

        random_sleep();
    }
    return nullptr;
}

// -------------------- main --------------------

int main(int argc, char *argv[]) {
    const int P = 4;
    const int C = 4;
    const std::size_t N = 10;

    // mode selection: default monitor, or "monitor"/"semaphore" from argv[1]
    std::string mode = "monitor";
    if (argc >= 2) {
        mode = argv[1];  // e.g., ./pc monitor  or  ./pc semaphore
    }

    std::cout << "Mode: " << mode << "\n";

    IBoundedBuffer *buffer = nullptr;

    if (mode == "monitor") {
        buffer = new BoundedBufferMonitor(N);
    } else if (mode == "semaphore") {
        buffer = new BoundedBufferSemaphore(N);
    } else {
        std::cerr << "Unknown mode. Use: monitor | semaphore\n";
        return 1;
    }

    init_logging();

    std::atomic<bool> stop{false};

    std::vector<pthread_t> producers(P);
    std::vector<pthread_t> consumers(C);
    std::vector<ThreadArgs> pargs(P), cargs(C);

    // create producers
    for (int i = 0; i < P; ++i) {
        pargs[i].buffer = buffer;
        pargs[i].stopFlag = &stop;
        pargs[i].threadIndex = i;
        pargs[i].isProducer = true;
        pthread_create(&producers[i], nullptr, producer_func, &pargs[i]);
    }

    // create consumers
    for (int i = 0; i < C; ++i) {
        cargs[i].buffer = buffer;
        cargs[i].stopFlag = &stop;
        cargs[i].threadIndex = i;
        cargs[i].isProducer = false;
        pthread_create(&consumers[i], nullptr, consumer_func, &cargs[i]);
    }

    // run experiment for 5 seconds
    std::this_thread::sleep_for(std::chrono::seconds(5));
    stop.store(true);

    // (for now, don't join to avoid threads stuck waiting on cond vars)
    // Later you can add proper wakeups + joins for a clean shutdown.

    // write log to CSV (include mode in filename)
    std::string filename = "events_" + mode + ".csv";
    write_log_to_csv(filename);

    std::cout << "Done.\n";
    delete buffer;
    return 0;
}
