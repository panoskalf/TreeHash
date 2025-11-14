#include <iostream>
#include <filesystem>
#include <array>
#include <vector>
#include <algorithm>
#include <fstream>
#include <map>
#include <unordered_map>
#include <iomanip>
#include <optional>
#include "sha-256.h"

// Array long enough to hold sha256 in bytes
using SHA256Hash = std::array<uint8_t, 32>;
// Size of file reading in chunks, needs to grow for larger files or might take too long
constexpr size_t CHUNK_SIZE = 64 * 1024;

std::optional<SHA256Hash> calculateFileHash(const std::filesystem::path &path)
{
    std::error_code size_ec;
    const auto file_size = std::filesystem::file_size(path, size_ec);
    if (size_ec)
    {
        std::cerr << "Cannot get size of: " << path << std::endl;
        return std::nullopt;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error reading: " << path << std::endl;
        return std::nullopt;
    }

    std::array<uint8_t, CHUNK_SIZE> chunk;

    SHA256Hash hash;
    struct Sha_256 sha;
    sha_256_init(&sha, hash.data());
    while (file.read(reinterpret_cast<char *>(chunk.data()), CHUNK_SIZE) || file.gcount() > 0)
    {
        sha_256_write(&sha, chunk.data(), file.gcount());
    }

    sha_256_close(&sha);

    return hash;
}

std::ostream &operator<<(std::ostream &stream, const SHA256Hash &hash)
{
    // Save current state
    auto old_flags = stream.flags();
    auto old_fill = stream.fill();

    // Set formatting for hex and 2 digits
    stream << std::hex << std::setfill('0');

    for (const auto &byte : hash)
    {
        stream << std::setw(2) << static_cast<unsigned int>(byte);
    }

    // Restore original state
    stream.flags(old_flags);
    stream.fill(old_fill);

    return stream;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        std::cout << "Usage: folder <path-to-folder>" << std::endl;
        return 1;
    }

    size_t directories = 0, files = 0;

    std::error_code ec;
    // to iterate through the folder recursively
    std::filesystem::recursive_directory_iterator iter(argv[1], ec);
    if (ec)
    {
        std::cerr << "Error: " << ec.message() << std::endl;
        return 1;
    }

    // data struture to assosiate directories with filenames and hashes
    std::unordered_map<std::filesystem::path,
                       std::map<std::filesystem::path, SHA256Hash>>
        dir_hashes;

    // populate the data structure
    for (const auto &entry : iter)
    {
        // skip these directories
        if (entry.is_directory() &&
            (entry.path().filename() == ".git" ||
             entry.path().filename() == "build"))
        {
            iter.disable_recursion_pending();
            continue;
        }

        // count files and directories
        entry.is_directory() ? ++directories : ++files;

        // Only create hash entries for regular files
        if (entry.is_regular_file())
        {
            const std::filesystem::path dir_entry = entry.path().parent_path();
            const std::filesystem::path file_entry = entry.path();

            auto hash = calculateFileHash(file_entry);
            if (hash)
            {
                dir_hashes[dir_entry][file_entry] = *hash;
            }
        }
    }

    // Print the results
    for (const auto &[dir_path, file_hashes] : dir_hashes)
    {
        std::cout << "Directory: " << dir_path.string() << ": " << std::endl;

        for (const auto &[file_path, hash] : file_hashes)
        {
            std::cout << "  " << file_path.filename().string() << " " << hash << std::endl;
        }
    }

    std::cout << "Total directories: " << directories << ", Total files: " << files << std::endl;

    return 0;
}
