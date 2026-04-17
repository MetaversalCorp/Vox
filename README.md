# Vox

An abstract layer for dispatching SPIR-V compute shaders across Vulkan, DX12, and Metal.

## What It Does

Vox accepts SPIR-V compute shader bytecode and dispatches it on the GPU. You write one shader; Vox handles the rest. On Vulkan, SPIR-V is fed directly to the driver. On DX12 and Metal, Vox uses [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross) to cross-compile the shader to HLSL or MSL at kernel creation time, then hands the translated source to the platform's native shader compiler.

No rendering. No framebuffers. No swap chains. Pure compute.

## Architecture

Vox uses classic polymorphism. Three abstract base classes (`DEVICE`, `BUFFER`, `KERNEL`) define the public API. Each GPU backend provides its own derived implementations:

| Backend | SPIR-V Handling | Platform |
|---------|----------------|----------|
| Vulkan  | Native (no translation) | Windows, Linux |
| DX12    | SPIRV-Cross to HLSL, then D3DCompile | Windows |
| Metal   | SPIRV-Cross to MSL, then MTLLibrary | macOS |

`DEVICE::Create(Backend::Auto)` selects the best available backend for the current platform. Callers only interact with the abstract base classes — backend-specific types are never exposed.

## Building

Vox is a standard CMake project:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSPIRV_CROSS_ROOT=/path/to/spirv-cross/install
cmake --build build --config Release
```

### Dependencies

| Dependency | Required By | Notes |
|------------|-------------|-------|
| Vulkan SDK | Vulkan backend | Headers only. Auto-detected via `find_package(Vulkan)`. |
| SPIRV-Cross | DX12 + Metal backends | HLSL and MSL cross-compilation. Pass `-DSPIRV_CROSS_ROOT=...` if not on the system path. |
| Windows SDK | DX12 backend | `d3d12.lib`, `dxgi.lib`, `d3dcompiler.lib`. Already present on Windows dev machines. |
| Metal framework | Metal backend | macOS system framework. |

### Backend Selection

Backends are enabled automatically based on the platform and available SDKs:

- **Vulkan** — enabled if `find_package(Vulkan)` succeeds
- **DX12** — enabled on Windows (always)
- **Metal** — enabled on macOS (always)

## Usage

```cpp
#include <vox/Vox.h>

// Create a device (auto-selects best backend)
vox::DEVICE* pDevice = vox::DEVICE::Create ();

// Create buffers
vox::BUFFER_DESC inputDesc  = { nDataSize, true };
vox::BUFFER_DESC outputDesc = { nDataSize, true };
vox::BUFFER* pInput  = pDevice->CreateBuffer (inputDesc);
vox::BUFFER* pOutput = pDevice->CreateBuffer (outputDesc);

// Upload data
pInput->SetData (aMyData, nDataSize);

// Load SPIR-V kernel
vox::KERNEL* pKernel = pDevice->CreateKernel (pSpvBytes, nSpvSize, "main");

// Dispatch
pDevice->SetKernel (pKernel);
pDevice->SetBuffer (pInput, 0, true);    // binding 0, read-only
pDevice->SetBuffer (pOutput, 1, false);  // binding 1, read-write
pDevice->Dispatch ({ nGroupsX, 1, 1 });
pDevice->Finish ();

// Read results
pOutput->GetData (aResults, nDataSize);

// Cleanup
pDevice->DestroyBuffer (pInput);
pDevice->DestroyBuffer (pOutput);
pDevice->DestroyKernel (pKernel);
delete pDevice;
```

## License

Apache 2.0. See [LICENSE](LICENSE) for details.
