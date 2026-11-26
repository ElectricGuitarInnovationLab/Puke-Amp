#pragma once

// Data-only cabinet library. No graphics or DSP dependencies: the same catalog
// is used by the editor and by the asset validation tests.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"

namespace nam_cabinet
{
namespace fs = std::filesystem;

inline std::string PathString(const fs::path& path)
{
  const auto text = path.u8string();
  return std::string(text.begin(), text.end());
}

struct Artwork
{
  fs::path path;
  float anchorX = 0.f, anchorY = 0.f;
  float scale = 1.f;
};

struct Cabinet
{
  std::string id, name, prefix;
  Artwork image;
  float pixelsPerInch = 260.f;
};

struct Microphone
{
  std::string id, name, token;
  std::map<int, Artwork> images;
};

enum class Placement { Measured, Inside, Outside };

struct Recording
{
  fs::path path;
  size_t cabinet = 0, microphone = 0;
  int angle = 0;
  float center = 0.f, grill = 0.f;
  Placement placement = Placement::Measured;
  int preference = 0;
};

class Catalog
{
public:
  std::vector<Cabinet> cabinets;
  std::vector<Microphone> microphones;
  std::vector<Recording> recordings;

  static Catalog Load(const fs::path& modelsRoot)
  {
    Catalog result;
    std::ifstream stream(modelsRoot / "cabinet-library.json");
    if (!stream)
      throw std::runtime_error("Cabinet library is missing. Reinstall the bundled Models resources.");
    nlohmann::json document;
    stream >> document;
    if (document.at("version").get<int>() != 1)
      throw std::runtime_error("Unsupported cabinet library version.");
    const auto omittedGrill = document.at("omitted_grill_inches").get<float>();
    CheckDistance(omittedGrill);
    std::set<std::string> ids, prefixes;
    for (const auto& value : document.at("cabinets"))
    {
      Cabinet cabinet;
      cabinet.id = value.at("id").get<std::string>();
      cabinet.name = value.at("name").get<std::string>();
      cabinet.prefix = value.at("filename_prefix").get<std::string>();
      cabinet.image = ReadArtwork(modelsRoot, value.at("image"));
      cabinet.pixelsPerInch = value.at("source_pixels_per_inch").get<float>();
      if (cabinet.id.empty() || cabinet.name.empty() || cabinet.prefix.empty()
          || !ids.insert(cabinet.id).second || !prefixes.insert(cabinet.prefix).second
          || !std::isfinite(cabinet.pixelsPerInch) || cabinet.pixelsPerInch <= 0.f)
        throw std::runtime_error("Invalid or duplicate cabinet definition.");
      result.cabinets.push_back(cabinet);
    }
    ids.clear();
    prefixes.clear();
    for (const auto& value : document.at("microphones"))
    {
      Microphone microphone;
      microphone.id = value.at("id").get<std::string>();
      microphone.name = value.at("name").get<std::string>();
      microphone.token = value.at("filename_token").get<std::string>();
      for (auto it = value.at("images").begin(); it != value.at("images").end(); ++it)
      {
        if (it.key() != "0" && it.key() != "45")
          throw std::runtime_error("Microphone image angles must be 0 or 45.");
        microphone.images.emplace(std::stoi(it.key()), ReadArtwork(modelsRoot, it.value()));
      }
      if (microphone.id.empty() || microphone.name.empty() || microphone.token.empty()
          || !ids.insert(microphone.id).second || !prefixes.insert(microphone.token).second
          || microphone.images.empty())
        throw std::runtime_error("Invalid or duplicate microphone definition.");
      result.microphones.push_back(microphone);
    }

    // Tokens are compared literally (MD421II(M), for example, contains regex
    // metacharacters). Only the position suffix is parsed as a regular expression.
    const std::regex position(R"(^([0-9]+(?:\.[0-9]+)?)_from_center(?:_([0-9]+(?:\.[0-9]+)?)_from_grill)?(?:_(0|45)_offset)?(_LL)?$)");
    const std::regex atmosphere(R"(^(?:Atmospheric|Atmosphere)(Inside|Outside)(?:_G11B)?(_LL)?$)");
    for (const auto& file : fs::recursive_directory_iterator(modelsRoot / "IR"))
    {
      if (!file.is_regular_file() || file.path().extension() != ".wav")
        continue;
      const auto stem = PathString(file.path().stem());
      for (size_t cab = 0; cab < result.cabinets.size(); ++cab)
        for (size_t mic = 0; mic < result.microphones.size(); ++mic)
        {
          const auto prefix = result.cabinets[cab].prefix + "_" + result.microphones[mic].token + "_";
          if (stem.compare(0, prefix.size(), prefix) != 0)
            continue;
          std::smatch match;
          const auto suffix = stem.substr(prefix.size());
          Recording recording;
          recording.path = file.path().lexically_normal();
          recording.cabinet = cab;
          recording.microphone = mic;
          if (std::regex_match(suffix, match, position))
          {
            recording.center = std::stof(match[1].str());
            recording.grill = match[2].matched ? std::stof(match[2].str()) : omittedGrill;
            recording.angle = match[3].matched ? std::stoi(match[3].str()) : 0;
            recording.preference = (match[2].matched ? 2 : 0) + (match[4].matched ? 1 : 0);
            CheckDistance(recording.center);
            CheckDistance(recording.grill);
          }
          else if (std::regex_match(suffix, match, atmosphere))
          {
            recording.placement = match[1].str() == "Inside" ? Placement::Inside : Placement::Outside;
            recording.preference = match[2].matched ? 1 : 0;
          }
          else
            continue;
          if (!result.microphones[mic].images.count(recording.angle))
            throw std::runtime_error("Missing microphone artwork for " + PathString(file.path().filename()));
          result.recordings.push_back(recording);
        }
    }
    // Explicit distances and LL variants win ties, irrespective of filesystem order.
    std::sort(result.recordings.begin(), result.recordings.end(), [](const Recording& a, const Recording& b) {
      return a.preference != b.preference ? a.preference > b.preference : a.path < b.path;
    });
    if (result.recordings.empty())
      throw std::runtime_error("No matching cabinet recordings were found in Models/IR.");
    return result;
  }

  std::vector<size_t> Positions(size_t cabinet, size_t microphone, int angle) const
  {
    std::vector<size_t> positions;
    for (size_t i = 0; i < recordings.size(); ++i)
      if (recordings[i].cabinet == cabinet && recordings[i].microphone == microphone && recordings[i].angle == angle
          && recordings[i].placement == Placement::Measured)
        positions.push_back(i);
    return positions;
  }

  int Nearest(size_t cabinet, size_t microphone, int angle, float center, float grill) const
  {
    int nearest = -1;
    float distance = std::numeric_limits<float>::max();
    for (auto index : Positions(cabinet, microphone, angle))
    {
      const auto& recording = recordings[index];
      const float dx = center - recording.center, dy = grill - recording.grill;
      const float candidate = dx * dx + dy * dy;
      if (candidate < distance)
      {
        distance = candidate;
        nearest = static_cast<int>(index);
      }
    }
    return nearest;
  }

  int RoomPosition(size_t cabinet, size_t microphone, Placement placement) const
  {
    for (size_t i = 0; i < recordings.size(); ++i)
      if (recordings[i].cabinet == cabinet && recordings[i].microphone == microphone
          && recordings[i].placement == placement)
        return static_cast<int>(i);
    return -1;
  }

  // Keep the microphone where possible, then match the booth location or nearest measured position.
  int Match(const Recording& source, size_t cabinet, size_t microphone) const
  {
    auto any = std::find_if(recordings.begin(), recordings.end(), [=](const Recording& r) {
      return r.cabinet == cabinet;
    });
    if (any == recordings.end())
      return -1;
    auto first = std::find_if(recordings.begin(), recordings.end(), [=](const Recording& r) {
      return r.cabinet == cabinet && r.microphone == microphone;
    });
    if (first == recordings.end())
      first = any;
    microphone = first->microphone;
    if (source.placement != Placement::Measured)
    {
      const int same = RoomPosition(cabinet, microphone, source.placement);
      if (same >= 0)
        return same;
    }
    const int nearest = Nearest(cabinet, microphone, source.angle, source.center, source.grill);
    if (nearest >= 0)
      return nearest;
    if (first->placement == Placement::Measured)
      return Nearest(cabinet, microphone, first->angle, source.center, source.grill);
    return static_cast<int>(first - recordings.begin());
  }

  int Find(const fs::path& path) const
  {
    if (path.empty())
      return -1;
    std::error_code error;
    const auto normalized = fs::weakly_canonical(path, error);
    if (error)
      return -1;
    for (size_t i = 0; i < recordings.size(); ++i)
    {
      const auto candidate = fs::weakly_canonical(recordings[i].path, error);
      if (!error && normalized == candidate)
        return static_cast<int>(i);
    }
    return -1;
  }

private:
  static void CheckDistance(float distance)
  {
    if (!std::isfinite(distance) || distance < 0.f || distance > 1000.f)
      throw std::runtime_error("Invalid microphone distance in cabinet library.");
  }

  static Artwork ReadArtwork(const fs::path& root, const nlohmann::json& value)
  {
    Artwork artwork;
    const auto relative = fs::u8path(value.at("path").get<std::string>());
    if (relative.empty() || relative.is_absolute())
      throw std::runtime_error("Artwork paths must be relative to Models.");
    for (const auto& component : relative)
      if (component == "..")
        throw std::runtime_error("Artwork paths cannot leave Models.");
    artwork.path = root / relative;
    artwork.anchorX = value.at("anchor").at(0).get<float>();
    artwork.anchorY = value.at("anchor").at(1).get<float>();
    artwork.scale = value.value("scale", 1.f);
    if (!fs::is_regular_file(artwork.path) || !std::isfinite(artwork.anchorX) || !std::isfinite(artwork.anchorY)
        || artwork.anchorX < 0.f || artwork.anchorY < 0.f || !std::isfinite(artwork.scale) || artwork.scale <= 0.f)
      throw std::runtime_error("Missing or invalid artwork: " + PathString(relative));
    return artwork;
  }
};
} // namespace nam_cabinet
