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
        const char *name;
        const std::vector<const char*> params;

        Function(const char *n, std::vector<const char*> &&p)
            : name(n), params(std::move(p)) {}

        Function(const char *n, std::initializer_list<const char*> &&p = {})
            : Function(n, std::vector<const char*>(p)) {}

        bool operator==(const Function &rhs) const {
            return std::strcmp(name, rhs.name) == 0
                && params.size() == rhs.params.size()
                && std::equal(params.cbegin(), params.cend(), rhs.params.cbegin(),
                              [](const char *l, const char *r) { return std::strcmp(l, r) == 0; });
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

    enum ml_compile_result feedLines(std::vector<const char*>&& lines) {
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
        std::vector<const char*> params;
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
        std::vector<std::string> names {"abc", "helen", "fish", "uwa"};
        std::sort(names.begin(), names.end());

        std::vector<std::string> lines;
        std::vector<std::string> globals;
        std::vector<const char*> pointers;
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
    CPPUNIT_TEST(testNames);
    CPPUNIT_TEST(testInvalid);
    CPPUNIT_TEST_SUITE_END();

public:
    void testNames() {
        const std::vector<const char*> signatures = {
            "function zzzz",
            "function z a b c",
            "function za     ",
            "function bc # Hello",
            "function aab a b # Hello",
        };
        const std::vector<Compiler::Function> funcs({
            {"zzzz"},
            {"z", {"a", "b", "c"}},
            {"za"},
            {"bc"},
            {"aab", {"a", "b"}},
        });

        Compiler c;
        for (const auto &line : signatures)
            CPPUNIT_ASSERT(c.feedLines({line, "\tvar <- 1", ""}) == ML_COMPILE_RESULT_SUCCEED);

        CPPUNIT_ASSERT(c.getFunctionCount() == funcs.size());
        for (int i = 0, n = funcs.size(); i < n; i++)
            CPPUNIT_ASSERT(c.getFunctionAt(i) == funcs[i]);
    }

    void testInvalid() {
        const std::vector<const char*> signatures = {
            "function abc a b a",
            "function abc a abc",
            "function abc a b b",
            "function #hello",
            "function abc,",
        };
        const std::vector<enum ml_compile_result> results = {
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_NAME_COLLISION,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
            ML_COMPILE_RESULT_ERROR_SYNTAX_ERROR,
        };
        CPPUNIT_ASSERT(signatures.size() == results.size());

        for (int i = 0, n = signatures.size(); i < n; i++) {
            const auto res = Compiler().feedLines({signatures[i], "\tvar <- 1", ""});
            CPPUNIT_ASSERT(res == results[i]);
        }
    }
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileStrings);
CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileFunction);

}
