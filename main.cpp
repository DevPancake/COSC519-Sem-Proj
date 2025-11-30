#include <iostream>
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <mutex>
#include <string>
#include <unistd.h>    
#include <pthread.h>

#include "buffer_base.hpp"
#include "buffer_monitor.hpp"
#include "buffer_semaphore.hpp"


struct LogEvent {
    long long timestamp_ns;
    int thread_index;      
    char role;            
    std::string event;    
    long item_id;       
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



std::atomic<long> next_id{0};
std::atomic<long> total_consumed{0}; 

struct ThreadArgs {
    IBoundedBuffer *buffer;
    std::atomic<bool> *stopFlag;
    int threadIndex;
    bool isProducer;
    long targetItems;
};


void random_sleep() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 9);
    int ms = dist(rng);
    usleep(ms * 1000);
}


void *producer_func(void *arg) {
    auto *args = static_cast<ThreadArgs *>(arg);
    while (!args->stopFlag->load()) {
        if (total_consumed.load() >= args->targetItems) {
            break;
        }

        long id = next_id.fetch_add(1);
        Item it{id};

        log_event(args->threadIndex, 'P', "P_REQ", it.id);

        args->buffer->put(it);

        log_event(args->threadIndex, 'P', "P_DONE", it.id);

        random_sleep();
    }
    return nullptr;
}

void *consumer_func(void *arg) {
    auto *args = static_cast<ThreadArgs *>(arg);
    while (!args->stopFlag->load()) {
        long current = total_consumed.load();
        if (current >= args->targetItems) {
            break;
        }

        log_event(args->threadIndex, 'C', "C_REQ", -1);

        Item it = args->buffer->get();

        log_event(args->threadIndex, 'C', "C_DONE", it.id);

        long newTotal = total_consumed.fetch_add(1) + 1;

        if (newTotal >= args->targetItems) {
            args->stopFlag->store(true);
        }

        random_sleep();
    }
    return nullptr;
}

int main(int argc, char *argv[]) {

    std::string mode = "monitor";
    int P = 2;
    int C = 2;
    long targetItems = 50;
    const std::size_t N = 10; 

    if (argc >= 2) {
        mode = argv[1];
    }
    if (argc >= 3) {
        P = std::stoi(argv[2]);
    }
    if (argc >= 4) {
        C = std::stoi(argv[3]);
    }
    if (argc >= 5) {
        targetItems = std::stol(argv[4]);
    }

    std::cout << "Mode: " << mode
              << " | P=" << P
              << " | C=" << C
              << " | targetItems=" << targetItems
              << "\n";

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
    next_id.store(0);
    total_consumed.store(0);

    std::atomic<bool> stop{false};

    std::vector<pthread_t> producers(P);
    std::vector<pthread_t> consumers(C);
    std::vector<ThreadArgs> pargs(P), cargs(C);

    for (int i = 0; i < P; ++i) {
        pargs[i].buffer = buffer;
        pargs[i].stopFlag = &stop;
        pargs[i].threadIndex = i;
        pargs[i].isProducer = true;
        pargs[i].targetItems = targetItems;
        pthread_create(&producers[i], nullptr, producer_func, &pargs[i]);
    }

    for (int i = 0; i < C; ++i) {
        cargs[i].buffer = buffer;
        cargs[i].stopFlag = &stop;
        cargs[i].threadIndex = i;
        cargs[i].isProducer = false;
        cargs[i].targetItems = targetItems;
        pthread_create(&consumers[i], nullptr, consumer_func, &cargs[i]);
    }

    while (total_consumed.load() < targetItems) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    stop.store(true);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));


    std::string filename = "events_" + mode +
                           "_P" + std::to_string(P) +
                           "_C" + std::to_string(C) +
                           "_N" + std::to_string(targetItems) +
                           ".csv";
    write_log_to_csv(filename);

    std::cout << "Done. Total consumed = " << total_consumed.load() << "\n";

    delete buffer;
    return 0;
}
