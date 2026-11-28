#pragma once

#include "../AudioDSPTools/dsp/ImpulseResponse.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace nam_cabinet
{
// Both microphones receive one mono input. Only the processing thread touches FIR history.
class Mixer
{
public:
  using Paths = std::array<std::string, 2>;
  using Changes = std::array<std::optional<std::string>, 2>; // nullopt keeps the slot, empty switches it off
  using Status = dsp::wav::LoadReturnCode;
  struct Result
  {
    int slot = 0;
    Status status = Status::SUCCESS;
  };
  struct Mix
  {
    double level = 1.; // linear amplitude, signed for polarity
    double pan = 0.; // -1 left to +1 right
  };

  Paths GetPaths() const
  {
    std::lock_guard<std::mutex> lock(mControlMutex);
    return mDesired ? mDesired->paths : Paths{};
  }

  // Control side. Validate the complete selection before publishing either slot.
  Result Select(const Changes& changes)
  {
    std::lock_guard<std::mutex> lock(mControlMutex);
    auto next = std::make_unique<Pair>();
    for (int i = 0; i < 2; ++i)
    {
      auto& path = next->paths[i];
      path = changes[i].value_or(mDesired ? mDesired->paths[i] : "");
      if (path.empty())
        continue;
      if (i && path == next->paths[0])
        next->irs[i] = next->irs[0];
      else if (mDesired)
        for (int j = 0; j < 2; ++j)
          if (path == mDesired->paths[j])
            next->irs[i] = mDesired->irs[j];
      if (next->irs[i])
        continue;
      try
      {
        dsp::ImpulseResponse::IRData data;
        auto status =
          dsp::wav::Load(std::filesystem::u8path(path).string().c_str(), data.mRawAudio, data.mRawAudioSampleRate);
        if (status != Status::SUCCESS)
          return {i, status};
        if (data.mRawAudio.empty() || !(data.mRawAudioSampleRate > 0.))
          return {i, Status::ERROR_INVALID_FILE};
        next->irs[i] = std::make_shared<IR>(data, mRate, mBlockSize);
      }
      catch (const std::exception&)
      {
        return {i, Status::ERROR_OTHER};
      }
    }
    if (!mDesired || next->paths != mDesired->paths)
      Publish(std::move(next));
    return {};
  }

  // Host reset, with rendering stopped. Rebuild from retained samples (no disk I/O).
  void Reset(double rate, int maxFrames)
  {
    std::lock_guard<std::mutex> lock(mControlMutex);
    mRate = rate;
    mBlockSize = std::max(1, maxFrames);
    mRampLength = std::max(1, static_cast<int>(std::round(rate * 0.01)));
    auto next = std::make_unique<Pair>();
    if (mDesired)
    {
      next->paths = mDesired->paths;
      for (int i = 0; i < 2; ++i)
        if (mDesired->irs[i])
          next->irs[i] = i && mDesired->irs[i] == mDesired->irs[0]
                           ? next->irs[0]
                           : std::make_shared<IR>(mDesired->irs[i]->GetData(), rate, mBlockSize);
    }
    mPending.store(nullptr);
    mActive = mPrevious = mDesired = nullptr;
    mOwned.clear();
    mHistoryEnd = mHistoryCount = 0;
    mFadeRemaining = 0;
    mFirstBlock = true;
    Publish(std::move(next));
  }

  void Collect()
  {
    if (!mNeedsCollection.exchange(false))
      return;
    std::lock_guard<std::mutex> lock(mControlMutex);
    CollectRetired();
  }

  // Render into the host's one or two output buffers. Inputs and outputs may alias.
  void Process(double* input, double** out, int channels, int frames, const std::array<Mix, 2>& mix, bool enabled,
               double dryMix = 0.)
  {
    if (!frames || !channels)
      return;
    ++mBlock;
    if (!mFadeRemaining)
      if (auto* next = mPending.exchange(nullptr))
      {
        mPrevious = mActive;
        mActive = next;
        mFadeRemaining = mFirstBlock ? 0 : mRampLength;
      }
    const double wetTarget = enabled ? 1. - dryMix : 0.;
    mWet.Set(wetTarget, mRampLength, mFirstBlock);
    for (int i = 0; i < 2; ++i)
      for (int c = 0; c < channels; ++c)
      {
        const double pan = channels == 1 ? 0. : (c == 0 ? mix[i].pan : -mix[i].pan);
        mGain[i][c].Set(mix[i].level * (1. - std::max(0., pan)), mRampLength, mFirstBlock);
      }
    mFirstBlock = false;
    const bool audible = wetTarget != 0. || mWet.value != 0.;
    if (!audible)
    {
      if (enabled && mActive && (mActive->irs[0] || mActive->irs[1]))
        Remember(input, frames); // Prime the IRs when returning from fully dry.
      else
        mHistoryCount = 0;
      mFadeRemaining = 0;
      RetirePrevious();
      for (int c = 0; c < channels; ++c)
        if (out[c] != input)
          std::copy_n(input, frames, out[c]);
      return;
    }
    const auto current = Render(mActive, input, frames);
    const auto previous = Render(mPrevious, input, frames);
    // Keep recent input while the cabinet is live. A newly positioned mic can hear
    // the same sustained note immediately, including captures with delayed onsets.
    if (current.mic[0] || current.mic[1] || previous.mic[0] || previous.mic[1])
      Remember(input, frames);
    else
      mHistoryCount = 0;
    for (int f = 0; f < frames; ++f)
    {
      const double dry = input[f], wet = mWet.Next();
      const double oldWeight = static_cast<double>(mFadeRemaining) / mRampLength;
      for (int c = 0; c < channels; ++c)
      {
        const double a = mGain[0][c].Next(), b = mGain[1][c].Next();
        const double now = current.Sample(f, dry, a, b);
        const double before = oldWeight ? previous.Sample(f, dry, a, b) : now;
        const double cabinet = now + oldWeight * (before - now);
        out[c][f] = (1. - wet) * dry + wet * cabinet;
      }
      if (mFadeRemaining)
        --mFadeRemaining;
    }
    if (!mFadeRemaining)
      RetirePrevious();
  }

private:
  struct IR : dsp::ImpulseResponse
  {
    IR(const IRData& data, double rate, int maxFrames)
    : ImpulseResponse(data, rate)
    {
      // Prime the upstream convolver's output and history allocations before publication.
      std::vector<double> silence(maxFrames);
      auto* samples = silence.data();
      _PrepareBuffers(1, maxFrames);
      _UpdateHistory(&samples, 1, maxFrames);
    }
    const double* Render(double* input, int frames, uint64_t block, const Mixer& mixer)
    {
      if (lastBlock != block)
      {
        if (!lastBlock || block - lastBlock > 1)
        {
          mHistoryIndex = mHistoryRequired;
          const size_t count = std::min(mHistoryRequired, mixer.mHistoryCount);
          const size_t capacity = mixer.mHistory.size();
          const size_t start = (mixer.mHistoryEnd + capacity - count) % capacity;
          const size_t first = std::min(count, capacity - start);
          auto dest = std::fill_n(mHistory.begin(), mHistoryRequired - count, 0.f);
          dest = std::copy_n(mixer.mHistory.begin() + start, first, dest);
          std::copy_n(mixer.mHistory.begin(), count - first, dest);
        }
        output = Process(&input, 1, frames)[0];
        lastBlock = block;
      }
      return output;
    }
    uint64_t lastBlock = 0;
    const double* output = nullptr;
  };
  struct Pair
  {
    Paths paths;
    std::array<std::shared_ptr<IR>, 2> irs;
    std::atomic<bool> retired{false};
  };
  struct Signals
  {
    std::array<const double*, 2> mic{};
    double Sample(int frame, double dry, double a, double b) const
    {
      if (!mic[0] && !mic[1])
        return dry;
      return (mic[0] ? a * mic[0][frame] : 0.) + (mic[1] ? b * mic[1][frame] : 0.);
    }
  };
  Signals Render(Pair* pair, double* input, int frames)
  {
    Signals signals;
    if (pair)
      for (int i = 0; i < 2; ++i)
        if (pair->irs[i])
          signals.mic[i] = pair->irs[i]->Render(input, frames, mBlock, *this);
    return signals;
  }
  void Remember(const double* input, size_t frames)
  {
    const size_t capacity = mHistory.size();
    const size_t count = std::min(frames, capacity);
    input += frames - count;
    const size_t first = std::min(count, capacity - mHistoryEnd);
    std::copy_n(input, first, mHistory.begin() + mHistoryEnd);
    std::copy_n(input + first, count - first, mHistory.begin());
    mHistoryEnd = (mHistoryEnd + count) % capacity;
    mHistoryCount = std::min(mHistoryCount + count, capacity);
  }
  struct Ramp
  {
    double value = 0., target = 0., step = 0.;
    int remaining = 0;
    void Set(double next, int length, bool immediate)
    {
      if (immediate)
      {
        value = target = next;
        remaining = 0;
      }
      else if (next != target)
      {
        target = next;
        remaining = length;
        step = (target - value) / length;
      }
    }
    double Next()
    {
      if (remaining)
        value = --remaining ? value + step : target;
      return value;
    }
  };
  void RetirePrevious()
  {
    if (!mPrevious)
      return;
    mPrevious->retired.store(true);
    mPrevious = nullptr;
    mNeedsCollection.store(true);
  }
  void Publish(std::unique_ptr<Pair> next)
  {
    mOwned.push_back(std::move(next));
    mDesired = mOwned.back().get();
    if (auto* superseded = mPending.exchange(mDesired))
      superseded->retired.store(true);
    CollectRetired();
  }
  void CollectRetired()
  {
    mOwned.erase(
      std::remove_if(mOwned.begin(), mOwned.end(), [](const auto& p) { return p->retired.load(); }), mOwned.end());
  }
  mutable std::mutex mControlMutex;
  std::vector<std::unique_ptr<Pair>> mOwned;
  Pair* mDesired = nullptr;
  std::atomic<Pair*> mPending{nullptr};
  Pair* mActive = nullptr;
  Pair* mPrevious = nullptr;
  std::array<float, 8192> mHistory{}; // upstream maximum cabinet FIR length
  size_t mHistoryEnd = 0, mHistoryCount = 0;
  std::atomic<bool> mNeedsCollection{false};
  Ramp mWet, mGain[2][2];
  uint64_t mBlock = 0;
  double mRate = 48000.;
  int mBlockSize = 1024, mRampLength = 480, mFadeRemaining = 0;
  bool mFirstBlock = true;
};
} // namespace nam_cabinet
