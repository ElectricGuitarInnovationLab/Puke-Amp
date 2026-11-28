#pragma once

#include "AssetLibrary.h"
#include "CabinetMixer.h"
#include <future>
#include "IPlug_include_in_plug_hdr.h"

using namespace iplug;
using namespace igraphics;

class PukeAcoustic final : public Plugin
{
public:
  explicit PukeAcoustic(const InstanceInfo& info);
  void OnReset() override;
  void OnIdle() override;
  void ProcessBlock(sample** inputs, sample** outputs, int frames) override;
  bool SerializeState(IByteChunk& chunk) const override;
  int UnserializeState(const IByteChunk& chunk, int pos) override;

  std::string Selection(int slot) const;
  bool Choose(int slot, const std::string& id);
  bool LoadIR(int slot, const char* path);
  void ImportIRs(const char* path);
  const std::filesystem::path& PresetDirectory() const { return mPresetDirectory; }
  std::filesystem::path FactoryPresetDirectory() const;
  std::filesystem::path IRLibraryDirectory() const;
  const std::string& GetPresetNameText() const { return mPresetName; }
  bool SavePreset(const std::filesystem::path& path);
  bool LoadPreset(const std::filesystem::path& path);
  const std::vector<std::string>& Library() const { return mLibrary; }

private:
  std::string RelativeId(const std::string& path) const;
  std::string PathFor(const std::string& id) const;
  nam_cabinet::Mixer mMixer;
  std::filesystem::path mRoot;
  std::filesystem::path mLibraryRoot;
  std::filesystem::path mPresetDirectory;
  std::future<nam_library::ImportResult> mImport;
  std::string mPresetName = "Presets";
  std::vector<std::string> mLibrary;
  std::atomic<bool> mRefresh{false};
};
