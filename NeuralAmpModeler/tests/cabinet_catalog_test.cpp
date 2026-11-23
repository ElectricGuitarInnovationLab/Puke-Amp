#include "../CabinetCatalog.h"
#include <cassert>
#include <chrono>
#include <iostream>

using namespace nam_cabinet;

int main(int argc, char** argv)
{
  assert(argc == 2);
  const fs::path root = fs::u8path(argv[1]);
  const auto catalog = Catalog::Load(root);
  assert(catalog.cabinets.size() == 1);
  assert(catalog.microphones.size() == 5);
  assert(catalog.recordings.size() == 163);
  size_t wavCount = 0;
  for (const auto& file : fs::recursive_directory_iterator(root / "IR"))
    if (file.path().extension() == ".wav")
    {
      ++wavCount;
      assert(catalog.Find(file.path()) >= 0); // Every shipped WAV is reachable.
    }
  assert(wavCount == catalog.recordings.size());
  for (size_t mic = 0; mic < 5; ++mic)
    for (int angle : {0, 45})
    {
      std::set<std::pair<float, float>> points;
      for (auto index : catalog.Positions(0, mic, angle))
        points.emplace(catalog.recordings[index].center, catalog.recordings[index].grill);
      assert(points.size() == 16);
      for (float center : {0.f, 1.f, 2.f, 5.f})
        for (float grill : {0.f, 1.f, 2.f, 5.f})
          assert(points.count({center, grill}));
    }

  const auto royer = catalog.Find(root / "IR/Orange_Celestion_V30_Royer_back_121_0_from_center_1_from_grill_45_offset_LL.wav");
  assert(royer >= 0);
  const auto& r = catalog.recordings[royer];
  assert(r.center == 0 && r.grill == 1 && r.angle == 45);
  assert(catalog.microphones[r.microphone].id == "royer-121-back");
  assert(catalog.microphones[r.microphone].images.at(45).path.filename() == "r121_45.png");
  assert(catalog.cabinets[r.cabinet].image.path.filename() == "ppc112.png");
  const auto sm57 = catalog.Find(root / "IR/Orange_Celestion_V30_Shure_SM57_2_from_center_2_from_grill_LL.wav");
  assert(sm57 >= 0);
  const auto& s = catalog.recordings[sm57];
  assert(s.center == 2 && s.grill == 2 && s.angle == 0);
  assert(catalog.microphones[s.microphone].images.at(0).path.filename() == "sm57.png");
  assert(catalog.Nearest(0, s.microphone, 0, 2.3f, 1.8f) == sm57);
  assert(catalog.Nearest(0, s.microphone, 90, 0, 0) == -1);
  const auto& far = catalog.recordings[catalog.Nearest(0, s.microphone, 0, 100, 100)];
  assert(far.center == 5 && far.grill == 5);
  const auto& near = catalog.recordings[catalog.Nearest(0, s.microphone, 0, -100, -100)];
  assert(near.center == 0 && near.grill == 0);
  const auto& e609 = catalog.recordings[catalog.Nearest(0, 2, 0, 2, 0)];
  assert(e609.path.filename() == "Orange_Celestion_V30_Sennheiser_609_2_from_center_0_from_grill_LL.wav");
  const auto omitted = catalog.Find(root / "IR/Orange_Celestion_V30_Royer_front_121_0_from_center_LL.wav");
  assert(omitted >= 0 && catalog.recordings[omitted].grill == 0);

  const auto temp = fs::temp_directory_path() / ("nam-cabinet-" + std::to_string(
    std::chrono::steady_clock::now().time_since_epoch().count()));
  struct Cleanup { fs::path path; ~Cleanup() { fs::remove_all(path); } } cleanup{temp};
  fs::create_directories(temp / "IR/nested");
  std::ifstream input(root / "cabinet-library.json");
  nlohmann::json document;
  input >> document;
  // A completely different cabinet and mic can be introduced through data alone.
  auto cabinet = document["cabinets"][0];
  cabinet["id"] = "new-cab";
  cabinet["filename_prefix"] = "New_Cab";
  cabinet["name"] = "New cabinet";
  document["cabinets"].push_back(cabinet);
  auto mic = document["microphones"][0];
  mic["id"] = "new-mic";
  mic["filename_token"] = "New_Mic(M)";
  mic["name"] = "New microphone";
  document["microphones"].push_back(mic);
  for (const auto& item : catalog.cabinets)
  {
    const auto path = temp / item.image.path.lexically_relative(root);
    fs::create_directories(path.parent_path());
    std::ofstream(path).put('x');
  }
  for (const auto& microphone : catalog.microphones)
    for (const auto& image : microphone.images)
    {
      const auto path = temp / image.second.path.lexically_relative(root);
      fs::create_directories(path.parent_path());
      std::ofstream(path).put('x');
    }
  const auto first = temp / "IR/nested/New_Cab_New_Mic(M)_1.5_from_center_3_from_grill_45_offset_LL.wav";
  const auto second = temp / "IR/nested/New_Cab_New_Mic(M)_5_from_center_0_from_grill_LL.wav";
  std::ofstream(first).put('x');
  std::ofstream(second).put('x');
  std::ofstream(temp / "IR/unmapped.wav").put('x');
  auto write = [&] { std::ofstream(temp / "cabinet-library.json") << document.dump(2); };
  write();
  const auto extended = Catalog::Load(temp);
  assert(extended.recordings.size() == 2);
  assert(extended.Find(first) >= 0);
  const auto& fractional = extended.recordings[extended.Find(first)];
  assert(fractional.center == 1.5f && fractional.grill == 3.f && fractional.angle == 45);
  assert(extended.Nearest(1, 5, 45, 5, 0) == extended.Find(first)); // Sparse set: never fabricate an IR.
  assert(extended.Nearest(1, 5, 0, 1.5f, 3) == extended.Find(second));
  assert(extended.Find(temp / first.filename()) == -1); // Same basename is not the same recording.
  assert(catalog.Find(first) == -1);

  auto rejected = [&] {
    write();
    bool failed = false;
    try { Catalog::Load(temp); } catch (const std::exception&) { failed = true; }
    assert(failed);
  };
  const auto valid = document;
  document["version"] = 2;
  rejected();
  document = valid;
  document["cabinets"][1]["id"] = document["cabinets"][0]["id"];
  rejected();
  document = valid;
  document["cabinets"][0]["image"]["path"] = "../outside.png";
  rejected();
  document = valid;
  document["cabinets"][0]["image"]["path"] = "GUI/missing.png";
  rejected();
  document = valid;
  document["microphones"][5]["images"].erase("45");
  rejected();
  std::cout << "Validated all 163 bundled IRs, snapping, aliases, sparse positions, extension and invalid catalogs.\n";
}
