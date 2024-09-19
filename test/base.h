extern "C" {
#include "ml_token.h"
}

#include <cstring>
#include <initializer_list>
#include <cppunit/extensions/HelperMacros.h>

namespace runml {

class BaseTextFixture : public CppUnit::TestFixture {
public:
    virtual void setUp() override;
    virtual void tearDown() override;

protected:
    void setMaxAllocSize(size_t size);
    void setMaxAllocCount(size_t count);
};

class RawString {
private:
    const char* pointer;

public:
    RawString() : RawString(nullptr) {}
    RawString(const char *s) : pointer(s ? s : "") {}
    RawString(const RawString &s) : pointer(s.pointer) {}

    int length() const { return std::strlen(pointer); }

    bool operator==(const char *rhs) const { return rhs && std::strcmp(pointer, rhs) == 0; }
    bool operator==(const RawString &rhs) const { return operator==(rhs.pointer); }
    bool operator<(const RawString &rhs) const { return std::strcmp(pointer, rhs.pointer) < 0; }

    operator const char*() const { return pointer; }

    RawString& operator=(const RawString& rhs) {
        pointer = rhs.pointer;
        return *this;
    }
};

class Tokenizer {
private:
    using RawStringLines = std::vector<RawString>;

private:
    struct {
        int offset = 0;
        int count = 0;
    } cursor;
    int index = 0;
    ml_token_ctx *ctx = nullptr;
    const RawStringLines lines;

private:
    static const ml_token_io_fns string_io_fns;

private:
    static int doReadString(void *opaque, char *buffer, int capacity);
    static void doCloseString(void *opaque) {}

public:
    Tokenizer(RawStringLines &&lines, const ml_token_ctx_init_args &args);
    Tokenizer(RawStringLines &&lines)
        : Tokenizer(std::move(lines), {4, 32}) {}
    Tokenizer(const char *s)
        : Tokenizer(RawStringLines{s}) {}
    Tokenizer(const char *s, const ml_token_ctx_init_args &args)
        : Tokenizer(RawStringLines{s}, args) {}
    ~Tokenizer();

    struct ml_token_ctx *cast() const { return ctx; }
    bool check(std::initializer_list<RawString> tokens);
    bool check(std::initializer_list<enum ml_token_type> types);

    template <typename Op>
    void iterate(Op op) {
        CPPUNIT_ASSERT(ctx);
        while (true) {
            ml_token_data data = {0};
            auto type = ml_token_iterate(ctx, &data);
            op(type, data);
            if (type == ML_TOKEN_TYPE_EOF)
                break;
        }
    }
};
}
