# Contributing to Puke Amp

Puke Amp welcomes fixes, features, documentation, and contributions to the
model and IR libraries. Fork the canonical repository and open a focused pull
request. Discuss substantial DSP, preset compatibility, platform, or asset
library changes with the maintainers before starting.
[Puke Amp repository](https://github.com/ElectricGuitarInnovationLab/Puke-Amp)

- Explain the behavior change, link any related discussion, and list the tests
  and manual checks you ran, including the host and platform when relevant.
- Build and test the affected format. For DSP changes, verify audio in a host
  with representative model and IR combinations. For parameter or state
  changes, recall existing DAW sessions and presets.
  [Native build setup](README.md#building) · [Tests and smoke checks](NeuralAmpModeler/tests/README.md)
- Follow the existing LLVM-based C++ formatting in changed code. Use
  [the repository guide](AGENTS.md) for architecture, compatibility, and review
  guidance.
- For models, IRs, and bundled graphics, give the source and licensing details.
  Follow the cabinet catalog conventions when changing microphone captures.
  [Asset license](ASSETS_LICENSE.md) · [Cabinet library](Models/CABINET_LIBRARY.md)
- Give modified distributions a distinct identity.
  [Trademark and branding guidelines](TRADEMARK.md)

## Contributor License Agreement

Read the [Puke Studio Individual Contributor License Agreement for Puke Amp](CONTRIBUTOR_LICENSE_AGREEMENT.md).
The agreement lets Puke Studio distribute contributions under the licensing
terms stated in the agreement while contributors retain ownership. Confirm
that you have the rights to submit your contribution and obtain any required
employer approval. Contact the maintainers if the entity agreement applies.

If a change includes material you do not own, identify its source, license,
and the parts you are submitting in the pull request so maintainers can review
them. Preserve required notices. [Third-party notices](THIRD_PARTY.md)

To record your agreement, post this exact comment on your pull request:

> I have read the CLA and I hereby sign it
