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

#ifndef VOX_VULKAN_DEVICE_H
#define VOX_VULKAN_DEVICE_H

#include <vox/Vox.h>
#include <vulkan/vulkan.h>
#include <vector>

namespace vox
{

class VULKAN_BUFFER : public BUFFER
{
public:
   VULKAN_BUFFER (VkDevice pDevice, VkPhysicalDevice pPhysical, const BUFFER_DESC& desc);
   ~VULKAN_BUFFER () override;

   void SetData (const void* pSrc, size_t nSize, size_t nOffset = 0) override;
   void GetData (void* pDst, size_t nSize, size_t nOffset = 0) override;

   VkBuffer     GetHandle () const { return m_pBuffer; }
   size_t       GetSize () const   { return m_nSize; }

private:
   VkDevice       m_pDevice;
   VkBuffer       m_pBuffer;
   VkDeviceMemory m_pMemory;
   size_t         m_nSize;
};

class VULKAN_KERNEL : public KERNEL
{
public:
   VULKAN_KERNEL (VkDevice pDevice, const void* pSpvBytes, size_t nSpvSize,
                  const char* szEntryPoint);
   ~VULKAN_KERNEL () override;

   VkPipeline       GetPipeline () const       { return m_pPipeline; }
   VkPipelineLayout GetPipelineLayout () const  { return m_pPipelineLayout; }
   VkDescriptorSetLayout GetDescriptorSetLayout () const { return m_pDescSetLayout; }

private:
   VkDevice              m_pDevice;
   VkShaderModule        m_pShaderModule;
   VkPipelineLayout      m_pPipelineLayout;
   VkPipeline            m_pPipeline;
   VkDescriptorSetLayout m_pDescSetLayout;
};

class VULKAN_DEVICE : public DEVICE
{
public:
   VULKAN_DEVICE ();
   ~VULKAN_DEVICE () override;

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
   uint32_t FindMemoryType (uint32_t nTypeFilter, VkMemoryPropertyFlags nProperties);

   VkInstance               m_pInstance;
   VkPhysicalDevice         m_pPhysicalDevice;
   VkDevice                 m_pDevice;
   VkQueue                  m_pQueue;
   uint32_t                 m_nQueueFamily;
   VkCommandPool            m_pCommandPool;
   VkCommandBuffer          m_pCommandBuffer;
   VkFence                  m_pFence;
   VkDescriptorPool         m_pDescPool;
   VkDescriptorSet          m_pDescSet;

   VULKAN_KERNEL*           m_pCurrentKernel;
   std::vector<const void*> m_aPushConstantData;
   size_t                   m_nPushConstantSize;

   struct BOUND_BUFFER
   {
      VULKAN_BUFFER* pBuffer;
      uint32_t       nBinding;
      bool           bReadOnly;
   };
   std::vector<BOUND_BUFFER> m_aBoundBuffers;
};

} // namespace vox

#endif // VOX_VULKAN_DEVICE_H
