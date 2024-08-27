extern "C" {
#include "ml_token.h"
}

#include <cstring>
#include <cstdio>
#include <algorithm>

#include <cppunit/extensions/HelperMacros.h>

namespace runml {

class Tokenizer {
private:
    ml_token_ctx *ctx = nullptr;
    const char *text = nullptr;
    int index = 0;
    int count = 0;

private:
    static int doReadString(void *opaque, char *buffer, int capacity);
    static void doCloseString(void *opaque) {}

private:
    static const ml_token_io_fns string_io_fns;

public:
    Tokenizer(const char *s, int token_capacity = 64) {
        text = s;
        count = std::strlen(s);
        ml_token_ctx_init_fns(&ctx, &string_io_fns, this, 32, token_capacity);
    }

    ~Tokenizer() {
        ml_token_ctx_uninit(&ctx);
    }

    struct ml_token_ctx *cast() const {
        return ctx;
    }

    bool check(std::initializer_list<std::string> tokens) {
        auto it = tokens.begin();
        while (true) {
            const char *word = nullptr;
            auto type = ml_token_iterate(ctx, &word, nullptr);
            if (type == ML_TOKEN_TYPE_EOF)
                break;

            if (it == tokens.end() || *it != word)
                return false;

            ++it;
        }
        return it == tokens.end();
    }
};

int Tokenizer::doReadString(void *opaque, char *buffer, int capacity) {
    auto ctx = reinterpret_cast<Tokenizer*>(opaque);
    int count = std::min(ctx->count - ctx->index, capacity);
    if (count)
        std::memcpy(buffer, ctx->text + ctx->index, count);
    ctx->index += count;
    return count;
}

const ml_token_io_fns Tokenizer::string_io_fns = {
    doReadString,
    doCloseString,
};

class TestTokenBase : public CppUnit::TestFixture {

    CPPUNIT_TEST_SUITE(TestTokenBase);
    CPPUNIT_TEST(testStopIterate);
    CPPUNIT_TEST_SUITE_END();

public:
    void testStopIterate() {
        Tokenizer t("abc");
        ml_token_iterate(t.cast(), nullptr, nullptr);
        for (int i = 0; i < 10; i++)
            CPPUNIT_ASSERT_EQUAL(ML_TOKEN_TYPE_EOF, ml_token_iterate(t.cast(), nullptr, nullptr));
    }
};

class TestTokenAnalyze : public CppUnit::TestFixture {

    CPPUNIT_TEST_SUITE(TestTokenAnalyze);
    CPPUNIT_TEST(testLineTerminator);
    CPPUNIT_TEST_SUITE_END();

public:
    void testLineTerminator() {
        CPPUNIT_ASSERT(Tokenizer("\r\r\r").check({"\r", "\r", "\r"}));
        CPPUNIT_ASSERT(Tokenizer("\n\n\n").check({"\n", "\n", "\n"}));
        CPPUNIT_ASSERT(Tokenizer("\r\n").check({"\r\n"}));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenBase);
CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenAnalyze);

}
