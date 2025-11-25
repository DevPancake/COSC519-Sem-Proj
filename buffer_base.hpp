#ifndef BUFFER_BASE_HPP
#define BUFFER_BASE_HPP

#include <cstddef>

struct Item {
    long id;
};

class IBoundedBuffer {
public:
    virtual ~IBoundedBuffer() = default;
    virtual void put(const Item& item) = 0;
    virtual Item get() = 0;
};

#endif // BUFFER_BASE_HPP
