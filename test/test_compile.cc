extern "C" {
#include "ml_compile.h"
}


#include "base.h"

#include <cstring>
#include <vector>
#include <string>
#include <random>

namespace runml {

class Compiler {
public:
    struct Function {
        bool ret;
        RawString name;
        const std::vector<RawString> params;

        Function(bool r, const char *n, std::vector<RawString> &&p)
            : ret(r), name(n), params(std::move(p)) {}

        Function(bool r, const char *n, std::initializer_list<RawString> &&p = {})
            : Function(r, n, std::vector<RawString>(p)) {}

        bool operator==(const Function &rhs) const {
            return name == rhs.name
                && params.size() == rhs.params.size()
                && std::equal(params.cbegin(), params.cend(), rhs.params.cbegin());
        }
    };

private:
    ml_compile_ctx *ctx = nullptr;
    std::vector<int> args;
    std::vector<RawString> globals;
    std::vector<Function> functions;

private:
    static std::vector<RawString> makeParams(const union ml_compile_visit_data *data) {
        std::vector<RawString> params(data->func.count);
        for (int i = 0; i < data->func.count; i++)
            params[i] = data->func.params[i];
        return params;
    }

    static void onVisitEvent(void *opaque,
                             enum ml_compile_visit_event event,
                             const union ml_compile_visit_data *data) {
        auto c = reinterpret_cast<Compiler*>(opaque);
        switch (event) {
            case ML_COMPILE_VISIT_EVENT_ARG_VISIT_INDEX:
                c->args.emplace_back(data->index);
                break;
            case ML_COMPILE_VISIT_EVENT_GLOBAL_VISIT_VAR:
                c->globals.emplace_back(data->name);
                break;
            case ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_START:
                c->functions.emplace_back(data->func.ret, data->func.name, makeParams(data));
                break;
            default:
                break;
        }
    }

public:
    Compiler() { ml_compile_ctx_init(&ctx, nullptr); }
    ~Compiler() { ml_compile_ctx_uninit(&ctx); }

    const std::vector<int>& getGlobalArgIndexes() { return args; }
    const std::vector<RawString>& getGlobalVariables() { return globals; }
    const std::vector<Function>& getFunctions() { return functions; }

    enum ml_compile_result feedLines(std::vector<RawString>&& lines) {
        args.clear();
        globals.clear();
        functions.clear();

        Tokenizer t(std::move(lines));
        auto result = ml_compile_feed(ctx, t.cast());
        if (result == ML_COMPILE_RESULT_SUCCEED)
            ml_compile_accept(ctx, this, onVisitEvent);

        return result;
    }
};

class TestCompileCollect : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestCompileCollect);
    CPPUNIT_TEST(testGloabVariables);
    CPPUNIT_TEST(testGlobalArgIndexes);
    CPPUNIT_TEST_SUITE_END();

public:
    void testGloabVariables() {
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
            CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, c.feedLines(std::move(pointers)));

            globals = c.getGlobalVariables();
            std::sort(globals.begin(), globals.end());
            CPPUNIT_ASSERT(names == globals);
        }
    }

    void testGlobalArgIndexes() {
        Compiler c1;
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, c1.feedLines({
            "var <- x + y + arg4",
            "var <- x + y + arg7",
            "function func a b c",
            "\t a <- a + b + c + arg2",
            "var <- func(arg9, arg14, var) + arg47",
        }));
        CPPUNIT_ASSERT(checkList(c1.getGlobalArgIndexes(), {
            2, 4, 7, 9, 14, 47,
        }));

        Compiler c2;
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, c2.feedLines({
            "function func a b c",
            "\t  print (a + b) * c + arg0",
            "print arg0",
            "print (1 + 3) / 0.5 * 2 / 8",
            "print func(1, 2, arg47)",
            "print func(arg1, arg2, 4) + func(1, 2, arg3)",
        }));
        CPPUNIT_ASSERT(checkList(c2.getGlobalArgIndexes(), {
            0, 1, 2, 3, 47,
        }));
    }
};


class TestCompileFunction : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestCompileFunction);
    CPPUNIT_TEST(testParamCount);
    CPPUNIT_TEST(testParamName);
    CPPUNIT_TEST(testReturnType);
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
            {false, "zzzz"},
            {false, "z", {"a", "b", "c"}},
            {false, "za"},
            {false, "bc"},
            {false, "aab", {"x", "y"}},
        });

        Compiler c;
        for (const auto &line : signatures)
            CPPUNIT_ASSERT(c.feedLines({line, "\tvar <- 1", ""}) == ML_COMPILE_RESULT_SUCCEED);

        CPPUNIT_ASSERT(c.getFunctions().size() == funcs.size());
        for (int i = 0, n = funcs.size(); i < n; i++)
            CPPUNIT_ASSERT(c.getFunctions()[i] == funcs[i]);
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

        for (int i = 0, n = signatures.size(); i < n; i++)
            CPPUNIT_ASSERT_EQUAL(results[i], Compiler().feedLines({signatures[i], "\tvar <- 1", ""}));
    }

    void testReturnType() {
        Compiler c;
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, c.feedLines({
            "function a",
            "\tzz <- 1",
            "zzz <- 1",
            "function b",
            "\treturn zzz",
            "# what?",
            "function c",
            "\t xxxx <- 2",
            "\t return xxxx",
        }));

        CPPUNIT_ASSERT(!c.getFunctions()[0].ret);
        CPPUNIT_ASSERT(c.getFunctions()[1].ret);
        CPPUNIT_ASSERT(c.getFunctions()[2].ret);
    }

    void testWrongReturn() {
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN, Compiler().feedLines({
            "return bar",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_RETURN_IN_MAIN, Compiler().feedLines({
            "function foo",
            "\t tmp <- 1",
            "bar <- 1",
            "return bar  # what",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_REDUNDANT_RETURN, Compiler().feedLines({
            "function foo",
            "\t tmp <- 1",
            "\t return ok",
            "\t return again",
        }));
    }

    void testFinshBody() {
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION, Compiler().feedLines({
            "function foo",
            "\t print bar",
            "\t function bar",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NESTED_FUNCTION, Compiler().feedLines({
            "function foo",
            "# comment1",
            "# comment2",
            "\t function bar",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, Compiler().feedLines({
            "function foo",
            "# comment1",
            "# comment2",
            "\t print bar",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_SUCCEED, Compiler().feedLines({
            "function x a b c",
            "\t print a",
            "function y a b c",
            "\t print b",
        }));
    }

    void testEmptyBody() {
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION, Compiler().feedLines({
            "a <- 1",
            "function abc",
            "# comment1",
            "# comment2",
            "a <- haha",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION, Compiler().feedLines({
            "a <- 1",
            "function abc",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION, Compiler().feedLines({
            "a <- 1",
            "function abc",
            "",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_EMPTY_FUNCTION, Compiler().feedLines({
            "a <- 1",
            "function abc",
            "# haha",
            "# haha",
        }));
    }

    void testRedundantTab() {
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB, Compiler().feedLines({
            "function abc",
            "# haha",
            "# haha",
            "\t\t  a <- 1",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB, Compiler().feedLines({
            "function abc",
            "\t",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_REDUNDANT_TAB, Compiler().feedLines({
            "function abc",
            " \t # haha",
        }));
    }

    void testNameCollision() {
        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function var a b c",
            "\t var <- 1",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function abc a abc",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "global <- 1",
            "function global a b c",
            "\t var <- 1",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "global <- 1",
            "function func a b c global",
            "\t var <- 1",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function func a b c",
            "\t func <- 1",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function func a b c",
            "\t var <- a + b + c + func",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "var <- global",
            "function func a b c",
            "\t var <- global  ()  # haha",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function func a b c",
            "\t var <- 1",
            "bar <- func(1, 2, a)",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function x",
            "\t var <- 1",
            "function y",
            "\t var <- x(1, y)",
        }));

        CPPUNIT_ASSERT_EQUAL(ML_COMPILE_RESULT_ERROR_NAME_COLLISION, Compiler().feedLines({
            "function x var",
            "\t var <- 1",
            "print x(1, y, var)",
        }));
    }
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileCollect);
CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileFunction);

}
