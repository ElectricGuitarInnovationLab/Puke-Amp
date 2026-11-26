#include "../CabinetMixer.h"

#include <cassert>
#include <thread>

using nam_cabinet::Mixer;

int main(int argc, char** argv)
{
  assert(argc == 3);
  const std::string a = argv[1], b = argv[2];
  assert(a != b);
  Mixer mixer;
  constexpr int block = 64;
  std::array<double, block> input, left, right;
  double* source = input.data();
  double* output[]{left.data(), right.data()};
  std::array<Mixer::Mix, 2> mix{};
  for (double rate : {48000., 96000.})
  {
    mixer.Reset(rate, block);
    assert(mixer.Select({a, std::string{}}).status == Mixer::Status::SUCCESS);
    dsp::ImpulseResponse refA(a.c_str(), rate), refB(b.c_str(), rate);
    int frame = 0;
    auto render = [&](int channels, bool enabled) {
      for (double& value : input)
      {
        ++frame;
        value = std::sin(frame * .082) + .15 * std::cos(frame * .013);
      }
      mixer.Process(source, output, channels, block, mix, enabled);
      return std::array<const double*, 2>{refA.Process(&source, 1, block)[0], refB.Process(&source, 1, block)[0]};
    };
    mix = {};
    for (int n = 0; n < 110; ++n)
    {
      auto ref = render(2, true);
      for (int i = 0; i < block; ++i)
        assert(left[i] == ref[0][i] && right[i] == ref[0][i]);
    }
    mix[0].pan = -1.;
    mix[1].pan = 1.;
    for (int n = 0; n < 20; ++n)
      render(2, true);
    assert(mixer.Select({std::nullopt, b}).status == Mixer::Status::SUCCESS);
    const int fade = static_cast<int>(rate * .01);
    for (int n = 0; n < 24; ++n)
    {
      auto ref = render(2, true);
      for (int i = 0; i < block; ++i)
      {
        assert(left[i] == ref[0][i]); // A's history survives a change to B
        const double weight = std::min(1., (n * block + i) / static_cast<double>(fade));
        assert(std::abs(right[i] - weight * ref[1][i]) < 1e-12);
      }
    }
    // A failed pair is atomic, including when A's proposed selection was valid.
    auto failed = mixer.Select({b, b + ".missing"});
    assert(failed.status != Mixer::Status::SUCCESS && failed.slot == 1);
    assert((mixer.GetPaths() == Mixer::Paths{a, b}));
    mix[1].level = -.5;
    for (int n = 0; n < 20; ++n)
      render(1, true);
    auto ref = render(1, true);
    for (int i = 0; i < block; ++i)
      assert(left[i] == ref[0][i] - .5 * ref[1][i]);
    mix = {};
    mix[1].level = -1.;
    assert(mixer.Select({a, a}).status == Mixer::Status::SUCCESS);
    for (int n = 0; n < 20; ++n)
      render(2, true);
    for (int i = 0; i < block; ++i)
      assert(left[i] == 0. && right[i] == 0.);
    for (int n = 0; n < 20; ++n)
      render(2, false);
    assert(left == input && right == input);
    // Latest pending selection wins before the next callback.
    mixer.Select({b, b});
    mixer.Select({std::string{}, std::string{}});
    for (int n = 0; n < 20; ++n)
      render(2, true);
    assert(left == input && right == input);
    mixer.Collect();
  }
  // Both selections survive a host sample-rate reset and are rebuilt from the originals.
  mixer.Select({a, b});
  mixer.Reset(44100., block);
  assert((mixer.GetPaths() == Mixer::Paths{a, b}));
  mix = {{{1., -1.}, {1., 1.}}};
  dsp::ImpulseResponse resetA(a.c_str(), 44100.), resetB(b.c_str(), 44100.);
  for (int n = 0; n < 12; ++n)
  {
    mixer.Process(source, output, 2, block, mix, true);
    auto* refA = resetA.Process(&source, 1, block)[0];
    auto* refB = resetB.Process(&source, 1, block)[0];
    for (int i = 0; i < block; ++i)
      assert(left[i] == refA[i] && right[i] == refB[i]);
  }
  mixer.Select({std::string{}, std::nullopt});
  for (int n = 0; n < 10; ++n)
  {
    mixer.Process(source, output, 2, block, mix, true);
    auto* refA = resetA.Process(&source, 1, block)[0];
    auto* refB = resetB.Process(&source, 1, block)[0];
    for (int i = 0; i < block; ++i)
    {
      const double fade = std::max(0., 1. - (n * block + i) / 441.);
      assert(std::abs(left[i] - fade * refA[i]) < 1e-12);
      assert(right[i] == refB[i]);
    }
  }
  // Exercise selection and reclamation while a host renders variable block sizes.
  std::atomic<bool> done{false};
  std::thread playback([&] {
    int n = 0;
    while (!done.load())
    {
      mixer.Process(source, output, 2, 1 + n % block, mix, n % 21 != 0);
      ++n;
    }
  });
  for (int n = 0; n < 120; ++n)
  {
    assert(mixer.Select({n % 3 ? a : b, n % 2 ? b : ""}).status == Mixer::Status::SUCCESS);
    mixer.Collect();
  }
  done = true;
  playback.join();
}
