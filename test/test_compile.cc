extern "C" {
#include "ml_compile.h"
}


#include "base.h"

#include <cstring>
#include <vector>
#include <string>
#include <random>
#include <algorithm>

namespace runml {

class Compiler {
private:
    ml_compile_ctx *ctx = nullptr;

public:
    Compiler() {
        ml_compile_ctx_init(&ctx, nullptr);
    }

    ~Compiler() {
        ml_compile_ctx_uninit(&ctx);
    }

    void feedLine(const char *str) {
        Tokenizer(str).iterate([this](enum ml_token_type type, const ml_token_data &data) {
            ml_compile_feed_token(ctx, type, &data);
        });
        ml_compile_feed_token(ctx, ML_TOKEN_TYPE_LINE_TERMINATOR, nullptr);
    }

    template <typename F>
    void iterateGlobalVarNames(F func) {
        CPPUNIT_ASSERT(ctx);
        const char **names = nullptr;
        const auto count = ml_compile_get_global_vars(ctx, &names);
        for (int i = 0; i < count; i++)
            func(names[i]);
    }
};

class TestCompileStrings : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestCompileStrings);
    CPPUNIT_TEST(testCollectGloabVars);
    CPPUNIT_TEST_SUITE_END();

public:
    void testCollectGloabVars() {
        std::vector<std::string> names {"abc", "helen", "fish", "uwa"};
        std::sort(names.begin(), names.end());

        std::vector<std::string> tmp;
        std::default_random_engine engine;
        for (int i = 0; i < 40; i++) {
            // make assignment statements
            tmp.clear();
            for (const auto &name : names) {
                tmp.emplace_back();
                tmp.back().append(name);
                tmp.back().append(" <- 1\n");
            }

            // shuffle and feed to the compiler
            std::shuffle(tmp.begin(), tmp.end(), engine);
            Compiler compiler;
            for (const auto &line : tmp)
                compiler.feedLine(line.c_str());

            // collect all names and check
            tmp.clear();
            compiler.iterateGlobalVarNames([&tmp](const char *name) { tmp.emplace_back(name); });
            std::sort(tmp.begin(), tmp.end());
            CPPUNIT_ASSERT(names == tmp);
        }
    }
};


CPPUNIT_TEST_SUITE_REGISTRATION(TestCompileStrings);

}
