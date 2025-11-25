#ifndef BUFFER_SEMAPHORE_HPP
#define BUFFER_SEMAPHORE_HPP

#include "buffer_base.hpp"
#include <vector>
#include <pthread.h>
#include <semaphore.h>

class BoundedBufferSemaphore : public IBoundedBuffer {
public:
    explicit BoundedBufferSemaphore(std::size_t capacity)
        : buffer(capacity), capacity(capacity),
          head(0), tail(0), count(0) {
        pthread_mutex_init(&mtx, nullptr);
        sem_init(&empty, 0, capacity);
        sem_init(&full, 0, 0);
    }

    ~BoundedBufferSemaphore() override {
        pthread_mutex_destroy(&mtx);
        sem_destroy(&empty);
        sem_destroy(&full);
    }

    void put(const Item& item) override {
        sem_wait(&empty);
        pthread_mutex_lock(&mtx);

        buffer[tail] = item;
        tail = (tail + 1) % capacity;
        ++count;

        pthread_mutex_unlock(&mtx);
        sem_post(&full);
    }

    Item get() override {
        sem_wait(&full);
        pthread_mutex_lock(&mtx);

        Item item = buffer[head];
        head = (head + 1) % capacity;
        --count;

        pthread_mutex_unlock(&mtx);
        sem_post(&empty);
        return item;
    }

private:
    std::vector<Item> buffer;
    std::size_t capacity;
    std::size_t head;
    std::size_t tail;
    std::size_t count;

    pthread_mutex_t mtx;
    sem_t empty;
    sem_t full;
};

#endif
