#pragma once

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nam_library
{
namespace fs = std::filesystem;

// C++17 and C++20 use different character types for u8string().
inline std::string PathString(const fs::path& path, bool generic = false)
{
  const auto bytes = generic ? path.generic_u8string() : path.u8string();
  return std::string(bytes.begin(), bytes.end());
}

struct ImportResult
{
  size_t imported = 0;
  size_t skipped = 0;
  std::vector<std::string> errors;
};

// Independent of UI and DSP state. Call filesystem operations off the audio thread.
class AssetLibrary
{
public:
  using Validator = std::function<void(const fs::path&)>;

  explicit AssetLibrary(fs::path root) : mRoot(std::move(root)) {}

  fs::path Category(const std::string& category) const { return mRoot / category; }

  static bool IsWithin(const fs::path& path, const fs::path& root)
  {
    const auto relative = fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(root));
    return !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
  }

  std::string Encode(const fs::path& path) const
  {
    if (!IsWithin(path, mRoot))
      return PathString(path);
    return "library://" + PathString(fs::weakly_canonical(path).lexically_relative(fs::weakly_canonical(mRoot)), true);
  }

  fs::path Resolve(const std::string& reference) const
  {
    constexpr const char* prefix = "library://";
    if (reference.rfind(prefix, 0) != 0)
      return fs::u8path(reference);
    const auto relative = fs::u8path(reference.substr(std::char_traits<char>::length(prefix)));
    if (relative.empty() || relative.is_absolute() || relative.has_root_name())
      throw std::runtime_error("Invalid library asset path");
    const auto resolved = mRoot / relative;
    if (!IsWithin(resolved, mRoot))
      throw std::runtime_error("Library asset path is outside the library");
    return resolved.lexically_normal();
  }

  ImportResult Import(const fs::path& source, const std::string& category, const std::string& extension,
                      const Validator& validate) const
  {
    ImportResult result;
    try
    {
      const auto destination = Category(category);
      if (IsWithin(source, mRoot))
        throw std::runtime_error("These files are already in the library.");
      const bool directory = fs::is_directory(source);
      // Reject ancestors as well, so an import can never copy its own output.
      if (directory && IsWithin(mRoot, source))
        throw std::runtime_error("Choose a folder that does not contain the user library.");
      std::vector<fs::path> files;
      if (directory)
      {
        for (const auto& entry : fs::recursive_directory_iterator(source))
        {
          if (!entry.is_symlink() && entry.is_regular_file())
            files.push_back(entry.path());
        }
      }
      else
        files.push_back(source);
      std::sort(files.begin(), files.end());
      for (const auto& file : files)
      {
        auto suffix = PathString(file.extension());
        std::transform(suffix.begin(), suffix.end(), suffix.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (suffix != "." + extension)
        {
          ++result.skipped;
          continue;
        }
        try
        {
          auto relative = directory ? fs::weakly_canonical(source).filename() / file.lexically_relative(source)
                                    : file.filename();
          // The existing directory browser matches extensions case-sensitively.
          relative.replace_extension("." + extension);
          auto target = destination / relative;
          if (!IsWithin(target, destination))
            throw std::runtime_error("Destination is outside the category folder");
          fs::create_directories(target.parent_path());
          // Stage and validate a private copy, then publish it atomically. Menus and
          // other instances can never load a partially copied asset.
          StagingDirectory staging(destination);
          const auto stagedFile = staging.path / file.filename();
          fs::copy_file(file, stagedFile);
          validate(stagedFile);
          const auto original = target;
          for (size_t index = 2;; ++index)
          {
            std::error_code error;
            fs::create_hard_link(stagedFile, target, error);
            if (!error)
              break;
            if (error != std::errc::file_exists)
              throw fs::filesystem_error("Could not publish asset", file, target, error);
            target = original.parent_path() /
                     fs::u8path(PathString(original.stem()) + " (" + std::to_string(index) + ")" + suffix);
          }
          ++result.imported;
        }
        catch (const std::exception& error)
        {
          result.errors.push_back(PathString(file) + ": " + error.what());
        }
      }
    }
    catch (const std::exception& error)
    {
      result.errors.push_back(error.what());
    }
    return result;
  }

private:
  struct StagingDirectory
  {
    fs::path path;
    explicit StagingDirectory(const fs::path& root)
    {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      for (size_t attempt = 0;; ++attempt)
      {
        path = root / (".import-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (fs::create_directory(path))
          break;
      }
    }
    ~StagingDirectory()
    {
      std::error_code error;
      fs::remove_all(path, error);
    }
  };

  fs::path mRoot;
};
} // namespace nam_library
