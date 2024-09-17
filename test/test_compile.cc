extern "C" {
#include "ml_compile.h"
}


#include "base.h"

#include <cstring>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <initializer_list>

namespace runml {

class Compiler {
public:
    struct Function {
        RawString name;
        const std::vector<RawString> params;

        Function(const char *n, std::vector<RawString> &&p)
            : name(n), params(std::move(p)) {}

        Function(const char *n, std::initializer_list<RawString> &&p = {})
            : Function(n, std::vector<RawString>(p)) {}

        bool operator==(const Function &rhs) const {
            return name == rhs.name
                && params.size() == rhs.params.size()
                && std::equal(params.cbegin(), params.cend(), rhs.params.cbegin());
        }
    };

private:
    ml_compile_ctx *ctx = nullptr;

public:
    Compiler() {
        ml_compile_ctx_init(&ctx, nullptr);
    }

    ~Compiler() {
        ml_compile_ctx_uninit(&ctx);
    }

    enum ml_compile_result feedLines(std::vector<RawString>&& lines) {
        Tokenizer t(std::move(lines));
        return ml_compile_feed_tokens(ctx, t.cast());
    }

    template <typename F>
    void iterateGlobalVariables(F op) {
        CPPUNIT_ASSERT(ctx);
        const char **names = nullptr;
        const auto count = ml_compile_get_global_names(ctx, &names);
        for (int i = 0; i < count; i++)
            op(names[i]);
    }

    int getFunctionCount() {
        return ml_compile_get_func_count(ctx);
    }

    Function getFunctionAt(int i) {
        CPPUNIT_ASSERT(i < getFunctionCount());
        std::vector<RawString> params;
        const char *name = ml_compile_get_func_name(ctx, i);
        for (int j = 0, n = ml_compile_get_func_param_count(ctx, i); j < n; j++)
            params.emplace_back(ml_compile_get_func_param_name(ctx, i, j));
        return Function(name, std::move(params));
    }
};

class TestCompileStrings : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestCompileStrings);
    CPPUNIT_TEST(testCollectGloabNames);
    CPPUNIT_TEST_SUITE_END();

public:
    void testCollectGloabNames() {
        std::vector<RawString> names {"abc", "helen", "fish", "uwa"};
        std::sort(names.begin(), names.end());

        std::vector<std::string> lines;
        std::vector<RawString> globals;
        std::vector<RawString> pointers;
        std::default_random_engine engine;
        for (int i = 0; i < 40; i++) {
            // make assignment statements
            lines.clear();
            for (const auto &name : names) {
                lines.emplace_back();
                lines.back().append(name);
                lines.back().append(" <- 1");
            }

            // shuffle assignment statements
            std::shuffle(lines.begin(), lines.end(), engine);

            // collect global names
            Compiler c;
            for (const auto& line : lines)
                pointers.emplace_back(line.c_str());
            pointers.emplace_back("");
            CPPUNIT_ASSERT(c.feedLines(std::move(pointers)) == ML_COMPILE_RESULT_SUCCEED);

            globals.clear();
            c.iterateGlobalVariables([&globals](const char *name) { globals.emplace_back(name); });
            std::sort(globals.begin(), globals.end());
            CPPUNIT_ASSERT(names == globals);
        }
    }
};


class TestCompileFunction : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestCompileFunction);
    CPPUNIT_TEST(testParamCount);
    CPPUNIT_TEST(testParamName);
    CPPUNIT_TEST(testWrongReturn);
    CPPUNIT_TEST(testFinshBody);
    CPPUNIT_TEST(testEmptyBody);
    CPPUNIT_TEST(testRedundantTab);
    CPPUNIT_TEST(testNameCollision);
    CPPUNIT_TEST_SUITE_END();

public:
    void testParamCount() {
        const std::vector<RawString> signatures({
            "function zzzz",
            "function z a b c",
            "function za     ",
            "function bc # Hello",
            "function aab x y # Hello",
        });
        const std::vector<Compiler::Function> funcs({
            {"zzzz"},
            {"z", {"a", "b", "c"}},
            {"za"},
            {"bc"},
            {"aab", {"x", "y"}},
        });

        Compiler c;
        for (const auto &line : signatures)
            CPPUNIT_ASSERT(c.feedLines({line, "\tvar <- 1", ""}) == ML_COMPILE_RESULT_SUCCEED);

        CPPUNIT_ASSERT(c.getFunctionCount() == funcs.size());
        for (int i = 0, n = funcs.size(); i < n; i++)
            CPPUNIT_ASSERT(c.getFunctionAt(i) == funcs[i]);
    }

    void testParamName() {
        const std::vector<RawString> signatures({
            "function abc a b a",
            "function abc a b b",
            "function #hello",
            "function abc,",
            "function func a b var",
        });
        const std::vector<enum ml_compile_result> results({
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_SUCCEED,
        });
        CPPUNIT_ASSERT(signatures.size() == results.size());

        for (int i = 0, n = signatures.size(); i < n; i++) {
            const auto res = Compiler().feedLines({signatures[i], "\tvar <- 1", ""});
            CPPUNIT_ASSERT(res == results[i]);
        }
    }

    void testWrongReturn() {
        CPPUNIT_ASSERT(Compiler().feedLines({
            "return bar",
        }) == ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function foo",
            "\t tmp <- 1",
            "bar <- 1",
            "return bar  # what",
        }) == ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function foo",
            "\t tmp <- 1",
            "\t return ok",
            "\t return again",
        }) == ML_COMPILE_RESULT_ERROR_REDUNDANT_RETURN);
    }

    void testFinshBody() {
        CPPUNIT_ASSERT(Compiler().feedLines({
            "function foo",
            "\t print bar",
            "\t function bar",
        }) == ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function foo",
            "# comment1",
            "# comment2",
            "\t function bar",
        }) == ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function foo",
            "# comment1",
            "# comment2",
            "\t print bar",
        }) == ML_COMPILE_RESULT_SUCCEED);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function x a b c",
            "\t print a",
            "function y a b c",
            "\t print b",
        }) == ML_COMPILE_RESULT_SUCCEED);
    }

    void testEmptyBody() {
        CPPUNIT_ASSERT(Compiler().feedLines({
            "a <- 1",
            "function abc",
            "# comment1",
            "# comment2",
            "a <- haha",
        }) == ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "a <- 1",
            "function abc",
        }) == ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "a <- 1",
            "function abc",
            "",
        }) == ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "a <- 1",
            "function abc",
            "# haha",
            "# haha",
        }) == ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION);
    }

    void testRedundantTab() {
        CPPUNIT_ASSERT(Compiler().feedLines({
            "function abc",
            "# haha",
            "# haha",
            "\t\t  a <- 1",
        }) == ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function abc",
            "\t",
        }) == ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function abc",
            " \t # haha",
        }) == ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB);
    }

    void testNameCollision() {
        CPPUNIT_ASSERT(Compiler().feedLines({
            "function var a b c",
            "\t var <- 1",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function abc a abc",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "global <- 1",
            "function global a b c",
            "\t var <- 1",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "global <- 1",
            "function func a b c global",
            "\t var <- 1",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function func a b c",
            "\t func <- 1",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function func a b c",
            "\t var <- a + b + c + func",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "var <- global",
            "function func a b c",
            "\t var <- global  ()  # haha",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function func a b c",
            "\t var <- 1",
            "bar <- func(1, 2, a)",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function x",
            "\t var <- 1",
            "function y",
            "\t var <- x(1, y)",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);

        CPPUNIT_ASSERT(Compiler().feedLines({
            "function x var",
            "\t var <- 1",
            "print x(1, y, var)",
        }) == ML_COMPILE_RESULT_ERROR_NAME_COLLISION);
    }
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileStrings);
CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileFunction);

}
