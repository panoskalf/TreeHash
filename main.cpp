#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "cxxopts.hpp"
#include "sha-256.h"

// array long enough to hold sha256 in bytes
using SHA256Hash = std::array<uint8_t, 32>;
// size of file reading in chunks, needs to grow for larger files or might take too long
constexpr size_t CHUNK_SIZE = 64 * 1024;

// file info primitives organized
struct FileInfo {
    std::filesystem::path path;
    std::uintmax_t size;
    std::optional<SHA256Hash> hash;
};

// overload operator to more easily print with std::cout
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
    struct Sha_256 sha;
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

// distributes file hashing across multiple threads using size-based load balancing
// sorts files by size (largest first) and assigns to least-loaded threads
// @param files: vector of FileInfo to process (modified in-place with hashes)
// @param num_threads: Maximum number of threads to use (will use fewer if less files)
// @param verbose: if true, prints per-thread progress to stderr
void hashFilesParallel(std::vector<FileInfo>& files, unsigned int num_threads, bool verbose) {
    if (files.empty()) return;
    // sort files to better distribute - bigger files first
    std::vector<FileInfo*> sorted_files;
    for (size_t i = 0; i < files.size(); ++i) {
        sorted_files.push_back(&files[i]);
    }

    std::sort(sorted_files.begin(), sorted_files.end(),
              [](const FileInfo* a, const FileInfo* b) { return a->size > b->size; });

    // distribute files to threads
    std::vector<std::vector<FileInfo*>> files_per_thread(num_threads);
    std::vector<std::uintmax_t> thread_workload(num_threads, 0);
    for (size_t i = 0; i < sorted_files.size(); i++) {
        // find thread with minimum workload
        auto min_iter = std::min_element(thread_workload.begin(), thread_workload.end());
        size_t min_workload_idx = std::distance(thread_workload.begin(), min_iter);

        files_per_thread[min_workload_idx].push_back(sorted_files[i]);
        thread_workload[min_workload_idx] += sorted_files[i]->size;
    }

    if (verbose) {
        std::cerr << "Hardware concurrency: " << num_threads << " threads" << std::endl;
    }
    // start the threads
    std::vector<std::thread> threads(num_threads);
    for (size_t i = 0; i < threads.size(); i++) {
        if (verbose) {
            // threads will be running when this prints so it has to be atomic too
            std::cerr << "Starting thread " + std::to_string(i) + "\n";
        }
        threads[i] = std::thread([&files_per_thread, i, verbose]() {
            for (auto& file : files_per_thread[i]) {
                if (verbose) {
                    // atomic
                    std::cerr << "Thread " + std::to_string(i) + " calculating hash for " + file->path.string() + "\n";
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
        cxxopts::value<std::vector<std::string>>()->default_value(".git,build"))("h,help", "Print usage");
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

    const std::string& target_path = result["path"].as<std::string>();
    const bool verbose = result["verbose"].as<bool>();
    const std::vector<std::string> exclude_dirs = result["exclude"].as<std::vector<std::string>>();

    std::error_code ec;
    // to iterate through the folder recursively
    std::filesystem::recursive_directory_iterator rec_dir_iter(target_path, ec);
    if (ec) {
        std::cerr << "Error: " << ec.message() << std::endl;
        return 1;
    }

    //
    std::vector<FileInfo> files;

    // read the file info data recursively
    for (const auto& entry : rec_dir_iter) {
        // skip directories whose name matches --exclude
        if (entry.is_directory() && std::find(exclude_dirs.begin(), exclude_dirs.end(),
                                              entry.path().filename().string()) != exclude_dirs.end()) {
            rec_dir_iter.disable_recursion_pending();
            continue;
        }

        // only create hash entries for regular files
        if (entry.is_regular_file()) {
            // hashes will be populated later
            files.push_back({entry.path(), std::filesystem::file_size(entry), std::nullopt});
        }
    }

    // don't create more threads than files
    const unsigned int MAX_THREADS =
        std::min(std::thread::hardware_concurrency(), static_cast<unsigned int>(files.size()));
    hashFilesParallel(files, MAX_THREADS, verbose);

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

    std::cerr << "\n=== Summary ===" << std::endl;
    std::cerr << "Total files processed: " << files.size() << std::endl;
    std::cerr << "Successfully hashed: " << successful << std::endl;
    if (failed > 0) {
        std::cerr << "Failed: " << failed << std::endl;
    }

    return 0;
}
