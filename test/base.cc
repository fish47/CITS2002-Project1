extern "C" {
#include "ml_memory.h"
}

#include "base.h"

#include <memory>
#include <vector>
#include <unordered_map>

namespace runml {

class MemoryAllocator {
public:
    enum class LimitMode {
        kNone,
        kCount,
        kSize,
    };

private:
    LimitMode limit_mode;
    size_t limit_value;
    size_t invoke_count;
    size_t allocate_size;
    std::unordered_map<void*, std::shared_ptr<std::vector<uint8_t>>> memory_map;

private:
    bool check(size_t old_size, size_t new_size) {
        switch (limit_mode) {
            case LimitMode::kNone:
                return true;
            case LimitMode::kCount:
                if (invoke_count < limit_value) {
                    invoke_count++;
                    return true;
                }
                return false;
            case LimitMode::kSize:
                if (allocate_size - old_size + new_size > limit_value)
                    return false;
                allocate_size -= old_size;
                allocate_size += new_size;
                return true;
            default:
                return false;
        }
    }

public:
    void limit(LimitMode mode, size_t value) {
        limit_mode = mode;
        limit_value = value;
    }

    bool reset() {
        bool no_leak = memory_map.empty();
        memory_map.clear();
        invoke_count = 0;
        allocate_size = 0;
        return no_leak;
    }

    void *alloc(size_t size) {
        if (!check(0, size))
            return nullptr;

        auto mem = std::make_shared<std::vector<uint8_t>>(size);
        memory_map.insert({mem->data(), mem});
        return mem->data();
    }

    void *realloc(void *ptr, size_t size) {
        auto it = memory_map.find(ptr);
        if (it == memory_map.end())
            return alloc(size);

        if (!check(it->second->size(), size))
            return nullptr;

        // the base memory may change after resize
        auto mem = it->second;
        mem->resize(size);
        if (it->first != mem->data()) {
            memory_map.erase(it);
            memory_map.insert({mem->data(), mem});
        }
        return mem->data();
    }

    void free(void *ptr) {
        auto it = memory_map.find(ptr);
        if (it == memory_map.end()) {
            CPPUNIT_ASSERT(false);
            return;
        }
        memory_map.erase(it);

    }
};

static MemoryAllocator g_allocator;

void BaseTextFixture::setMaxAllocSize(size_t size) {
    CPPUNIT_ASSERT(g_allocator.reset());
    g_allocator.limit(MemoryAllocator::LimitMode::kSize, size);
}

void BaseTextFixture::setMaxAllocCount(size_t count) {
    CPPUNIT_ASSERT(g_allocator.reset());
    g_allocator.limit(MemoryAllocator::LimitMode::kCount, count);
}

void BaseTextFixture::setUp() {
    g_allocator.reset();
    g_allocator.limit(MemoryAllocator::LimitMode::kNone, 0);
}

void BaseTextFixture::tearDown() {
    CPPUNIT_ASSERT(g_allocator.reset());
}

}


void *ml_memory_malloc(size_t size) {
    return runml::g_allocator.alloc(size);
}

void *ml_memory_realloc(void *ptr, size_t size) {
    return runml::g_allocator.realloc(ptr, size);
}

void ml_memory_free(void *ptr) {
    runml::g_allocator.free(ptr);
}
