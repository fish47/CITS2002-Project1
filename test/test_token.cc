extern "C" {
#include "ml_token.h"
}

#include "base.h"

#include <cstring>
#include <cstdio>
#include <algorithm>
#include <initializer_list>

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
        CPPUNIT_ASSERT(ctx);
        while (true) {
            const char *token = nullptr;
            ml_token_result result = {0};
            auto type = ml_token_iterate(ctx, &result);
            if (!func(type, &result))
                return false;
            if (type == ML_TOKEN_TYPE_EOF)
                break;
        }
        return true;
    }

public:
    Tokenizer(const char *s, int read_capacity = 4, int token_capacity = 32) {
        text = s;
        count = std::strlen(s);
        ml_token_ctx_init_fns(&ctx, &string_io_fns, this, read_capacity, token_capacity);
    }

    ~Tokenizer() {
        if (ctx)
            ml_token_ctx_uninit(&ctx);
    }

    struct ml_token_ctx *cast() const {
        return ctx;
    }

    bool check(std::initializer_list<std::string> tokens) {
        auto it = tokens.begin();
        return doCheck([&it, &tokens](enum ml_token_type type, const ml_token_result *result) {
            return it == tokens.end()
                ? (type == ML_TOKEN_TYPE_EOF)
                : (result->buf && *it++ == result->buf);
        });
    }

    bool check(std::initializer_list<enum ml_token_type> types) {
        auto it = types.begin();
        return doCheck([&it, &types](enum ml_token_type type, const ml_token_result *result) {
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

class TestTokenBase : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestTokenBase);
    CPPUNIT_TEST(testStopIterate);
    CPPUNIT_TEST(testInitFail);
    CPPUNIT_TEST(testGrowBufferFail);
    CPPUNIT_TEST_SUITE_END();

public:
    void testStopIterate() {
        Tokenizer t("abc");
        ml_token_iterate(t.cast(), nullptr);
        for (int i = 0; i < 10; i++)
            CPPUNIT_ASSERT_EQUAL(ML_TOKEN_TYPE_EOF, ml_token_iterate(t.cast(), nullptr));
    }

    void testInitFail() {
        for (int i = 0; i <= 2; i++) {
            setMaxAllocCount(i);
            CPPUNIT_ASSERT(!Tokenizer("haha").cast());
        }
    }

    void testGrowBufferFail() {
        setMaxAllocSize(200);

        std::string str;
        str += "a ";
        str += std::string(200, 'b');
        str += " c d\n";
        str += "e";

        CPPUNIT_ASSERT(Tokenizer(str.c_str(), 4).check({
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ERROR,
            ML_TOKEN_TYPE_LINE_TERMINATOR,
            ML_TOKEN_TYPE_NAME,
        }));
    }
};

class TestTokenAnalyze : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestTokenAnalyze);
    CPPUNIT_TEST(testTypes);
    CPPUNIT_TEST(testValues);
    CPPUNIT_TEST(testLineTerminator);
    CPPUNIT_TEST(testMergedSpace);
    CPPUNIT_TEST(testSpecialCharacter);
    CPPUNIT_TEST(testComment);
    CPPUNIT_TEST(testNumber);
    CPPUNIT_TEST(testIdentifier);
    CPPUNIT_TEST(testAssignmentOperator);
    CPPUNIT_TEST(testArgument);
    CPPUNIT_TEST_SUITE_END();

private:
    static bool isInvalidToken(const char *str) {
        return Tokenizer(str).check({ML_TOKEN_TYPE_ERROR});
    }

    static ml_token_result parseTokenValue(const char *str) {
        Tokenizer tokenizer(str);
        ml_token_result result = {0};
        ml_token_iterate(tokenizer.cast(), &result);
        return result;
    }


public:
    void testTypes() {
        CPPUNIT_ASSERT(Tokenizer(" \t+-*/,()1.1#\n<-").check({
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_TAB,
            ML_TOKEN_TYPE_PLUS,
            ML_TOKEN_TYPE_MINUS,
            ML_TOKEN_TYPE_MULTIPLY,
            ML_TOKEN_TYPE_DIVIDE,
            ML_TOKEN_TYPE_COMMA,
            ML_TOKEN_TYPE_PARENTHESIS_L,
            ML_TOKEN_TYPE_PARENTHESIS_R,
            ML_TOKEN_TYPE_NUMBER,
            ML_TOKEN_TYPE_COMMENT,
            ML_TOKEN_TYPE_LINE_TERMINATOR,
            ML_TOKEN_TYPE_ASSIGNMENT,
        }));

        CPPUNIT_ASSERT(Tokenizer("abc print return function arg0").check({
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_PRINT,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_RETURN,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_FUNCTION,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ARGUMENT,
        }));
    }

    void testValues() {
        CPPUNIT_ASSERT_EQUAL(1234.5, parseTokenValue("1234.5").value.real);
        CPPUNIT_ASSERT_EQUAL(1234, parseTokenValue("arg1234").value.index);
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
        CPPUNIT_ASSERT(Tokenizer("   +   -  ,").check({" ", "+", " ", "-", " ", ","}));
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

    void testAssignmentOperator() {
        CPPUNIT_ASSERT(Tokenizer("a<-b").check({"a", "<-", "b"}));
        CPPUNIT_ASSERT(isInvalidToken("< "));
        CPPUNIT_ASSERT(isInvalidToken("<."));
        CPPUNIT_ASSERT(isInvalidToken("<1"));
        CPPUNIT_ASSERT(isInvalidToken("<a"));
        CPPUNIT_ASSERT(isInvalidToken("<#"));
        CPPUNIT_ASSERT(isInvalidToken("<<"));
        CPPUNIT_ASSERT(isInvalidToken("<("));
    }

    void testArgument() {
        CPPUNIT_ASSERT(Tokenizer("arg arg0 arg9").check({"arg", " ", "arg0", " ", "arg9"}));
        CPPUNIT_ASSERT(isInvalidToken("arg00"));
        CPPUNIT_ASSERT(isInvalidToken("arg1."));
        CPPUNIT_ASSERT(isInvalidToken("arg1x"));
        CPPUNIT_ASSERT(Tokenizer("arg argx xarg arg2024").check({
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_NAME,
            ML_TOKEN_TYPE_SPACE,
            ML_TOKEN_TYPE_ARGUMENT,
        }));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenBase);
CPPUNIT_TEST_SUITE_REGISTRATION(TestTokenAnalyze);

}
