#ifndef BUFFER_MONITOR_HPP
#define BUFFER_MONITOR_HPP

#include "buffer_base.hpp"
#include <vector>
#include <pthread.h>

class BoundedBufferMonitor : public IBoundedBuffer {
public:
    explicit BoundedBufferMonitor(std::size_t capacity)
        : buffer(capacity), capacity(capacity),
          head(0), tail(0), count(0) {
        pthread_mutex_init(&mtx, nullptr);
        pthread_cond_init(&notFull, nullptr);
        pthread_cond_init(&notEmpty, nullptr);
    }

    ~BoundedBufferMonitor() override {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&notFull);
        pthread_cond_destroy(&notEmpty);
    }

    void put(const Item& item) override {
        pthread_mutex_lock(&mtx);

        while (count == capacity) {
            pthread_cond_wait(&notFull, &mtx);
        }

        buffer[tail] = item;
        tail = (tail + 1) % capacity;
        ++count;

        pthread_cond_signal(&notEmpty);
        pthread_mutex_unlock(&mtx);
    }

    Item get() override {
        pthread_mutex_lock(&mtx);

        while (count == 0) {
            pthread_cond_wait(&notEmpty, &mtx);
        }

        Item item = buffer[head];
        head = (head + 1) % capacity;
        --count;

        pthread_cond_signal(&notFull);
        pthread_mutex_unlock(&mtx);
        return item;
    }

private:
    std::vector<Item> buffer;
    std::size_t capacity;
    std::size_t head;
    std::size_t tail;
    std::size_t count;

    pthread_mutex_t mtx;
    pthread_cond_t notFull;
    pthread_cond_t notEmpty;
};

#endif // BUFFER_MONITOR_HPP
