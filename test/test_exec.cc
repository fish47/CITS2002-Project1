extern "C" {
#include "ml_exec.h"
}

#include "base.h"

#include <cstdio>
#include <cstdarg>
#include <vector>
#include <algorithm>
#include <initializer_list>

namespace runml {

class TestExecution : public BaseTextFixture {

    CPPUNIT_TEST_SUITE(TestExecution);
    CPPUNIT_TEST(testSample1);
    CPPUNIT_TEST(testSample2);
    CPPUNIT_TEST(testSample3);
    CPPUNIT_TEST(testSample4);
    CPPUNIT_TEST(testSample5);
    CPPUNIT_TEST(testSample6);
    CPPUNIT_TEST(testSample7);
    CPPUNIT_TEST(testSample8);
    CPPUNIT_TEST(testNoInputFile);
    CPPUNIT_TEST(testNoneFile);
    CPPUNIT_TEST(testForwardArgs);
    CPPUNIT_TEST_SUITE_END();

private:
    std::string stderr_data;
    std::vector<std::string> stdout_lines;
    std::vector<std::string> temp_file_paths;

private:
    static bool makeTempFilePath(void *opaque, ml_exec_path path, const char *suffix) {
        auto t = reinterpret_cast<TestExecution*>(opaque);
        std::string name = std::tmpnam(nullptr);
        name.append(suffix);
        if (name.length() + 1 > sizeof(ml_exec_path))
            return false;
        name.copy(path, name.length());
        path[name.length()] = 0;
        t->temp_file_paths.emplace_back(std::move(name));
        return true;
    }

    static void writeStdout(void *opaque, const char *buf, int n) {
        auto t = reinterpret_cast<TestExecution*>(opaque);
        if (t->stdout_lines.empty())
            t->stdout_lines.emplace_back();
        t->stdout_lines.back().append(buf, n);
        while (true) {
            auto pos = t->stdout_lines.back().find('\n');
            if (pos == std::string::npos)
                break;

            auto cut = t->stdout_lines.back().substr(pos + 1);
            t->stdout_lines.back().resize(pos);
            t->stdout_lines.emplace_back(std::move(cut));
        }
    }

    static void writeStderr(void *opaque, const char *fmt, ...) {
        auto t = reinterpret_cast<TestExecution*>(opaque);
        std::va_list args;
        va_start(args, fmt);
        auto n = std::vsnprintf(nullptr, 0, fmt, args);
        std::vector<char> buf(n + 1);
        std::vsprintf(buf.data(), fmt, args);
        t->stderr_data.append(buf.data(), n);
        va_end(args);
    }

    int runCode(std::initializer_list<const char*> params,
                std::initializer_list<const char*> lines) {
        // create a source code file
        std::string path = std::tmpnam(nullptr);
        std::FILE *f = fopen(path.c_str(), "w");
        CPPUNIT_ASSERT(f);
        temp_file_paths.push_back(path);

        // write lines
        std::for_each(lines.begin(), lines.end(), [&f](const char *s) {
            std::fwrite(s, 1, RawString(s).length(), f);
            std::fwrite("\n", 1, 1, f);
        });
        std::fclose(f);

        // make arguments
        std::vector<const char*> argv {"?", path.c_str()};
        std::copy(params.begin(), params.end(), std::back_inserter(argv));
        argv.push_back(nullptr);

        return runCode(argv.size() - 1, argv.data());
    }

    int runCode(int argc, const char **argv) {
        // run code
        const ml_exec_run_fns fns {
            writeStdout,
            writeStderr,
            makeTempFilePath,
        };
        ml_exec_ctx ctx { &fns, this };
        auto ret = ml_exec_run_main(&ctx, argc, const_cast<char**>(argv));

        if (!stdout_lines.empty() && stdout_lines.back().empty())
            stdout_lines.pop_back();

        return ret;
    }

public:
    virtual void tearDown() override {
        BaseTextFixture::tearDown();
        for (const auto &path : temp_file_paths)
            std::remove(path.c_str());
    }

public:
    void testSample1() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# an assignment statement, nothing is printed",
            "x <- 2.3",
        }));
    }

    void testSample2() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# an assignment statement, 2.500000 is printed",
            "x <- 2.5",
            "print x",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"2.500000"}));
    }

    void testSample3() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 3.500000 is printed",
            "print 3.5",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"3.500000"}));
    }

    void testSample4() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 24 is printed",
            "x <- 8",
            "y <- 3",
            "print x * y",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"24"}));
    }

    void testSample5() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 18 is printed",
            "#",
            "function printsum a b",
            "	print a + b",
            "#",
            "printsum (12, 6)",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"18"}));
    }

    void testSample6() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 72 is printed",
            "#",
            "function multiply a b",
            "	return a * b",
            "#",
            "print multiply(12, 6)",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"72"}));
    }

    void testSample7() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 50 is printed",
            "#",
            "function multiply a b",
            "	x <- a * b",
            "	return x",
            "#",
            "print multiply(10, 5)",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"50"}));
    }

    void testSample8() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({}, {
            "# 9 is printed",
            "#",
            "one <- 1",
            "#",
            "function increment value",
            "	return value + one",
            "#",
            "print increment(3) + increment(4)",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"9"}));
    }

    void testNoInputFile() {
        const char *argv[] = {"?", nullptr};
        CPPUNIT_ASSERT_EQUAL(EXIT_FAILURE, runCode(1, argv));
        CPPUNIT_ASSERT(stderr_data.find("no input") != std::string::npos);
    }

    void testNoneFile() {
        const char *argv[] = {"?", "/", nullptr};
        CPPUNIT_ASSERT_EQUAL(EXIT_FAILURE, runCode(2, argv));
        CPPUNIT_ASSERT(stderr_data.find("readable") != std::string::npos);
    }

    void testForwardArgs() {
        CPPUNIT_ASSERT_EQUAL(EXIT_SUCCESS, runCode({"4", "5", "6"}, {
            "function add a b",
            "	return a + b",
            "",
            "function mul a b",
            "	return a * b",
            "",
            "print add(arg2, 1)",
            "print mul(arg0, 4)",
            "print mul(arg1, 4)",
            "print add(arg2024, 1)",
        }));
        CPPUNIT_ASSERT(checkList(stdout_lines, {"7", "16", "20", "1"}));
    }
};

CPPUNIT_TEST_SUITE_REGISTRATION(TestExecution);

}
