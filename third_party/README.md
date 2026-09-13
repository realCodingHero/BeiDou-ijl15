# Bundled upscaling dependencies

No dependencies or models are downloaded at game startup.

## d3d8to9

Upstream: https://github.com/crosire/d3d8to9

Pinned revision: `6cdb8a82184898f1b9371e4c8412c2d33ebb7b51`.
The `source` and `res` directories are vendored with the upstream BSD-2-Clause
license in `d3d8to9/LICENSE.md`. Copyright notices are preserved.

Local changes: rename factory/validation entry points for our native-DX8 fallback;
replace the interactive missing-D3DX error with a failure return; load D3DX from
the system directory; attach an owned renderer to each device; intercept primary
device Present/Reset/final Release and account for renderer-owned COM references.

## CuNNy

Upstream: https://github.com/funnyplanter/CuNNy

Pinned revision: `906031bb00c15dd6a6bbbaa21c0eb0b724ca8437`.
Files are copied unmodified from `magpie/CuNNy-fast-NVL.hlsl` and
`magpie/CuNNy-veryfast-NVL.hlsl`. Their weights are embedded in generated shaders.

The upstream repository LICENSE says LGPL-3.0 and is retained as `cunny/LICENSE`.
The two shader file headers explicitly say GPL-3.0-or-later; their notices are
preserved and the GNU GPL v3 text is included as `cunny/COPYING`. This integration
retains those terms for the model code. The surrounding BeiDou project retains
its existing AGPL v3 license in the repository root.

`tools/generate-cunny.py` translates the four convolution stages to ps_3_0,
splitting each stage into one draw per four output channels. Intermediate UNORM8
quantization, edge clamping, weights, subpixel order and YUV reconstruction match
the pinned models. No retraining or model-weight modification is performed.

Distributions of this DLL must include these notices, license texts, and the
corresponding source and build scripts, including the local integration changes.
