#include "../AssetLibrary.h"
#include "../AssetValidation.h"

#include <cassert>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) try
{
  assert(argc == 4);
  namespace fs = std::filesystem;
  nam_library::ValidateModel(fs::u8path(argv[1]));
  nam_library::ValidateModel(fs::u8path(argv[2]));
  nam_library::ValidateIR(fs::u8path(argv[3]));
  const auto root = fs::temp_directory_path() /
                    ("nam-validation-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root);
  std::ofstream(root / "bad.nam") << "not a model";
  std::ofstream(root / "bad.wav") << "not a wave file";
  nam_library::AssetLibrary library(root / "Library");
  assert(library.Import(root / "bad.nam", "Amps", "nam", nam_library::ValidateModel).errors.size() == 1);
  assert(library.Import(root / "bad.wav", "IRs", "wav", nam_library::ValidateIR).errors.size() == 1);
  assert(!fs::exists(library.Category("Amps") / "bad.nam"));
  assert(!fs::exists(library.Category("IRs") / "bad.wav"));
  fs::remove_all(root);
  std::cout << "Asset loader validation tests passed\n";
}
catch (const std::exception& error)
{
  std::cerr << error.what() << std::endl;
  return 1;
}
