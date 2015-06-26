#include <stdexcept>
#include <map>
#include <string>
#include <iostream>
#include <functional>

#include <memory>
#include <realm/util/thread.hpp>

#include "demangle.hpp"
#include "timer.hpp"
#include "random.hpp"
#include "wildcard.hpp"
#include "unit_test.hpp"

using namespace realm;
using namespace realm::util;
using namespace realm::test_util;
using namespace realm::test_util::unit_test;



// FIXME: Think about order of tests during execution.
// FIXME: Write quoted strings with escaped nonprintables



namespace {


struct SharedContext {
    Reporter& m_reporter;
    std::vector<Test*> m_tests;
    Mutex m_mutex;
    size_t m_next_test;

    SharedContext(Reporter& reporter):
        m_reporter(reporter),
        m_next_test(0)
    {
    }
};


void replace_char(std::string& str, char c, const std::string& replacement)
{
    for (size_t pos = str.find(c); pos != std::string::npos; pos = str.find(c, pos + 1))
        str.replace(pos, 1, replacement);
}


std::string xml_escape(const std::string& value)
{
    std::string value_2 = value;
    replace_char(value_2, '&',  "&amp;");
    replace_char(value_2, '<',  "&lt;");
    replace_char(value_2, '>',  "&gt;");
    replace_char(value_2, '\'', "&apos;");
    replace_char(value_2, '\"', "&quot;");
    return value_2;
}


class XmlReporter: public Reporter {
public:
    XmlReporter(std::ostream& out):
        m_out(out)
    {
    }

    ~XmlReporter() REALM_NOEXCEPT
    {
    }

    void begin(const TestDetails& details) override
    {
        test& t = m_tests[details.test_index];
        t.m_details = details;
    }

    void fail(const TestDetails& details, const std::string& message) override
    {
        failure f;
        f.m_details = details;
        f.m_message = message;
        test& t = m_tests[details.test_index];
        t.m_failures.push_back(f);
    }

    void end(const TestDetails& details, double elapsed_seconds) override
    {
        test& t = m_tests[details.test_index];
        t.m_elapsed_seconds = elapsed_seconds;
    }

    void summary(const Summary& summary) override
    {
        m_out <<
            "<?xml version=\"1.0\"?>\n"
            "<unittest-results "
            "tests=\"" << summary.num_included_tests << "\" "
            "failedtests=\"" << summary.num_failed_tests << "\" "
            "checks=\"" << summary.num_checks << "\" "
            "failures=\"" << summary.num_failed_checks << "\" "
            "time=\"" << summary.elapsed_seconds << "\">\n";
        typedef tests::const_iterator test_iter;
        test_iter tests_end = m_tests.end();
        for (test_iter i_1 = m_tests.begin(); i_1 != tests_end; ++i_1) {
            const test& t = i_1->second;
            m_out <<
                "  <test suite=\""<< xml_escape(t.m_details.suite_name) <<"\" "
                "name=\"" << xml_escape(t.m_details.test_name) << "\" "
                "time=\"" << t.m_elapsed_seconds << "\"";
            if (t.m_failures.empty()) {
                m_out << "/>\n";
                continue;
            }
            m_out << ">\n";
            typedef std::vector<failure>::const_iterator fail_iter;
            fail_iter fails_end = t.m_failures.end();
            for (fail_iter i_2 = t.m_failures.begin(); i_2 != fails_end; ++i_2) {
                std::string msg = xml_escape(i_2->m_message);
                m_out << "    <failure message=\"" << i_2->m_details.file_name << ""
                    "(" << i_2->m_details.line_number << ") : " << msg << "\"/>\n";
            }
            m_out << "  </test>\n";
        }
        m_out <<
            "</unittest-results>\n";
    }

protected:
    struct failure {
        TestDetails m_details;
        std::string m_message;
    };

    struct test {
        TestDetails m_details;
        std::vector<failure> m_failures;
        double m_elapsed_seconds;
    };

    typedef std::map<long, test> tests; // Key is test index
    tests m_tests;

    std::ostream& m_out;
};


class WildcardFilter: public Filter {
public:
    WildcardFilter(const std::string& filter)
    {
        bool exclude = false;
        typedef std::string::const_iterator iter;
        iter i = filter.begin(), end = filter.end();
        for (;;) {
            // Skip space
            while (i != end) {
                if (*i != ' ')
                    break;
                ++i;
            }

            // End of input?
            if (i == end)
                break;

            iter word_begin = i;

            // Find end of word
            while (i != end) {
                if (*i == ' ')
                    break;
                ++i;
            }

            iter word_end = i;
            size_t word_size = word_end - word_begin;
            if (word_size == 1 && *word_begin == '-') {
                exclude = true;
                continue;
            }

            std::string word(word_begin, word_end);
            patterns& p = exclude ? m_exclude : m_include;
            p.push_back(wildcard_pattern(word));
        }

        // Include everything if no includes are specified.
        if (m_include.empty())
            m_include.push_back(wildcard_pattern("*"));
    }

    ~WildcardFilter() REALM_NOEXCEPT
    {
    }

    bool include(const TestDetails& details) override
    {
        const char* name_begin = details.test_name.data();
        const char* name_end   = name_begin + details.test_name.size();
        typedef patterns::const_iterator iter;

        // Say "no" if it matches an exclude pattern
        {
            iter end = m_exclude.end();
            for (iter i = m_exclude.begin(); i != end; ++i) {
                if (i->match(name_begin, name_end))
                    return false;
            }
        }

        // Say "yes" if it matches an include pattern
        {
            iter end = m_include.end();
            for (iter i = m_include.begin(); i != end; ++i) {
                if (i->match(name_begin, name_end))
                    return true;
            }
        }

        // Not included
        return false;
    }

private:
    typedef std::vector<wildcard_pattern> patterns;
    patterns m_include, m_exclude;
};


} // anonymous namespace



namespace realm {
namespace test_util {
namespace unit_test {


class TestList::ExecContext {
public:
    SharedContext* m_shared;
    Mutex m_mutex;
    long long m_num_checks;
    long long m_num_failed_checks;
    long m_num_failed_tests;
    bool m_errors_seen;

    ExecContext():
        m_shared(0),
        m_num_checks(0),
        m_num_failed_checks(0),
        m_num_failed_tests(0)
    {
    }

    void run();
};


void TestList::add(Test& test, const char* suite, const std::string& name,
                   const char* file, long line)
{
    test.test_results.m_test = &test;
    test.test_results.m_list = this;
    long index = long(m_tests.size());
    TestDetails& details = test.test_details;
    details.test_index  = index;
    details.suite_name  = suite;
    details.test_name   = name;
    details.file_name   = file;
    details.line_number = line;
    m_tests.push_back(&test);
}

void TestList::reassign_indexes()
{
    long n = long(m_tests.size());
    for (long i = 0; i != n; ++i) {
        Test* test = m_tests[i];
        test->test_details.test_index = i;
    }
}

void TestList::ExecContext::run()
{
    Timer timer;
    double time = 0;
    Test* test = nullptr;
    for (;;) {
        double prev_time = time;
        time = timer.get_elapsed_time();

        // Next test
        {
            SharedContext& shared = *m_shared;
            Reporter& reporter = shared.m_reporter;
            LockGuard lock(shared.m_mutex);
            if (test)
                reporter.end(test->test_details, time - prev_time);
            if (shared.m_next_test == shared.m_tests.size())
                break;
            test = shared.m_tests[shared.m_next_test++];
            reporter.begin(test->test_details);
        }

        m_errors_seen = false;
        test->test_results.m_context = this;

        try {
            test->test_run();
        }
        catch (std::exception& ex) {
            std::string message = "Unhandled exception "+get_type_name(ex)+": "+ex.what();
            test->test_results.test_failed(message);
        }
        catch (...) {
            m_errors_seen = true;
            std::string message = "Unhandled exception of unknown type";
            test->test_results.test_failed(message);
        }

        test->test_results.m_context = 0;
        if (m_errors_seen)
            ++m_num_failed_tests;
    }
}

bool TestList::run(Reporter* reporter, Filter* filter, int num_threads, bool shuffle)
{
    Timer timer;
    Reporter fallback_reporter;
    Reporter& reporter_2 = reporter ? *reporter : fallback_reporter;
    if (num_threads < 1 || num_threads > 1024)
        throw std::runtime_error("Bad number of threads");

    SharedContext shared(reporter_2);
    size_t num_tests = m_tests.size(), num_disabled = 0;
    for (size_t i = 0; i != num_tests; ++i) {
        Test* test = m_tests[i];
        if (!test->test_enabled()) {
            ++num_disabled;
            continue;
        }
        if (filter && !filter->include(test->test_details))
            continue;
        shared.m_tests.push_back(test);
    }

    if (shuffle) {
        Random random(random_int<unsigned long>()); // Seed from slow global generator
        random.shuffle(shared.m_tests.begin(), shared.m_tests.end());
    }

    std::unique_ptr<ExecContext[]> thread_contexts(new ExecContext[num_threads]);
    for (int i = 0; i != num_threads; ++i)
        thread_contexts[i].m_shared = &shared;

    if (num_threads == 1) {
        thread_contexts[0].run();
    }
    else {
        std::unique_ptr<Thread[]> threads(new Thread[num_threads]);
        for (int i = 0; i != num_threads; ++i)
            threads[i].start(std::bind(&ExecContext::run, &thread_contexts[i]));
        for (int i = 0; i != num_threads; ++i)
            threads[i].join();
    }

    long num_failed_tests = 0;
    long long num_checks = 0;
    long long num_failed_checks = 0;

    for (int i = 0; i != num_threads; ++i) {
        ExecContext& thread_context = thread_contexts[i];
        num_failed_tests  += thread_context.m_num_failed_tests;
        num_checks        += thread_context.m_num_checks;
        num_failed_checks += thread_context.m_num_failed_checks;
    }

    Summary summary;
    summary.num_included_tests = long(shared.m_tests.size());
    summary.num_failed_tests   = num_failed_tests;
    summary.num_excluded_tests = long(num_tests - num_disabled) - summary.num_included_tests;
    summary.num_disabled_tests = long(num_disabled);
    summary.num_checks         = num_checks;
    summary.num_failed_checks  = num_failed_checks;
    summary.elapsed_seconds    = timer.get_elapsed_time();
    reporter_2.summary(summary);

    return num_failed_tests == 0;
}


TestList& get_default_test_list()
{
    static TestList list;
    return list;
}


TestResults::TestResults():
    m_test(0),
    m_list(0),
    m_context(0)
{
}


void TestResults::check_succeeded()
{
    LockGuard lock(m_context->m_mutex);
    ++m_context->m_num_checks;
}


void TestResults::check_failed(const char* file, long line, const std::string& message)
{
    {
        LockGuard lock(m_context->m_mutex);
        ++m_context->m_num_checks;
        ++m_context->m_num_failed_checks;
        m_context->m_errors_seen = true;
    }
    SharedContext& shared = *m_context->m_shared;
    TestDetails details = m_test->test_details; // Copy
    details.file_name   = file;
    details.line_number = line;
    {
        LockGuard lock(shared.m_mutex);
        shared.m_reporter.fail(details, message);
    }
}


void TestResults::test_failed(const std::string& message)
{
    {
        LockGuard lock(m_context->m_mutex);
        m_context->m_errors_seen = true;
    }
    SharedContext& shared = *m_context->m_shared;
    {
        LockGuard lock(shared.m_mutex);
        shared.m_reporter.fail(m_test->test_details, message);
    }
}


void TestResults::cond_failed(const char* file, long line, const char* macro_name,
                              const char* cond_text)
{
    std::string msg = std::string(macro_name)+"("+cond_text+") failed";
    check_failed(file, line, msg);
}


void TestResults::compare_failed(const char* file, long line, const char* macro_name,
                                 const char* a_text, const char* b_text,
                                 const std::string& a_val, const std::string& b_val)
{
    std::string msg = std::string(macro_name)+"("+a_text+", "+b_text+") failed with ("+a_val+", "+b_val+")";
    check_failed(file, line, msg);
}


void TestResults::inexact_compare_failed(const char* file, long line, const char* macro_name,
                                         const char* a_text, const char* b_text,
                                         const char* eps_text, long double a, long double b,
                                         long double eps)
{
    std::ostringstream out;
    out.precision(std::numeric_limits<long double>::digits10 + 1);
    out << macro_name<<"("<<a_text<<", "<<b_text<<", "<<eps_text<<") "
        "failed with ("<<a<<", "<<b<<", "<<eps<<")";
    check_failed(file, line, out.str());
}


void TestResults::throw_failed(const char* file, long line, const char* expr_text,
                               const char* exception_name)
{
    std::ostringstream out;
    out << "CHECK_THROW("<<expr_text<<", "<<exception_name<<") failed: Did not throw";
    check_failed(file, line, out.str());
}


void TestResults::throw_ex_failed(const char* file, long line, const char* expr_text,
                                  const char* exception_name, const char* exception_cond_text)
{
    std::ostringstream out;
    out << "CHECK_THROW_EX("<<expr_text<<", "<<exception_name<<", "<<
        exception_cond_text<<") failed: Did not throw";
    check_failed(file, line, out.str());
}


void TestResults::throw_ex_cond_failed(const char* file, long line, const char* expr_text,
                                       const char* exception_name, const char* exception_cond_text)
{
    std::ostringstream out;
    out << "CHECK_THROW_EX("<<expr_text<<", "<<exception_name<<", "<<
        exception_cond_text<<") failed: Did throw, but condition failed";
    check_failed(file, line, out.str());
}


void TestResults::throw_any_failed(const char* file, long line, const char* expr_text)
{
    std::ostringstream out;
    out << "CHECK_THROW_ANY("<<expr_text<<") failed: Did not throw";
    check_failed(file, line, out.str());
}


void Reporter::begin(const TestDetails&)
{
}

void Reporter::fail(const TestDetails&, const std::string&)
{
}

void Reporter::end(const TestDetails&, double)
{
}

void Reporter::summary(const Summary&)
{
}


class PatternBasedFileOrder::state: public RefCountBase {
public:
    typedef std::map<TestDetails*, int> major_map;
    major_map m_major_map;

    typedef std::vector<wildcard_pattern> patterns;
    patterns m_patterns;

    state(const char** patterns_begin, const char** patterns_end)
    {
        for (const char** i = patterns_begin; i != patterns_end; ++i)
            m_patterns.push_back(wildcard_pattern(*i));
    }

    ~state() REALM_NOEXCEPT
    {
    }

    int get_major(TestDetails* details)
    {
        major_map::const_iterator i = m_major_map.find(details);
        if (i != m_major_map.end())
            return i->second;
        patterns::const_iterator j = m_patterns.begin(), end = m_patterns.end();
        while (j != end && !j->match(details->file_name))
            ++j;
        int major = int(j - m_patterns.begin());
        m_major_map[details] = major;
        return major;
    }
};

bool PatternBasedFileOrder::operator()(TestDetails* a, TestDetails* b)
{
    int major_a = m_wrap.m_state->get_major(a);
    int major_b = m_wrap.m_state->get_major(b);
    if (major_a < major_b)
        return true;
    if (major_a > major_b)
        return false;
    int i = strcmp(a->file_name, b->file_name);
    return i < 0 || (i == 0 && a->test_index < b->test_index);
}

PatternBasedFileOrder::wrap::wrap(const char** patterns_begin, const char** patterns_end):
    m_state(new state(patterns_begin, patterns_end))
{
}

PatternBasedFileOrder::wrap::~wrap()
{
}

PatternBasedFileOrder::wrap::wrap(const wrap& w):
    m_state(w.m_state)
{
}

PatternBasedFileOrder::wrap& PatternBasedFileOrder::wrap::operator=(const wrap& w)
{
    m_state = w.m_state;
    return *this;
}


SimpleReporter::SimpleReporter(bool report_progress)
{
    m_report_progress = report_progress;
}

void SimpleReporter::begin(const TestDetails& details)
{
    if (!m_report_progress)
        return;

    std::cout << details.file_name << ":" << details.line_number << ": "
        "Begin " << details.test_name << "\n";
}

void SimpleReporter::fail(const TestDetails& details, const std::string& message)
{
    std::cerr << details.file_name << ":" << details.line_number << ": "
        "ERROR in " << details.test_name << ": " << message << "\n";
}

void SimpleReporter::summary(const Summary& summary)
{
    std::cout << "\n";
    if (summary.num_failed_tests == 0) {
        std::cout << "Success: All "<<summary.num_included_tests<<" tests passed "
            "("<<summary.num_checks<<" checks).\n";
    }
    else {
        std::cerr << "FAILURE: "<<summary.num_failed_tests<<" "
            "out of "<<summary.num_included_tests<<" tests failed "
            "("<<summary.num_failed_checks<<" "
            "out of "<<summary.num_checks<<" checks failed).\n";
    }
    std::cout << "Test time: "<<Timer::format(summary.elapsed_seconds)<<"\n";
    if (summary.num_excluded_tests == 1) {
        std::cout << "\nNote: One test was excluded!\n";
    }
    else if (summary.num_excluded_tests > 1) {
        std::cout << "\nNote: "<<summary.num_excluded_tests<<" tests were excluded!\n";
    }
}


Reporter* create_xml_reporter(std::ostream& out)
{
    return new XmlReporter(out);
}


Filter* create_wildcard_filter(const std::string& filter)
{
    return new WildcardFilter(filter);
}


} // namespace unit_test
} // namespace test_util
} // namespace realm
