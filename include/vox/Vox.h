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

#ifndef VOX_VOX_H
#define VOX_VOX_H

#include <cstdint>
#include <cstddef>

namespace vox
{

enum class Backend
{
   Auto,
   Vulkan,
   DX12,
   Metal
};

struct BUFFER_DESC
{
   size_t nSize;
   bool   bHostVisible;
};

struct DISPATCH_ARGS
{
   uint32_t nGroupsX;
   uint32_t nGroupsY;
   uint32_t nGroupsZ;
};

class BUFFER
{
public:
   virtual ~BUFFER () = default;
   BUFFER (const BUFFER&) = delete;
   BUFFER& operator= (const BUFFER&) = delete;
   BUFFER (BUFFER&&) = delete;
   BUFFER& operator= (BUFFER&&) = delete;

   virtual void SetData (const void* pSrc, size_t nSize, size_t nOffset = 0) = 0;
   virtual void GetData (void* pDst, size_t nSize, size_t nOffset = 0) = 0;

protected:
   BUFFER () = default;
};

class KERNEL
{
public:
   virtual ~KERNEL () = default;
   KERNEL (const KERNEL&) = delete;
   KERNEL& operator= (const KERNEL&) = delete;
   KERNEL (KERNEL&&) = delete;
   KERNEL& operator= (KERNEL&&) = delete;

protected:
   KERNEL () = default;
};

class DEVICE
{
public:
   virtual ~DEVICE () = default;
   DEVICE (const DEVICE&) = delete;
   DEVICE& operator= (const DEVICE&) = delete;
   DEVICE (DEVICE&&) = delete;
   DEVICE& operator= (DEVICE&&) = delete;

   static DEVICE* Create (Backend eBackend = Backend::Auto);

   virtual Backend GetBackend () const = 0;

protected:
   DEVICE () = default;

public:

   virtual BUFFER* CreateBuffer (const BUFFER_DESC& desc) = 0;
   virtual KERNEL* CreateKernel (const void* pSpvBytes, size_t nSpvSize,
                                 const char* szEntryPoint) = 0;

   virtual void DestroyBuffer (BUFFER* pBuffer) = 0;
   virtual void DestroyKernel (KERNEL* pKernel) = 0;

   virtual void SetKernel (KERNEL* pKernel) = 0;
   virtual void SetBuffer (BUFFER* pBuffer, uint32_t nBinding, bool bReadOnly = true) = 0;
   virtual void SetPushConstants (const void* pData, size_t nSize) = 0;
   virtual void Dispatch (const DISPATCH_ARGS& args) = 0;
   virtual void Finish () = 0;
};

} // namespace vox

#endif // VOX_VOX_H
