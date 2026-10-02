# Apple9 hardware contract library

This directory holds the API-independent description of what an Apple9 GPU
(G16G in the M4, T8132) consumes: control streams, fixed-function state
records, state-load programs, resource descriptors and the per-pass
configuration handed to the kernel. The Gallium driver and the Vulkan driver
are both built on it.

## Provenance

**Everything here is derived from static analysis of Apple's binaries**
(macOS 27.0 26A428: `AGXMetalG16G_B0`, `AGXCompilerCore`, `IOGPU`, the AGX
kernel extension and the GPU firmware). It is not clean-room work. Do not
submit it to upstream Mesa, to GravityLinux or to any project that depends on
a clean-room provenance, and do not copy it into such a tree.

Source comments cite the evidence as address ranges:

| Prefix | Image |
|---|---|
| `U:` | `AGXMetalG16G_B0` (userspace Metal driver, dyld shared cache addresses) |
| `C:` | `AGXCompilerCore` |
| `I:` | `IOGPU` |
| `K:` | AGX kernel extension |
| `F:` | GPU firmware |

A field name is taken from the native symbol or string when one exists;
otherwise it is a descriptive name. Constants without an address are not
accepted.

## Layout

The module split follows the native driver's own encoders, so that each file
has one native counterpart to be checked against.

| Module | Native counterpart | Contents |
|---|---|---|
| `apple9.xml` | — | Bit layouts of every record, generated into `agx_pack.h` next to the Apple8 structs |
| `agx9_vdm` | `AGX::VDMEncoderGen5` | Vertex data master stream: draw commands, state words, links |
| `agx9_ppp` | `AGX::PPPEncoderGen6` | Per-draw fixed-function state records and the varying table |
| `agx9_cdm` | `AGX::ComputeContext` | Compute data master stream |
| `agx9_esl` | `AGX::ESLStateLoadEncoderGen2`, `AGX::ESLInstructionEncoderGen3` | State-load programs that bind resources and launch a shader |
| `agx9_texture` | `AGX::TextureGen4`, `AGX::SamplerStateEncoderGen4_1` | Image read/write state, sampler state, memory layout |
| `agx9_framebuffer` | `AGX::Framebuffer`, `AGX::RenderUSCStateLoader` | Tile configuration, load/store programs, render command fields |

## Drivers

- `src/gallium/drivers/asahi` keeps its Apple8 path unchanged. Its Apple9 path
  moves onto this library one encoder at a time; the black-box encoders in
  `agx_apple9*.c` are removed as their replacements land.
- The Vulkan driver for Apple9 is a separate front end from Honeykrisp's
  Apple8 back end. It maps Vulkan objects onto the same objects the native
  Metal driver uses: a pipeline is a compiled program pair with vertex layout,
  blend and attachment formats baked in; a descriptor set is an argument
  buffer of 64-bit resource pointers; push constants and dynamic offsets live
  in the per-draw root table; a dynamic-rendering pass is one render command
  with native load/store programs.

Semantics that Metal does not have (robust buffer access, triangle fans,
geometry shaders, transform feedback, logic ops, provoking-vertex control)
have no native reference. Their lowering lives in the drivers, not here.
