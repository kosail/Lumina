// ---------------------------------------------------------------------------
// Unit tests for enrollment image discovery: extension filtering, sorting, and
// graceful handling of a missing directory. Pure filesystem logic, no hardware.
// ---------------------------------------------------------------------------

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "vision/image_list.hpp"

using lumina::vision::isSupportedImageExtension;
using lumina::vision::listImageFiles;

namespace {

std::filesystem::path makeTempDir()
{
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "lumina_image_list_test";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    return dir;
}

void touch(const std::filesystem::path& path)
{
    std::ofstream(path) << "x";  // contents are irrelevant; only the name matters
}

}  // namespace

TEST_CASE("image_list: extension matching is case-insensitive and limited")
{
    CHECK(isSupportedImageExtension(".jpg"));
    CHECK(isSupportedImageExtension(".JPEG"));
    CHECK(isSupportedImageExtension(".Png"));
    CHECK(isSupportedImageExtension(".bmp"));
    CHECK(isSupportedImageExtension(".webp"));
    CHECK_FALSE(isSupportedImageExtension(".txt"));
    CHECK_FALSE(isSupportedImageExtension(""));
}

TEST_CASE("image_list: finds only images, sorted by path")
{
    const std::filesystem::path dir = makeTempDir();
    touch(dir / "b.PNG");
    touch(dir / "a.jpg");
    touch(dir / "notes.txt");
    touch(dir / "c.jpeg");

    const std::vector<std::string> files = listImageFiles(dir.string());
    std::filesystem::remove_all(dir);

    REQUIRE(files.size() == 3);
    // Sorted lexicographically: a.jpg, b.PNG, c.jpeg.
    CHECK(files[0].find("a.jpg") != std::string::npos);
    CHECK(files[1].find("b.PNG") != std::string::npos);
    CHECK(files[2].find("c.jpeg") != std::string::npos);
}

TEST_CASE("image_list: a missing directory yields an empty list")
{
    CHECK(listImageFiles("/nonexistent/lumina_image_list_dir").empty());
}
