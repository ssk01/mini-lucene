// End-to-end test that drives the user-facing demo binaries
// (`index_files`, `search_files`, `delete_files`) as subprocesses,
// exactly the way a human user would invoke them.
//
// Rationale: every previous test in this repo exercises the C++ API
// directly. That misses an entire class of failures where the demos
// import the wrong header, mis-wire QueryParser, or silently regress
// when an "internal" API contract changes (e.g. when QueryParser
// started throwing on unknown fields, search_files broke immediately
// for `path:foo` but no test noticed).
//
// Oracle: scenario invariants on the demo's STDOUT — what the user
// sees on screen. Not Java byte-compat (that would need a separate
// fixture); just "the demo does what its README promises".

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

// Resolve the bazel-built binary by relative runfiles path. cc_binary
// outputs land in bazel-bin/<name>, and runfiles for cc_tests put the
// data deps under a known parent. We bypass runfiles entirely by
// jumping up from $TEST_SRCDIR to the bazel-bin sibling, which is the
// stable convention for bazel test wrappers.
std::string FindDemoBin(const std::string& name) {
    // 1) `$TEST_SRCDIR/_main/<name>` (bazel-test layout, MODULE.bazel)
    if (const char* srcdir = std::getenv("TEST_SRCDIR")) {
        fs::path p1 = fs::path(srcdir) / "_main" / name;
        if (fs::exists(p1)) return p1.string();
        // older layout
        fs::path p2 = fs::path(srcdir) / "__main__" / name;
        if (fs::exists(p2)) return p2.string();
    }
    // 2) Fallback to `bazel-bin/<name>` relative to the workspace.
    if (const char* wsdir = std::getenv("BUILD_WORKSPACE_DIRECTORY")) {
        fs::path p = fs::path(wsdir) / "bazel-bin" / name;
        if (fs::exists(p)) return p.string();
    }
    return name;  // last resort: rely on PATH (will fail explicitly)
}

struct ProcResult {
    int  exit_code;
    std::string stdout_text;
};

// Run `cmd` (already shell-escaped); pipe `stdin_text` to its stdin if
// non-empty; capture stdout. Stderr is folded in via `2>&1` in the
// caller's command string.
ProcResult RunProc(const std::string& cmd, const std::string& stdin_text = "") {
    std::string full = cmd;
    if (!stdin_text.empty()) {
        // popen with "w" or "r" only — we need both. Use a temp input file.
        std::string in_path = (fs::temp_directory_path() /
                               "mldemo_e2e_stdin.txt").string();
        {
            std::ofstream f(in_path);
            f << stdin_text;
        }
        full = "cat \"" + in_path + "\" | " + cmd;
    }
    full += " 2>&1";

    FILE* fp = popen(full.c_str(), "r");
    if (!fp) return {-1, "popen failed"};
    std::ostringstream out;
    std::array<char, 4096> buf{};
    while (size_t n = std::fread(buf.data(), 1, buf.size(), fp)) {
        out.write(buf.data(), static_cast<std::streamsize>(n));
    }
    int rc = pclose(fp);
    // Normalize WEXITSTATUS to plain exit code.
    int exit_code = (rc == -1) ? -1 : ((rc & 0xff00) >> 8);
    return {exit_code, out.str()};
}

bool Contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Build a 3-doc corpus deterministic across runs so output assertions
// are stable.
fs::path SetupCorpus() {
    auto root = fs::temp_directory_path() / "mldemo_e2e_corpus";
    fs::remove_all(root);
    fs::create_directories(root);

    auto write = [&](const std::string& name, const std::string& body) {
        std::ofstream f(root / name);
        f << body;
    };
    write("alpha.txt", "The quick brown fox jumps over the lazy dog.");
    write("beta.txt",  "Lucene is a free text search engine library.");
    write("gamma.txt", "Hello World from mini-lucene C++ port.");
    return root;
}

}  // namespace

// =============================================================================
// E2E 1: `index_files <corpus>` builds an index that `search_files` can read.
// =============================================================================
TEST(DemoE2E, IndexThenSearchBareTerm) {
    auto corpus = SetupCorpus();
    auto cwd_before = fs::current_path();
    auto work = fs::temp_directory_path() / "mldemo_e2e_work";
    fs::remove_all(work);
    fs::create_directories(work);
    fs::current_path(work);  // demos write `./index` relative to cwd

    auto index_bin  = FindDemoBin("index_files");
    auto search_bin = FindDemoBin("search_files");

    auto idx = RunProc("\"" + index_bin + "\" \"" + corpus.string() + "\"");
    ASSERT_EQ(idx.exit_code, 0) << idx.stdout_text;
    EXPECT_TRUE(Contains(idx.stdout_text, "adding "))
        << "indexer should announce each file; got:\n" << idx.stdout_text;
    EXPECT_TRUE(Contains(idx.stdout_text, "total milliseconds"))
        << "indexer should print timing summary";

    // Sanity: index dir exists with at least segments + a field info file.
    EXPECT_TRUE(fs::exists(work / "index" / "segments"))
        << "index/segments must exist after index_files";

    // Search a unique term from alpha.txt.
    auto srch = RunProc("\"" + search_bin + "\"", "fox\n");
    ASSERT_EQ(srch.exit_code, 0) << srch.stdout_text;
    EXPECT_TRUE(Contains(srch.stdout_text, "Searching for: contents:fox"))
        << "ToString should display the real field name 'contents', not "
           "the legacy hardcoded 'body:'. Got:\n" << srch.stdout_text;
    EXPECT_TRUE(Contains(srch.stdout_text, "1 total matching documents"))
        << "expected exactly 1 hit for 'fox' (alpha.txt). Got:\n"
        << srch.stdout_text;
    EXPECT_TRUE(Contains(srch.stdout_text, "alpha.txt"))
        << "hit should report the alpha.txt path; got:\n" << srch.stdout_text;

    fs::current_path(cwd_before);
}

// =============================================================================
// E2E 2: cross-field `path:` query must NOT crash (regression for the
// QueryParser field-discard hardening that previously broke this demo).
// =============================================================================
TEST(DemoE2E, CrossFieldQueryDoesNotCrash) {
    auto corpus = SetupCorpus();
    auto cwd_before = fs::current_path();
    auto work = fs::temp_directory_path() / "mldemo_e2e_work2";
    fs::remove_all(work);
    fs::create_directories(work);
    fs::current_path(work);

    auto index_bin  = FindDemoBin("index_files");
    auto search_bin = FindDemoBin("search_files");

    ASSERT_EQ(RunProc("\"" + index_bin + "\" \"" + corpus.string() + "\"").exit_code, 0);

    auto srch = RunProc("\"" + search_bin + "\"", "path:alpha\n");
    ASSERT_EQ(srch.exit_code, 0) << srch.stdout_text;

    // Must not surface the "no FieldResolver" runtime_error — that's the
    // exact regression we are guarding against.
    EXPECT_FALSE(Contains(srch.stdout_text, "no FieldResolver"))
        << "search_files must wire UseFieldInfos; got the raw error:\n"
        << srch.stdout_text;
    EXPECT_FALSE(Contains(srch.stdout_text, "runtime_error"))
        << "no exception should escape; got:\n" << srch.stdout_text;

    // And the query must actually be field-scoped.
    EXPECT_TRUE(Contains(srch.stdout_text, "Searching for: path:"))
        << "ToString should report 'path:' field; got:\n" << srch.stdout_text;

    fs::current_path(cwd_before);
}

// =============================================================================
// E2E 3: AND / OR / NOT keyword path through the demo.
// =============================================================================
TEST(DemoE2E, BooleanKeywordsThroughDemo) {
    auto corpus = SetupCorpus();
    auto cwd_before = fs::current_path();
    auto work = fs::temp_directory_path() / "mldemo_e2e_work3";
    fs::remove_all(work);
    fs::create_directories(work);
    fs::current_path(work);

    auto index_bin  = FindDemoBin("index_files");
    auto search_bin = FindDemoBin("search_files");

    ASSERT_EQ(RunProc("\"" + index_bin + "\" \"" + corpus.string() + "\"").exit_code, 0);

    // "lucene AND fox" requires both terms; nothing in our corpus has both
    // (alpha has fox, beta+gamma have lucene). Expect 0 hits.
    auto srch = RunProc("\"" + search_bin + "\"", "lucene AND fox\n");
    ASSERT_EQ(srch.exit_code, 0) << srch.stdout_text;
    EXPECT_TRUE(Contains(srch.stdout_text, "0 total matching documents"))
        << "no doc has both 'lucene' and 'fox'; got:\n" << srch.stdout_text;

    // "lucene OR fox" should match alpha + beta + gamma = 3 docs (gamma
    // has 'mini-lucene' which tokenizes to 'mini' + 'lucene').
    srch = RunProc("\"" + search_bin + "\"", "lucene OR fox\n");
    ASSERT_EQ(srch.exit_code, 0) << srch.stdout_text;
    EXPECT_TRUE(Contains(srch.stdout_text, "3 total matching documents"))
        << "OR over the corpus must hit 3 docs; got:\n" << srch.stdout_text;

    fs::current_path(cwd_before);
}

// =============================================================================
// E2E 4: `delete_files` marks docs in the index; subsequent `search_files`
// must reflect the deletion.
// =============================================================================
TEST(DemoE2E, DeleteFilesRemovesHits) {
    auto corpus = SetupCorpus();
    auto cwd_before = fs::current_path();
    auto work = fs::temp_directory_path() / "mldemo_e2e_work4";
    fs::remove_all(work);
    fs::create_directories(work);
    fs::current_path(work);

    auto index_bin  = FindDemoBin("index_files");
    auto search_bin = FindDemoBin("search_files");
    auto delete_bin = FindDemoBin("delete_files");

    ASSERT_EQ(RunProc("\"" + index_bin + "\" \"" + corpus.string() + "\"").exit_code, 0);

    // Baseline: 'fox' hits 1.
    auto before = RunProc("\"" + search_bin + "\"", "fox\n");
    ASSERT_EQ(before.exit_code, 0);
    ASSERT_TRUE(Contains(before.stdout_text, "1 total matching documents"))
        << "baseline expected 1 hit; got:\n" << before.stdout_text;

    // Delete every doc in the first segment.
    auto del = RunProc("\"" + delete_bin + "\" index");
    ASSERT_EQ(del.exit_code, 0) << del.stdout_text;

    auto after = RunProc("\"" + search_bin + "\"", "fox\n");
    ASSERT_EQ(after.exit_code, 0);
    EXPECT_TRUE(Contains(after.stdout_text, "0 total matching documents"))
        << "after delete_files, 'fox' must hit 0 docs; got:\n"
        << after.stdout_text;

    fs::current_path(cwd_before);
}
