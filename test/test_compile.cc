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
            case ML_COMPILE_VISIT_EVENT_ARG_SECTION_START:
                c->args.clear();
                break;
            case ML_COMPILE_VISIT_EVENT_ARG_VISIT_INDEX:
                c->args.emplace_back(data->index);
                break;
            case ML_COMPILE_VISIT_EVENT_GLOBAL_SECTION_START:
                c->globals.clear();
                break;
            case ML_COMPILE_VISIT_EVENT_GLOBAL_VISIT_VAR:
                c->globals.emplace_back(data->name);
                break;
            case ML_COMPILE_VISIT_EVENT_SUB_FUNC_SECTION_START:
                c->functions.clear();
                break;
            case ML_COMPILE_VISIT_EVENT_SUB_FUNC_VISIT_START:
                c->functions.emplace_back(data->func.name, makeParams(data));
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
            CPPUNIT_ASSERT(c.feedLines(std::move(pointers)) == ML_COMPILE_RESULT_SUCCEED);

            globals = c.getGlobalVariables();
            std::sort(globals.begin(), globals.end());
            CPPUNIT_ASSERT(names == globals);
        }
    }

    void testGlobalArgIndexes() {
        Compiler c1;
        CPPUNIT_ASSERT(c1.feedLines({
            "var <- x + y + arg4",
            "var <- x + y + arg7",
            "function func a b c",
            "\t a <- a + b + c + arg2",
            "var <- func(arg9, arg14, var) + arg47",
        }) == ML_COMPILE_RESULT_SUCCEED);
        CPPUNIT_ASSERT(c1.getGlobalArgIndexes() == std::vector<int>({
            2, 4, 7, 9, 14, 47,
        }));

        Compiler c2;
        CPPUNIT_ASSERT(c2.feedLines({
            "function func a b c",
            "\t  print (a + b) * c + arg0",
            "print arg0",
            "print (1 + 3) / 0.5 * 2 / 8",
            "print func(1, 2, arg47)",
            "print func(arg1, arg2, 4) + func(1, 2, arg3)",
        }) == ML_COMPILE_RESULT_SUCCEED);
        CPPUNIT_ASSERT(c2.getGlobalArgIndexes() == std::vector<int>({
            0, 1, 2, 3, 47,
        }));
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


CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileCollect);
CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileFunction);

}
