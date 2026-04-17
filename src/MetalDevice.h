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

#ifndef VOX_METAL_DEVICE_H
#define VOX_METAL_DEVICE_H

#include <vox/Vox.h>

#ifdef __OBJC__
#import <Metal/Metal.h>
#endif

namespace vox
{

class METAL_BUFFER : public BUFFER
{
public:
   METAL_BUFFER (void* pDevice, const BUFFER_DESC& desc);
   ~METAL_BUFFER () override;

   void SetData (const void* pSrc, size_t nSize, size_t nOffset = 0) override;
   void GetData (void* pDst, size_t nSize, size_t nOffset = 0) override;

   void*  GetHandle () const { return m_pBuffer; }
   size_t GetSize () const   { return m_nSize; }

private:
   void*  m_pBuffer;
   size_t m_nSize;
};

class METAL_KERNEL : public KERNEL
{
public:
   METAL_KERNEL (void* pDevice, const void* pSpvBytes, size_t nSpvSize,
                 const char* szEntryPoint);
   ~METAL_KERNEL () override;

   void* GetPipelineState () const { return m_pPipelineState; }

private:
   void* m_pPipelineState;
};

class METAL_DEVICE : public DEVICE
{
public:
   METAL_DEVICE ();
   ~METAL_DEVICE () override;

   Backend GetBackend () const override;

   BUFFER* CreateBuffer (const BUFFER_DESC& desc) override;
   KERNEL* CreateKernel (const void* pSpvBytes, size_t nSpvSize,
                         const char* szEntryPoint) override;

   void DestroyBuffer (BUFFER* pBuffer) override;
   void DestroyKernel (KERNEL* pKernel) override;

   void SetKernel (KERNEL* pKernel) override;
   void SetBuffer (BUFFER* pBuffer, uint32_t nBinding, bool bReadOnly = true) override;
   void SetPushConstants (const void* pData, size_t nSize) override;
   void Dispatch (const DISPATCH_ARGS& args) override;
   void Finish () override;

private:
   void* m_pDevice;
   void* m_pCommandQueue;

   METAL_KERNEL* m_pCurrentKernel;

   struct BOUND_BUFFER
   {
      METAL_BUFFER* pBuffer;
      uint32_t      nBinding;
      bool          bReadOnly;
   };
   std::vector<BOUND_BUFFER> m_aBoundBuffers;

   std::vector<uint8_t> m_aPushConstantData;
};

} // namespace vox

#endif // VOX_METAL_DEVICE_H
