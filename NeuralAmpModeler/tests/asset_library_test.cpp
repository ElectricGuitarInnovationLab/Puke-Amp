#include "../AssetLibrary.h"

#include <cassert>
#include <fstream>
#include <future>
#include <iostream>

using namespace nam_library;

static void Write(const fs::path& path, const std::string& contents)
{
  fs::create_directories(path.parent_path());
  std::ofstream(path) << contents;
}

static std::string Read(const fs::path& path)
{
  std::ifstream input(path);
  return std::string(std::istreambuf_iterator<char>(input), {});
}

int main()
{
  const auto root = fs::temp_directory_path() /
                    ("nam-library-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  try
  {
    const auto source = root / "Collection";
    AssetLibrary library(root / "User" / "Library");
    const auto validate = [](const fs::path& file) {
      if (Read(file) != "valid")
        throw std::runtime_error("Invalid test asset");
    };
    Write(source / "Nested" / "amp.NAM", "valid");
    Write(source / "bad.nam", "bad");
    Write(source / "notes.txt", "notes");
    auto result = library.Import(source, "Amps", "nam", validate);
    assert(result.imported == 1 && result.skipped == 1 && result.errors.size() == 1);
    const auto imported = library.Category("Amps") / "Collection" / "Nested" / "amp.nam";
    assert(Read(imported) == "valid");
    assert(!fs::exists(library.Category("Amps") / "Collection" / "bad.nam"));
    assert(Read(source / "Nested" / "amp.NAM") == "valid");

    // Same names never overwrite either an existing asset or another importer.
    Write(library.Category("Amps") / "amp.nam", "existing");
    auto first = std::async(std::launch::async, [&] {
      return library.Import(source / "Nested" / "amp.NAM", "Amps", "nam", validate);
    });
    auto second = std::async(std::launch::async, [&] {
      return library.Import(source / "Nested" / "amp.NAM", "Amps", "nam", validate);
    });
    assert(first.get().imported == 1 && second.get().imported == 1);
    assert(Read(library.Category("Amps") / "amp.nam") == "existing");
    assert(Read(library.Category("Amps") / "amp (2).nam") == "valid");
    assert(Read(library.Category("Amps") / "amp (3).nam") == "valid");

    // Persistence and relocation do not depend on a live UI or an index file.
    const auto encoded = library.Encode(imported);
    assert(encoded == "library://Amps/Collection/Nested/amp.nam");
    AssetLibrary reopened(root / "User" / "Library");
    assert(reopened.Resolve(encoded) == imported);
    fs::rename(root / "User", root / "Moved");
    AssetLibrary moved(root / "Moved" / "Library");
    assert(Read(moved.Resolve(encoded)) == "valid");
    assert(moved.Resolve(PathString(source / "bad.nam")) == source / "bad.nam");
    assert(moved.Encode(source / "bad.nam") == PathString(source / "bad.nam"));
    bool rejected = false;
    try { moved.Resolve("library://../../outside.nam"); }
    catch (const std::runtime_error&) { rejected = true; }
    assert(rejected);
    assert(moved.Import(moved.Category("Amps"), "Amps", "nam", validate).imported == 0);
    assert(moved.Import(root, "Amps", "nam", validate).imported == 0);
    assert(!moved.Import(root / "missing.nam", "Amps", "nam", validate).errors.empty());

    // Unicode filenames and category isolation.
    const auto unicode = source / fs::u8path("Café.nam");
    Write(unicode, "valid");
    assert(moved.Import(unicode, "Pedals", "nam", validate).imported == 1);
    assert(Read(moved.Category("Pedals") / unicode.filename()) == "valid");
    assert(!fs::exists(moved.Category("Amps") / unicode.filename()));
    Write(source / "cab.WAV", "valid");
    assert(moved.Import(source / "cab.WAV", "IRs", "wav", validate).imported == 1);
    assert(Read(moved.Category("IRs") / "cab.wav") == "valid");
    for (const auto& entry : fs::recursive_directory_iterator(root / "Moved"))
      assert(PathString(entry.path().filename()).rfind(".import-", 0) != 0);
  }
  catch (...)
  {
    fs::remove_all(root);
    throw;
  }
  fs::remove_all(root);
  std::cout << "Asset library tests passed\n";
}
