#include <algorithm>
#include <array>
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
#include <thread>
#include <tuple>
#include <vector>

#include "cxxopts.hpp"
#include "sha-256.h"

// array long enough to hold sha256 in bytes
using SHA256Hash = std::array<uint8_t, 32>;
// size of file reading in chunks, needs to grow for larger files or might take too long
constexpr size_t CHUNK_SIZE = 64 * 1024;
// number of hex characters used to print/parse a SHA256Hash (2 hex digits per byte)
constexpr size_t HASH_HEX_CHARS = std::tuple_size_v<SHA256Hash> * 2;
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

// prints file hashes to stdout in a flat, sorted, sha256sum-compatible format
// (<hash>  <path>), so stdout can be redirected straight into a manifest file
// only prints files that have been successfully hashed
// @param files: vector of FileInfo containing paths, sizes, and optional hashes
void printSorted(const std::vector<FileInfo>& files) {
    std::vector<const FileInfo*> hashed_files;
    for (const auto& file : files) {
        if (file.hash.has_value()) {
            hashed_files.push_back(&file);
        }
    }

    // sort by path - filesystem traversal order is not guaranteed stable
    std::sort(hashed_files.begin(), hashed_files.end(),
              [](const FileInfo* a, const FileInfo* b) { return a->path < b->path; });

    for (const FileInfo* file_ptr : hashed_files) {
        std::cout << *file_ptr->hash << "  " << file_ptr->path.string() << std::endl;
    }
}

// prints a "=== <title> ===" block to stderr: a "<total_label>: <total>" line,
// followed by one line per {label, count} pair that is nonzero or marked always
struct SummaryLine {
    std::string label;
    size_t count;
    bool always = false;
};
void printSummary(const std::string& title, const std::string& total_label, size_t total,
                  const std::vector<SummaryLine>& lines) {
    std::cerr << "\n=== " << title << " ===" << std::endl;
    std::cerr << total_label << ": " << total << std::endl;
    for (const auto& line : lines) {
        if (line.always || line.count > 0) std::cerr << line.label << ": " << line.count << std::endl;
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
// @param compare: expected {path, hash} pairs parsed from a manifest
// @param malformed: number of unparseable lines skipped while reading the manifest
// @return: false if any file is FAILED or MISSING (NOT IN MANIFEST doesn't count), true otherwise
bool compareAndPrint(const std::vector<FileInfo>& file, const std::vector<ManifestEntry>& compare, size_t malformed) {
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
    for (const auto& f : compare) {
        by_path[normalizePathForCompare(f.path, cwd)].expected = &f;
    }

    size_t ok = 0, failed = 0, missing = 0, untracked = 0;
    for (const auto& [normalized_path, match] : by_path) {
        if (match.expected == nullptr) {
            std::cout << match.actual->path.string() << ": NOT IN MANIFEST" << std::endl;
            ++untracked;
        } else if (match.actual == nullptr) {
            std::cout << match.expected->path.string() << ": MISSING" << std::endl;
            ++missing;
        } else if (match.actual->hash.has_value() && match.actual->hash == match.expected->hash) {
            std::cout << match.expected->path.string() << ": OK" << std::endl;
            ++ok;
        } else {
            std::cout << match.expected->path.string() << ": FAILED" << std::endl;
            ++failed;
        }
    }

    printSummary("Check Summary", "Total checked", ok + failed + missing,
                 {{"OK", ok, true},
                  {"FAILED", failed, false},
                  {"MISSING", missing, false},
                  {"NOT IN MANIFEST", untracked, false},
                  {"Malformed", malformed, false}});

    return failed == 0 && missing == 0;
}

// calculates SHA256 hash by reading file in chunks for memory efficiency
// @param path: Path to the file to hash
// @return: optional containing hash on success, nullopt on error
std::optional<SHA256Hash> calculateFileHash(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::cerr << "Error opening: " << path << std::endl;
        return std::nullopt;
    }

    std::array<uint8_t, CHUNK_SIZE> chunk;

    SHA256Hash hash;
    Sha_256 sha;
    sha_256_init(&sha, hash.data());

    // read file in chunks: handles full chunks, partial last chunk, and exact multiples
    while (file.read(reinterpret_cast<char*>(chunk.data()), CHUNK_SIZE) || file.gcount() > 0) {
        sha_256_write(&sha, chunk.data(), file.gcount());
    }

    // finalize hash computation (safe to call even on error)
    sha_256_close(&sha);

    // check if we exited due to error (not just EOF)
    if (file.bad()) {
        std::cerr << "I/O error reading: " << path << std::endl;
        return std::nullopt;
    }

    return hash;
}

// result of parsing a manifest: entries plus a count of skipped malformed lines
struct ParseResult {
    std::vector<ManifestEntry> entries;
    size_t malformed = 0;
};

// parses a file with compatible format and returns a ParseResult.
// expects lines as written by printSorted: <hex hash><2 spaces><path>
// @param file: the manifest file
// @return: entries with populated paths and hashes (malformed lines are skipped, and counted)
ParseResult parseManifest(std::ifstream& file) {
    ParseResult result;
    std::string line;
    size_t line_num = 0;

    while (std::getline(file, line)) {
        ++line_num;
        // std::getline only strips '\n' - drop a trailing '\r' left by CRLF line endings
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        // need at least HASH_HEX_CHARS hex chars + 2 spaces + 1 char of path
        bool malformed_line =
            line.size() < MIN_LINE_LEN || line[HASH_HEX_CHARS] != ' ' || line[HASH_HEX_CHARS + 1] != ' ';

        SHA256Hash hash;
        for (size_t i = 0; !malformed_line && i < hash.size(); ++i) {
            const char* start = line.data() + i * 2;
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

        if (malformed_line) {
            std::cerr << "Skipping malformed line " << line_num << " in check file" << std::endl;
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
// @param ec: set on failure to open/iterate target_path (e.g. invalid path)
// @return: vector of FileInfo with path and size populated; hash is populated later
std::vector<FileInfo> discoverFiles(const std::string& target_path, const std::vector<std::string>& exclude_dirs,
                                    std::error_code& ec) {
    std::filesystem::recursive_directory_iterator rec_dir_iter(target_path, ec);
    if (ec) return {};

    std::vector<FileInfo> files;
    std::filesystem::recursive_directory_iterator end;
    for (; rec_dir_iter != end; ++rec_dir_iter) {
        const auto& entry = *rec_dir_iter;
        // skip directories whose name matches --exclude
        if (entry.is_directory() && std::find(exclude_dirs.begin(), exclude_dirs.end(),
                                              entry.path().filename().string()) != exclude_dirs.end()) {
            rec_dir_iter.disable_recursion_pending();
            continue;
        }

        // only create hash entries for regular files
        if (entry.is_regular_file()) {
            // hashes will be populated later
            files.push_back({entry.path(), entry.file_size(), std::nullopt});
        }
    }

    return files;
}

// distributes file hashing across multiple threads using size-based load balancing
// sorts files by size (largest first) and assigns to least-loaded threads
// @param files: vector of FileInfo to process (modified in-place with hashes)
// @param num_threads: Maximum number of threads to use (will use fewer if less files)
// @param verbose: if true, prints per-thread progress to stderr
void hashFilesParallel(std::vector<FileInfo>& files, unsigned int num_threads, bool verbose) {
    if (files.empty()) return;
    std::vector<FileInfo*> sorted_files;
    sorted_files.reserve(files.size());
    for (auto& file : files) {
        sorted_files.push_back(&file);
    }

    std::sort(sorted_files.begin(), sorted_files.end(),
              [](const FileInfo* a, const FileInfo* b) { return a->size > b->size; });

    std::vector<std::vector<FileInfo*>> files_per_thread(num_threads);
    std::vector<std::uintmax_t> thread_workload(num_threads, 0);
    for (size_t i = 0; i < sorted_files.size(); i++) {
        auto min_iter = std::min_element(thread_workload.begin(), thread_workload.end());
        size_t min_workload_idx = std::distance(thread_workload.begin(), min_iter);

        files_per_thread[min_workload_idx].push_back(sorted_files[i]);
        thread_workload[min_workload_idx] += sorted_files[i]->size;
    }

    if (verbose) {
        std::cerr << "Hardware concurrency: " << num_threads << " threads" << std::endl;
    }
    // guards the verbose progress prints below - without it, concurrent std::cerr
    // writes from different threads can interleave mid-line into garbled output
    std::mutex log_mutex;

    std::vector<std::thread> threads(num_threads);
    for (size_t i = 0; i < threads.size(); i++) {
        if (verbose) {
            std::lock_guard<std::mutex> lock(log_mutex);
            std::cerr << "Starting thread " << i << "\n";
        }
        threads[i] = std::thread([&files_per_thread, i, verbose, &log_mutex]() {
            for (auto& file : files_per_thread[i]) {
                if (verbose) {
                    std::lock_guard<std::mutex> lock(log_mutex);
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

int main(int argc, char* argv[]) {
    cxxopts::Options options("TreeHash", "Recursive directory SHA-256 calculator with parallel processing");
    options.positional_help("<path-to-directory>");
    options.add_options()("path", "Directory to hash", cxxopts::value<std::string>())(
        "v,verbose", "Print per-thread progress to stderr", cxxopts::value<bool>()->default_value("false"))(
        "x,exclude", "Directory names to skip (comma-separated or repeatable)",
        cxxopts::value<std::vector<std::string>>()->default_value(".git,build"))("h,help", "Print usage")(
        "c,check", "Check against a previously written file", cxxopts::value<std::string>());
    options.parse_positional({"path"});

    cxxopts::ParseResult result;
    try {
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        std::cerr << "Error parsing options: " << e.what() << std::endl;
        return 1;
    }

    if (result.count("help")) {
        std::cout << options.help() << std::endl;
        return 0;
    }

    if (!result.count("path")) {
        std::cerr << options.help() << std::endl;
        return 1;
    }

    const bool do_check = result.count("check") > 0;
    const std::string& target_path = result["path"].as<std::string>();
    const bool verbose = result["verbose"].as<bool>();
    const std::vector<std::string> exclude_dirs = result["exclude"].as<std::vector<std::string>>();

    std::vector<ManifestEntry> files_to_compare;
    size_t malformed = 0;
    if (do_check) {
        std::ifstream check_file(result["check"].as<std::string>());
        if (!check_file) {
            std::cerr << "Error opening check file" << std::endl;
            return 2;
        }
        ParseResult parsed = parseManifest(check_file);
        files_to_compare = std::move(parsed.entries);
        malformed = parsed.malformed;
    }

    std::error_code ec;
    std::vector<FileInfo> files = discoverFiles(target_path, exclude_dirs, ec);
    if (ec) {
        std::cerr << "Error: " << ec.message() << std::endl;
        return 1;
    }

    // don't create more threads than files
    const unsigned int MAX_THREADS =
        std::min(std::thread::hardware_concurrency(), static_cast<unsigned int>(files.size()));
    hashFilesParallel(files, MAX_THREADS, verbose);

    bool check_passed = true;
    if (do_check) {
        check_passed = compareAndPrint(files, files_to_compare, malformed);
    } else {
        // Print the results - stdout carries only the hash manifest, so it can be redirected/piped
        printSorted(files);

        // Print summary statistics to stderr, so it doesn't pollute a redirected manifest
        size_t successful = 0;
        size_t failed = 0;
        for (const auto& file : files) {
            if (file.hash.has_value())
                ++successful;
            else
                ++failed;
        }

        printSummary("Summary", "Total files processed", files.size(),
                     {{"Successfully hashed", successful, true}, {"Failed", failed, false}});
    }

    return check_passed ? 0 : 1;
}
