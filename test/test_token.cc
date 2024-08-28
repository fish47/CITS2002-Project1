extern "C" {
#include "ml_token.h"
}

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <initializer_list>

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

private:
    template <typename F>
    bool doCheck(F func) {
        while (true) {
            const char *token = nullptr;
            auto type = ml_token_iterate(ctx, &token, nullptr);
            if (!func(type, token))
                return false;
            if (type == ML_TOKEN_TYPE_EOF)
                break;
        }
        return true;
    }

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
        return doCheck([&it, &tokens](enum ml_token_type type, const char *token) {
            return it == tokens.end() ? (type == ML_TOKEN_TYPE_EOF) : (*it++ == token);
        });
    }

    bool check(std::initializer_list<enum ml_token_type> types) {
        auto it = types.begin();
        return doCheck([&it, &types](enum ml_token_type type, const char *token) {
            return it == types.end() ? (type == ML_TOKEN_TYPE_EOF) : (*it++ == type);
        });
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
    CPPUNIT_TEST(testTypes);
    CPPUNIT_TEST(testLineTerminator);
    CPPUNIT_TEST(testMergedSpace);
    CPPUNIT_TEST(testSpecialCharacter);
    CPPUNIT_TEST(testComment);
    CPPUNIT_TEST(testNumber);
    CPPUNIT_TEST(testIdentifier);
    CPPUNIT_TEST_SUITE_END();

private:
    static bool isInvalidToken(const char *str) {
        return Tokenizer(str).check({ML_TOKEN_TYPE_ERROR});
    }

public:
    void testTypes() {
        CPPUNIT_ASSERT(Tokenizer(" \t+-*/()1.1#\n").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_TAB,
            ML_TOKEN_TYPE_PLUS,
            ML_TOKEN_TYPE_MINUS,
            ML_TOKEN_TYPE_MULTIPLY,
            ML_TOKEN_TYPE_DIVIDE,
            ML_TOKEN_TYPE_PARENTHESIS_L,
            ML_TOKEN_TYPE_PARENTHESIS_R,
            ML_TOKEN_TYPE_NUMBER,
            ML_TOKEN_TYPE_COMMENT,
            ML_TOKEN_TYPE_LINE_TERMINATOR,
        }));

        CPPUNIT_ASSERT(Tokenizer("abc print return function").check({
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_PRINT,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_RETURN,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_FUNCTION,
        }));
    }

    void testLineTerminator() {
        CPPUNIT_ASSERT(Tokenizer("\r\r\r").check({"\r", "\r", "\r"}));
        CPPUNIT_ASSERT(Tokenizer("\n\n\n").check({"\n", "\n", "\n"}));
        CPPUNIT_ASSERT(Tokenizer("\r\n").check({"\r\n"}));
    }

    void testMergedSpace() {
        CPPUNIT_ASSERT(Tokenizer("  \n    \n ").check({" ", "\n", " ", "\n", " "}));
        CPPUNIT_ASSERT(Tokenizer(" \n\n\r\n  \n").check({" ", "\n", "\n", "\r\n", " ", "\n"}));
    }

    void testSpecialCharacter() {
        CPPUNIT_ASSERT(Tokenizer("\t+-*/()").check({"\t", "+", "-", "*", "/", "(", ")"}));
        CPPUNIT_ASSERT(Tokenizer("   +   -  ").check({" ", "+", " ", "-", " "}));
    }

    void testComment() {
        CPPUNIT_ASSERT(Tokenizer("#  + -").check({"#"}));
        CPPUNIT_ASSERT(Tokenizer("# :-o ##\r\n#").check({"#", "\r\n", "#"}));
        CPPUNIT_ASSERT(Tokenizer("+-*  # :-)\n/").check({"+", "-", "*", " ", "#", "\n", "/"}));
    }

    void testNumber() {
        CPPUNIT_ASSERT(Tokenizer("1234567890").check({"1234567890"}));
        CPPUNIT_ASSERT(Tokenizer("  1234").check({" ", "1234"}));
        CPPUNIT_ASSERT(Tokenizer("  1234.5\n").check({" ", "1234.5", "\n"}));
        CPPUNIT_ASSERT(Tokenizer("1234.").check({"1234."}));
        CPPUNIT_ASSERT(Tokenizer(".1").check({".1"}));
        CPPUNIT_ASSERT(Tokenizer("0.1").check({"0.1"}));

        CPPUNIT_ASSERT(Tokenizer(" + 1a").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_PLUS,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ERROR,
        }));
        CPPUNIT_ASSERT(Tokenizer(" \n . haha").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_LINE_TERMINATOR,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ERROR,
        }));
        CPPUNIT_ASSERT(Tokenizer("  .1.#.").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ERROR,
        }));
        CPPUNIT_ASSERT(Tokenizer("  .1_ haha\n123").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ERROR,
            ML_TOKEN_TYPE_LINE_TERMINATOR,
            ML_TOKEN_TYPE_NUMBER,
        }));
    }

    void testIdentifier() {
        CPPUNIT_ASSERT(Tokenizer("print(").check({"print", "("}));
        CPPUNIT_ASSERT(Tokenizer("  abc#").check({" ", "abc", "#"}));
        CPPUNIT_ASSERT(Tokenizer("+abc  (fg/bg").check({"+", "abc", " ", "(", "fg", "/", "bg"}));;
        CPPUNIT_ASSERT(Tokenizer("printf returnx functionx").check({
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_NAME,
        }));

        CPPUNIT_ASSERT(isInvalidToken("abc1"));
        CPPUNIT_ASSERT(isInvalidToken("1abc"));
        CPPUNIT_ASSERT(isInvalidToken("a."));
        CPPUNIT_ASSERT(isInvalidToken(".b"));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenBase);
CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenAnalyze);

}
