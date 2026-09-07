#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "cxxopts.hpp"
#include "sha-256.h"

// from sha-256.h, so it can't drift from the library's own size
constexpr size_t SHA256_SIZE = SIZE_OF_SHA_256_HASH;
// array long enough to hold sha256 in bytes
using SHA256Hash = std::array<uint8_t, SHA256_SIZE>;
// cast avoids clang-tidy error, might need to grow for larger files
constexpr size_t CHUNK_SIZE = static_cast<size_t>(64 * 1024);
// number of hex characters used to print/parse a SHA256Hash (2 hex digits per byte)
constexpr size_t HASH_HEX_CHARS = SHA256_SIZE * 2;
// a manifest line is <hash hex chars><2 spaces><at least 1 path char>
constexpr size_t MIN_LINE_LEN = HASH_HEX_CHARS + 2 + 1;

// file info primitives organized
struct FileInfo {
    std::filesystem::path path;
    std::uintmax_t size;
    std::optional<SHA256Hash> hash;
};

// a {path, hash} pair parsed from a manifest - no size, since a manifest doesn't record one
struct ManifestEntry {
    std::filesystem::path path;
    SHA256Hash hash;
};

// result of parsing a manifest: entries plus a count of skipped malformed lines
struct ParseResult {
    std::vector<ManifestEntry> entries;
    size_t malformed = 0;
};

// files found in a directory walk, plus whether/why it had to stop early
struct DiscoverResult {
    std::vector<FileInfo> files;
    bool had_scan_error = false;
    std::error_code ec;
};

// guards every std::cerr write from worker threads (hold for the full print statement)
std::mutex log_mutex;

// SHA256Hash is a raw byte array with no built-in stream support
std::ostream& operator<<(std::ostream& stream, const SHA256Hash& hash) {
    // save current state
    auto old_flags = stream.flags();
    auto old_fill = stream.fill();

    // set formatting for hex and 2 digits
    stream << std::hex << std::setfill('0');

    for (const auto& byte : hash) {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }

    // restore original state
    stream.flags(old_flags);
    stream.fill(old_fill);

    return stream;
}

// prints hashed files sorted, sha256sum-compatible (<hash>  <path>)
void printSorted(const std::vector<FileInfo>& files) {
    std::vector<const FileInfo*> hashed_files;
    hashed_files.reserve(files.size());
    for (const auto& file : files) {
        if (file.hash.has_value()) {
            hashed_files.push_back(&file);
        }
    }

    // sort by path - filesystem traversal order is not guaranteed stable
    std::sort(hashed_files.begin(), hashed_files.end(),
              [](const FileInfo* a, const FileInfo* b) { return a->path < b->path; });

    for (const FileInfo* file_ptr : hashed_files) {
        // already guaranteed to exist but solves clang-tidy error. Don't remove check
        if (file_ptr->hash) {
            std::cout << *file_ptr->hash << "  " << file_ptr->path.string() << '\n';
        }
    }
}

// one summary row: printed only if count is nonzero or always is set
struct SummaryLine {
    std::string label;
    size_t count;
    bool always = false;
};

// prints a "=== <title> ===" block to stderr: a total line, then one line per row
void printSummary(const std::string& title, const std::string& total_label, size_t total,
                  const std::vector<SummaryLine>& lines) {
    std::cerr << "\n=== " << title << " ===" << '\n';
    std::cerr << total_label << ": " << total << '\n';
    for (const auto& line : lines) {
        if (line.always || line.count > 0) {
            std::cerr << line.label << ": " << line.count << '\n';
        }
    }
}

// normalizes a path for comparison: makes it absolute (using the given cwd, no
// filesystem access) then collapses "." / ".." / redundant separators, so
// e.g. "./x" and "/cwd/x" compare equal regardless of how each side's path
// was originally spelled. Doesn't resolve symlinks.
std::string normalizePathForCompare(const std::filesystem::path& p, const std::filesystem::path& cwd) {
    return (p.is_absolute() ? p : cwd / p).lexically_normal().string();
}

// compares freshly computed hashes against an expected manifest and prints
// per-file status to stdout, followed by a summary to stderr. <path>: OK/FAILED/MISSING
// follow the same convention as sha256sum -c; NOT IN MANIFEST is TreeHash's own addition
// for files present on disk but absent from the manifest - real sha256sum -c has no
// equivalent and silently ignores such files
// @param file: freshly hashed files found on disk
// @param manifest: expected {path, hash} pairs, plus the count of unparseable lines skipped while reading them
// @return: false if any file is FAILED or MISSING (NOT IN MANIFEST doesn't count), true otherwise
bool compareAndPrint(const std::vector<FileInfo>& file, const ParseResult& manifest) {
    struct Match {
        const FileInfo* actual = nullptr;
        const ManifestEntry* expected = nullptr;
    };

    // resolved once - every path below is normalized against the same cwd
    const auto cwd = std::filesystem::current_path();

    // one entry per distinct normalized path, sorted by that path for stable,
    // predictable output - matches printSorted's ordering
    std::map<std::string, Match> by_path;
    for (const auto& f : file) {
        by_path[normalizePathForCompare(f.path, cwd)].actual = &f;
    }
    for (const auto& f : manifest.entries) {
        by_path[normalizePathForCompare(f.path, cwd)].expected = &f;
    }

    size_t ok = 0;
    size_t failed = 0;
    size_t missing = 0;
    size_t untracked = 0;
    for (const auto& [normalized_path, match] : by_path) {
        if (match.expected == nullptr) {
            std::cout << match.actual->path.string() << ": NOT IN MANIFEST" << '\n';
            ++untracked;
        } else if (match.actual == nullptr) {
            std::cout << match.expected->path.string() << ": MISSING" << '\n';
            ++missing;
        } else if (match.actual->hash.has_value() && match.actual->hash == match.expected->hash) {
            std::cout << match.expected->path.string() << ": OK" << '\n';
            ++ok;
        } else {
            std::cout << match.expected->path.string() << ": FAILED" << '\n';
            ++failed;
        }
    }

    printSummary("Check Summary", "Total checked", ok + failed + missing,
                 {{"OK", ok, true},
                  {"FAILED", failed},
                  {"MISSING", missing},
                  {"NOT IN MANIFEST", untracked},
                  {"Malformed", manifest.malformed}});

    return failed == 0 && missing == 0;
}

// calculates SHA256 hash by reading file in chunks for memory efficiency
// @param path: Path to the file to hash
// @return: optional containing hash on success, nullopt on error
std::optional<SHA256Hash> calculateFileHash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::scoped_lock<std::mutex> lock(log_mutex);
        std::cerr << "Error opening: " << path << '\n';
        return std::nullopt;
    }

    std::array<uint8_t, CHUNK_SIZE> chunk;

    SHA256Hash hash;
    Sha_256 sha;
    sha_256_init(&sha, hash.data());

    // cleared first: the standard doesn't guarantee a failed read sets errno, so a
    // stale nonzero value from something unrelated must not be reported as the cause
    errno = 0;
    // read file in chunks: handles full chunks, partial last chunk, and exact multiples
    while (file.read(reinterpret_cast<char*>(chunk.data()), CHUNK_SIZE) || file.gcount() > 0) {
        sha_256_write(&sha, chunk.data(), file.gcount());
    }
    // saved right after the loop, before anything else can clobber it
    const int read_errno = errno;

    // finalize hash computation (safe to call even on error)
    sha_256_close(&sha);

    // check if we exited due to error (not just EOF)
    if (file.bad()) {
        std::scoped_lock<std::mutex> lock(log_mutex);
        std::cerr << "I/O error reading: " << path;
        if (read_errno != 0) {
            std::cerr << ": " << std::generic_category().message(read_errno);
        }
        std::cerr << '\n';
        return std::nullopt;
    }

    return hash;
}

// opens and parses a file with compatible format.
// expects lines as written by printSorted: <hex hash><2 spaces><path>
// @param path: the manifest file to read
// @return: entries with populated paths and hashes (malformed lines are skipped, and
//          counted), or nullopt if the file couldn't be opened
std::optional<ParseResult> parseManifest(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return std::nullopt;
    }

    ParseResult result;
    std::string line;
    size_t line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;
        // std::getline only strips '\n' - drop a trailing '\r' left by CRLF line endings
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        // need at least HASH_HEX_CHARS hex chars + 2 spaces + 1 char of path
        bool malformed_line =
            line.size() < MIN_LINE_LEN || line[HASH_HEX_CHARS] != ' ' || line[HASH_HEX_CHARS + 1] != ' ';

        // only decode once the shape check above has passed
        SHA256Hash hash;
        if (!malformed_line) {
            for (size_t i = 0; i < hash.size(); ++i) {
                const char* start = line.data() + (i * 2);
                unsigned int byte = 0;
                const auto [ptr, ec] = std::from_chars(start, start + 2, byte, 16);
                // reject anything that doesn't consume exactly 2 hex digits
                // (from_chars silently accepts a partial parse, e.g. "1g" -> 1)
                if (ec != std::errc() || ptr != start + 2) {
                    malformed_line = true;
                    break;
                }
                hash[i] = static_cast<uint8_t>(byte);
            }
        }

        if (malformed_line) {
            std::cerr << "Skipping malformed line " << line_num << " in check file" << '\n';
            ++result.malformed;
            continue;
        }

        result.entries.push_back({std::filesystem::path(line.cbegin() + (HASH_HEX_CHARS + 2), line.cend()), hash});
    }

    return result;
}

// recursively discovers regular files under target_path, skipping directories whose
// name matches exclude_dirs (not recursed into)
// @param target_path: root directory to walk
// @param exclude_dirs: directory names to skip
// @return: files with path and size populated (hash populated later); result.ec is
//        set on failure to open target_path or if the walk had to stop early - check
//        it even when result.files is non-empty, since that means the tree wasn't
//        fully covered
DiscoverResult discoverFiles(const std::string& target_path, const std::vector<std::string>& exclude_dirs) {
    DiscoverResult result;
    std::filesystem::recursive_directory_iterator rec_dir_iter(target_path, result.ec);
    if (result.ec) {
        return result;
    }

    std::filesystem::recursive_directory_iterator end;
    while (rec_dir_iter != end) {
        const auto& entry = *rec_dir_iter;
        std::error_code entry_ec;
        const auto status = entry.status(entry_ec);

        if (!entry_ec && status.type() == std::filesystem::file_type::directory) {
            // skip directories whose name matches --exclude
            if (std::find(exclude_dirs.begin(), exclude_dirs.end(), entry.path().filename().string()) !=
                exclude_dirs.end()) {
                rec_dir_iter.disable_recursion_pending();
            }
        } else if (!entry_ec && status.type() == std::filesystem::file_type::regular) {
            const auto size = entry.file_size(entry_ec);
            if (!entry_ec) {
                // hash will be populated later
                result.files.push_back({entry.path(), size, std::nullopt});
            }
        }

        if (entry_ec) {
            result.had_scan_error = true;
            std::scoped_lock<std::mutex> lock(log_mutex);
            std::cerr << "Skipping " << entry.path() << ": " << entry_ec.message() << '\n';
        }

        // non-throwing increment: a failure here sets ec instead of throwing
        rec_dir_iter.increment(result.ec);
        if (result.ec) {
            std::scoped_lock<std::mutex> lock(log_mutex);
            std::cerr << "Stopped directory walk early: " << result.ec.message() << '\n';
        }
    }

    return result;
}

// distributes file hashing across multiple threads using size-based load balancing
// sorts files by size (largest first) and assigns to least-loaded threads
// @param files: vector of FileInfo to process (modified in-place with hashes)
// @param verbose: if true, prints per-thread progress to stderr
void hashFilesParallel(std::vector<FileInfo>& files, bool verbose) {
    if (files.empty()) {
        return;
    }

    // hardware_concurrency() can return 0; fall back to 1. Also cap at file count.
    const unsigned int hw_concurrency = std::max(std::thread::hardware_concurrency(), 1U);
    const unsigned int num_threads = std::min(hw_concurrency, static_cast<unsigned int>(files.size()));

    std::vector<FileInfo*> sorted_files;
    sorted_files.reserve(files.size());
    for (auto& file : files) {
        sorted_files.push_back(&file);
    }

    std::sort(sorted_files.begin(), sorted_files.end(),
              [](const FileInfo* a, const FileInfo* b) { return a->size > b->size; });

    std::vector<std::vector<FileInfo*>> files_per_thread(num_threads);
    std::vector<std::uintmax_t> thread_workload(num_threads, 0);
    for (auto& sorted_file : sorted_files) {
        auto min_iter = std::min_element(thread_workload.begin(), thread_workload.end());
        size_t min_workload_idx = std::distance(thread_workload.begin(), min_iter);

        files_per_thread[min_workload_idx].push_back(sorted_file);
        thread_workload[min_workload_idx] += sorted_file->size;
    }

    if (verbose) {
        std::cerr << "Hardware concurrency: " << num_threads << " threads" << '\n';
    }

    std::vector<std::thread> threads(num_threads);
    for (size_t i = 0; i < threads.size(); i++) {
        if (verbose) {
            std::scoped_lock<std::mutex> lock(log_mutex);
            std::cerr << "Starting thread " << i << "\n";
        }
        threads[i] = std::thread([&files_per_thread, i, verbose]() {
            for (auto& file : files_per_thread[i]) {
                if (verbose) {
                    std::scoped_lock<std::mutex> lock(log_mutex);
                    std::cerr << "Thread " << i << " calculating hash for " << file->path.string() << "\n";
                }
                file->hash = calculateFileHash(file->path);
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

// prints the default (non-check) report: hash manifest to stdout, summary to stderr
// @return: false if any file failed to hash, true otherwise
bool printReport(const std::vector<FileInfo>& files) {
    // stdout carries only the hash manifest, so it can be redirected/piped
    printSorted(files);

    const auto successful = static_cast<size_t>(
        std::count_if(files.begin(), files.end(), [](const FileInfo& file) { return file.hash.has_value(); }));

    // summary goes to stderr, so it doesn't pollute a redirected manifest
    printSummary("Summary", "Total files processed", files.size(),
                 {{"Successfully hashed", successful, true}, {"Failed", files.size() - successful}});

    return successful == files.size();
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("TreeHash", "Recursive directory SHA-256 calculator with parallel processing");
    options.positional_help("<path-to-directory>");
    // catch avoids clang-tidy error
    try {
        options.add_options()("path", "Directory to hash", cxxopts::value<std::string>())(
            "v,verbose", "Print per-thread progress to stderr", cxxopts::value<bool>()->default_value("false"))(
            "x,exclude", "Directory names to skip (comma-separated or repeatable)",
            cxxopts::value<std::vector<std::string>>()->default_value(".git,build"))("h,help", "Print usage")(
            "c,check", "Check against a previously written manifest", cxxopts::value<std::string>());
    } catch (const std::exception& e) {
        std::cerr << "Developer error on literal strings: " << e.what() << '\n';
        return 1;
    }
    options.parse_positional({"path"});

    cxxopts::ParseResult result;
    try {
        result = options.parse(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << '\n';
        return 1;
    }

    if (result.contains("help")) {
        std::cout << options.help() << '\n';
        return 0;
    }

    if (!result.contains("path")) {
        std::cerr << options.help() << '\n';
        return 1;
    }

    const bool do_check = result.contains("check");
    std::string target_path;
    bool verbose = false;
    std::vector<std::string> exclude_dirs;
    std::string check_file_path;
    try {
        target_path = result["path"].as<std::string>();
        verbose = result["verbose"].as<bool>();
        exclude_dirs = result["exclude"].as<std::vector<std::string>>();
        if (do_check) {
            check_file_path = result["check"].as<std::string>();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error reading options: " << e.what() << '\n';
        return 1;
    }

    ParseResult manifest;
    if (do_check) {
        std::optional<ParseResult> parsed = parseManifest(check_file_path);
        if (!parsed) {
            std::cerr << "Error opening check file" << '\n';
            return 2;
        }
        manifest = std::move(*parsed);
    }

    DiscoverResult discovered = discoverFiles(target_path, exclude_dirs);
    if (discovered.ec && discovered.files.empty()) {
        std::cerr << "Error: " << discovered.ec.message() << '\n';
        return 1;
    }
    // an incomplete tree must fail the run even if every found file hashes fine
    const bool walk_incomplete = static_cast<bool>(discovered.ec) || discovered.had_scan_error;

    hashFilesParallel(discovered.files, verbose);

    bool check_passed = true;
    if (do_check) {
        check_passed = compareAndPrint(discovered.files, manifest);
    } else {
        check_passed = printReport(discovered.files);
    }

    return (check_passed && !walk_incomplete) ? 0 : 1;
}
