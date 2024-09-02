extern "C" {
#include "ml_token.h"
}

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

class Tokenizer {
private:
    const char* const text ;
    const int count;
    int index = 0;
    ml_token_ctx *ctx = nullptr;

private:
    static const ml_token_io_fns string_io_fns;

private:
    static int doReadString(void *opaque, char *buffer, int capacity);
    static void doCloseString(void *opaque) {}

public:
    Tokenizer(const char *s, int read_capacity = 4, int token_capacity = 32);
    ~Tokenizer();
    struct ml_token_ctx *cast() const { return ctx; }
    bool check(std::initializer_list<const char*> tokens);
    bool check(std::initializer_list<enum ml_token_type> types);

    template <typename Op>
    void iterate(Op op) {
        CPPUNIT_ASSERT(ctx);
        while (true) {
            const char *token = nullptr;
            ml_token_data data = {0};
            auto type = ml_token_iterate(ctx, &data);
            op(type, data);
            if (type == ML_TOKEN_TYPE_EOF)
                break;
        }
    }
};
}
