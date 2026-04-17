// Copyright 2026 Metaversal Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <vox/Vox.h>

#ifdef VOX_HAS_VULKAN
#include "VulkanDevice.h"
#endif

#ifdef VOX_HAS_DX12
#include "DX12Device.h"
#endif

#ifdef VOX_HAS_METAL
#include "MetalDevice.h"
#endif

namespace vox
{

DEVICE* DEVICE::Create (Backend eBackend)
{
   if (eBackend == Backend::Auto)
   {
#if defined(VOX_HAS_VULKAN)
      eBackend = Backend::Vulkan;
#elif defined(VOX_HAS_DX12)
      eBackend = Backend::DX12;
#elif defined(VOX_HAS_METAL)
      eBackend = Backend::Metal;
#else
      return nullptr;
#endif
   }

   DEVICE* pDevice = nullptr;

   switch (eBackend)
   {
#ifdef VOX_HAS_VULKAN
      case Backend::Vulkan:
         pDevice = new VULKAN_DEVICE ();
         break;
#endif

#ifdef VOX_HAS_DX12
      case Backend::DX12:
         pDevice = new DX12_DEVICE ();
         break;
#endif

#ifdef VOX_HAS_METAL
      case Backend::Metal:
         pDevice = new METAL_DEVICE ();
         break;
#endif

      default:
         break;
   }

   return pDevice;
}

} // namespace vox
