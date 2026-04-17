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

#include "VulkanDevice.h"
#include <cstring>
#include <stdexcept>
#include <string>

namespace vox
{

// ---------------------------------------------------------------------------
// VULKAN_BUFFER
// ---------------------------------------------------------------------------

VULKAN_BUFFER::VULKAN_BUFFER (VkDevice pDevice, VkPhysicalDevice pPhysical,
                              const BUFFER_DESC& desc)
   : m_pDevice (pDevice)
   , m_pBuffer (VK_NULL_HANDLE)
   , m_pMemory (VK_NULL_HANDLE)
   , m_nSize (desc.nSize)
{
   VkBufferCreateInfo bufInfo = {};
   bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bufInfo.size  = desc.nSize;
   bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
   bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

   vkCreateBuffer (pDevice, &bufInfo, nullptr, &m_pBuffer);

   VkMemoryRequirements memReqs;
   vkGetBufferMemoryRequirements (pDevice, m_pBuffer, &memReqs);

   VkPhysicalDeviceMemoryProperties memProps;
   vkGetPhysicalDeviceMemoryProperties (pPhysical, &memProps);

   uint32_t nMemType = 0;
   VkMemoryPropertyFlags nDesired =
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
   {
      if ((memReqs.memoryTypeBits & (1u << i)) &&
          (memProps.memoryTypes[i].propertyFlags & nDesired) == nDesired)
      {
         nMemType = i;
         break;
      }
   }

   VkMemoryAllocateInfo allocInfo = {};
   allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   allocInfo.allocationSize  = memReqs.size;
   allocInfo.memoryTypeIndex = nMemType;

   vkAllocateMemory (pDevice, &allocInfo, nullptr, &m_pMemory);
   vkBindBufferMemory (pDevice, m_pBuffer, m_pMemory, 0);
}

VULKAN_BUFFER::~VULKAN_BUFFER ()
{
   if (m_pBuffer != VK_NULL_HANDLE)
      vkDestroyBuffer (m_pDevice, m_pBuffer, nullptr);
   if (m_pMemory != VK_NULL_HANDLE)
      vkFreeMemory (m_pDevice, m_pMemory, nullptr);
}

void VULKAN_BUFFER::SetData (const void* pSrc, size_t nSize, size_t nOffset)
{
   void* pMapped = nullptr;
   vkMapMemory (m_pDevice, m_pMemory, nOffset, nSize, 0, &pMapped);
   std::memcpy (pMapped, pSrc, nSize);
   vkUnmapMemory (m_pDevice, m_pMemory);
}

void VULKAN_BUFFER::GetData (void* pDst, size_t nSize, size_t nOffset)
{
   void* pMapped = nullptr;
   vkMapMemory (m_pDevice, m_pMemory, nOffset, nSize, 0, &pMapped);
   std::memcpy (pDst, pMapped, nSize);
   vkUnmapMemory (m_pDevice, m_pMemory);
}

// ---------------------------------------------------------------------------
// VULKAN_KERNEL
// ---------------------------------------------------------------------------

VULKAN_KERNEL::VULKAN_KERNEL (VkDevice pDevice, const void* pSpvBytes, size_t nSpvSize,
                              const char* szEntryPoint)
   : m_pDevice (pDevice)
   , m_pShaderModule (VK_NULL_HANDLE)
   , m_pPipelineLayout (VK_NULL_HANDLE)
   , m_pPipeline (VK_NULL_HANDLE)
   , m_pDescSetLayout (VK_NULL_HANDLE)
{
   VkShaderModuleCreateInfo modInfo = {};
   modInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
   modInfo.codeSize = nSpvSize;
   modInfo.pCode    = static_cast<const uint32_t*> (pSpvBytes);
   vkCreateShaderModule (pDevice, &modInfo, nullptr, &m_pShaderModule);

   // Descriptor set layout: 16 storage buffer bindings should cover typical kernels
   static const uint32_t MAX_BINDINGS = 16;
   VkDescriptorSetLayoutBinding aBindings[MAX_BINDINGS] = {};
   for (uint32_t i = 0; i < MAX_BINDINGS; i++)
   {
      aBindings[i].binding         = i;
      aBindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      aBindings[i].descriptorCount = 1;
      aBindings[i].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
   }

   VkDescriptorSetLayoutCreateInfo descLayoutInfo = {};
   descLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   descLayoutInfo.bindingCount = MAX_BINDINGS;
   descLayoutInfo.pBindings    = aBindings;
   vkCreateDescriptorSetLayout (pDevice, &descLayoutInfo, nullptr, &m_pDescSetLayout);

   VkPushConstantRange pushRange = {};
   pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
   pushRange.offset     = 0;
   pushRange.size       = 128;

   VkPipelineLayoutCreateInfo layoutInfo = {};
   layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
   layoutInfo.setLayoutCount         = 1;
   layoutInfo.pSetLayouts            = &m_pDescSetLayout;
   layoutInfo.pushConstantRangeCount = 1;
   layoutInfo.pPushConstantRanges    = &pushRange;
   vkCreatePipelineLayout (pDevice, &layoutInfo, nullptr, &m_pPipelineLayout);

   VkComputePipelineCreateInfo pipeInfo = {};
   pipeInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
   pipeInfo.layout = m_pPipelineLayout;
   pipeInfo.stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
   pipeInfo.stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
   pipeInfo.stage.module = m_pShaderModule;
   pipeInfo.stage.pName  = szEntryPoint;
   vkCreateComputePipelines (pDevice, VK_NULL_HANDLE, 1, &pipeInfo, nullptr, &m_pPipeline);
}

VULKAN_KERNEL::~VULKAN_KERNEL ()
{
   if (m_pPipeline != VK_NULL_HANDLE)
      vkDestroyPipeline (m_pDevice, m_pPipeline, nullptr);
   if (m_pPipelineLayout != VK_NULL_HANDLE)
      vkDestroyPipelineLayout (m_pDevice, m_pPipelineLayout, nullptr);
   if (m_pDescSetLayout != VK_NULL_HANDLE)
      vkDestroyDescriptorSetLayout (m_pDevice, m_pDescSetLayout, nullptr);
   if (m_pShaderModule != VK_NULL_HANDLE)
      vkDestroyShaderModule (m_pDevice, m_pShaderModule, nullptr);
}

// ---------------------------------------------------------------------------
// VULKAN_DEVICE
// ---------------------------------------------------------------------------

VULKAN_DEVICE::VULKAN_DEVICE ()
   : m_pInstance (VK_NULL_HANDLE)
   , m_pPhysicalDevice (VK_NULL_HANDLE)
   , m_pDevice (VK_NULL_HANDLE)
   , m_pQueue (VK_NULL_HANDLE)
   , m_nQueueFamily (0)
   , m_pCommandPool (VK_NULL_HANDLE)
   , m_pCommandBuffer (VK_NULL_HANDLE)
   , m_pFence (VK_NULL_HANDLE)
   , m_pDescPool (VK_NULL_HANDLE)
   , m_pDescSet (VK_NULL_HANDLE)
   , m_pCurrentKernel (nullptr)
   , m_nPushConstantSize (0)
{
   VkApplicationInfo appInfo = {};
   appInfo.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
   appInfo.pApplicationName = "Vox";
   appInfo.apiVersion       = VK_API_VERSION_1_0;

   VkInstanceCreateInfo instInfo = {};
   instInfo.sType            = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
   instInfo.pApplicationInfo = &appInfo;
   vkCreateInstance (&instInfo, nullptr, &m_pInstance);

   uint32_t nDevCount = 0;
   vkEnumeratePhysicalDevices (m_pInstance, &nDevCount, nullptr);
   if (nDevCount > 0)
   {
      std::vector<VkPhysicalDevice> aDevices (nDevCount);
      vkEnumeratePhysicalDevices (m_pInstance, &nDevCount, aDevices.data ());
      m_pPhysicalDevice = aDevices[0];
   }

   uint32_t nQueueFamilyCount = 0;
   vkGetPhysicalDeviceQueueFamilyProperties (m_pPhysicalDevice, &nQueueFamilyCount, nullptr);
   std::vector<VkQueueFamilyProperties> aQueueFamilies (nQueueFamilyCount);
   vkGetPhysicalDeviceQueueFamilyProperties (m_pPhysicalDevice, &nQueueFamilyCount,
                                             aQueueFamilies.data ());

   for (uint32_t i = 0; i < nQueueFamilyCount; i++)
   {
      if (aQueueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
      {
         m_nQueueFamily = i;
         break;
      }
   }

   float dQueuePriority = 1.0f;
   VkDeviceQueueCreateInfo queueInfo = {};
   queueInfo.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
   queueInfo.queueFamilyIndex = m_nQueueFamily;
   queueInfo.queueCount       = 1;
   queueInfo.pQueuePriorities = &dQueuePriority;

   VkDeviceCreateInfo devInfo = {};
   devInfo.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
   devInfo.queueCreateInfoCount = 1;
   devInfo.pQueueCreateInfos    = &queueInfo;
   vkCreateDevice (m_pPhysicalDevice, &devInfo, nullptr, &m_pDevice);

   vkGetDeviceQueue (m_pDevice, m_nQueueFamily, 0, &m_pQueue);

   VkCommandPoolCreateInfo poolInfo = {};
   poolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
   poolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
   poolInfo.queueFamilyIndex = m_nQueueFamily;
   vkCreateCommandPool (m_pDevice, &poolInfo, nullptr, &m_pCommandPool);

   VkCommandBufferAllocateInfo cmdInfo = {};
   cmdInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
   cmdInfo.commandPool        = m_pCommandPool;
   cmdInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
   cmdInfo.commandBufferCount = 1;
   vkAllocateCommandBuffers (m_pDevice, &cmdInfo, &m_pCommandBuffer);

   VkFenceCreateInfo fenceInfo = {};
   fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
   vkCreateFence (m_pDevice, &fenceInfo, nullptr, &m_pFence);

   VkDescriptorPoolSize poolSize = {};
   poolSize.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
   poolSize.descriptorCount = 16;

   VkDescriptorPoolCreateInfo descPoolInfo = {};
   descPoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   descPoolInfo.maxSets       = 1;
   descPoolInfo.poolSizeCount = 1;
   descPoolInfo.pPoolSizes    = &poolSize;
   vkCreateDescriptorPool (m_pDevice, &descPoolInfo, nullptr, &m_pDescPool);
}

VULKAN_DEVICE::~VULKAN_DEVICE ()
{
   if (m_pDevice != VK_NULL_HANDLE)
   {
      vkDeviceWaitIdle (m_pDevice);

      if (m_pDescPool != VK_NULL_HANDLE)
         vkDestroyDescriptorPool (m_pDevice, m_pDescPool, nullptr);
      if (m_pFence != VK_NULL_HANDLE)
         vkDestroyFence (m_pDevice, m_pFence, nullptr);
      if (m_pCommandPool != VK_NULL_HANDLE)
         vkDestroyCommandPool (m_pDevice, m_pCommandPool, nullptr);

      vkDestroyDevice (m_pDevice, nullptr);
   }
   if (m_pInstance != VK_NULL_HANDLE)
      vkDestroyInstance (m_pInstance, nullptr);
}

Backend VULKAN_DEVICE::GetBackend () const
{
   return Backend::Vulkan;
}

BUFFER* VULKAN_DEVICE::CreateBuffer (const BUFFER_DESC& desc)
{
   return new VULKAN_BUFFER (m_pDevice, m_pPhysicalDevice, desc);
}

KERNEL* VULKAN_DEVICE::CreateKernel (const void* pSpvBytes, size_t nSpvSize,
                                     const char* szEntryPoint)
{
   return new VULKAN_KERNEL (m_pDevice, pSpvBytes, nSpvSize, szEntryPoint);
}

void VULKAN_DEVICE::DestroyBuffer (BUFFER* pBuffer)
{
   delete static_cast<VULKAN_BUFFER*> (pBuffer);
}

void VULKAN_DEVICE::DestroyKernel (KERNEL* pKernel)
{
   delete static_cast<VULKAN_KERNEL*> (pKernel);
}

void VULKAN_DEVICE::SetKernel (KERNEL* pKernel)
{
   m_pCurrentKernel = static_cast<VULKAN_KERNEL*> (pKernel);
}

void VULKAN_DEVICE::SetBuffer (BUFFER* pBuffer, uint32_t nBinding, bool bReadOnly)
{
   BOUND_BUFFER bound;
   bound.pBuffer  = static_cast<VULKAN_BUFFER*> (pBuffer);
   bound.nBinding = nBinding;
   bound.bReadOnly = bReadOnly;
   m_aBoundBuffers.push_back (bound);
}

void VULKAN_DEVICE::SetPushConstants (const void* pData, size_t nSize)
{
   m_aPushConstantData.resize (1);
   m_aPushConstantData[0] = pData;
   m_nPushConstantSize = nSize;
}

void VULKAN_DEVICE::Dispatch (const DISPATCH_ARGS& args)
{
   if (!m_pCurrentKernel)
      return;

   VkDescriptorSetAllocateInfo descAllocInfo = {};
   descAllocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   descAllocInfo.descriptorPool     = m_pDescPool;
   descAllocInfo.descriptorSetCount = 1;
   VkDescriptorSetLayout pLayout   = m_pCurrentKernel->GetDescriptorSetLayout ();
   descAllocInfo.pSetLayouts        = &pLayout;

   vkResetDescriptorPool (m_pDevice, m_pDescPool, 0);
   vkAllocateDescriptorSets (m_pDevice, &descAllocInfo, &m_pDescSet);

   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      VkDescriptorBufferInfo bufDesc = {};
      bufDesc.buffer = m_aBoundBuffers[i].pBuffer->GetHandle ();
      bufDesc.offset = 0;
      bufDesc.range  = m_aBoundBuffers[i].pBuffer->GetSize ();

      VkWriteDescriptorSet write = {};
      write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet          = m_pDescSet;
      write.dstBinding      = m_aBoundBuffers[i].nBinding;
      write.descriptorCount = 1;
      write.descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      write.pBufferInfo     = &bufDesc;

      vkUpdateDescriptorSets (m_pDevice, 1, &write, 0, nullptr);
   }

   VkCommandBufferBeginInfo beginInfo = {};
   beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
   beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

   vkResetCommandBuffer (m_pCommandBuffer, 0);
   vkBeginCommandBuffer (m_pCommandBuffer, &beginInfo);

   vkCmdBindPipeline (m_pCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                      m_pCurrentKernel->GetPipeline ());
   vkCmdBindDescriptorSets (m_pCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                            m_pCurrentKernel->GetPipelineLayout (), 0, 1,
                            &m_pDescSet, 0, nullptr);

   if (m_nPushConstantSize > 0 && m_aPushConstantData[0])
   {
      vkCmdPushConstants (m_pCommandBuffer, m_pCurrentKernel->GetPipelineLayout (),
                          VK_SHADER_STAGE_COMPUTE_BIT, 0,
                          static_cast<uint32_t> (m_nPushConstantSize),
                          m_aPushConstantData[0]);
   }

   vkCmdDispatch (m_pCommandBuffer, args.nGroupsX, args.nGroupsY, args.nGroupsZ);
   vkEndCommandBuffer (m_pCommandBuffer);

   VkSubmitInfo submitInfo = {};
   submitInfo.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submitInfo.commandBufferCount = 1;
   submitInfo.pCommandBuffers    = &m_pCommandBuffer;

   vkResetFences (m_pDevice, 1, &m_pFence);
   vkQueueSubmit (m_pQueue, 1, &submitInfo, m_pFence);
}

void VULKAN_DEVICE::Finish ()
{
   vkWaitForFences (m_pDevice, 1, &m_pFence, VK_TRUE, UINT64_MAX);
   m_aBoundBuffers.clear ();
   m_nPushConstantSize = 0;
}

uint32_t VULKAN_DEVICE::FindMemoryType (uint32_t nTypeFilter,
                                        VkMemoryPropertyFlags nProperties)
{
   VkPhysicalDeviceMemoryProperties memProps;
   vkGetPhysicalDeviceMemoryProperties (m_pPhysicalDevice, &memProps);

   for (uint32_t i = 0; i < memProps.memoryTypeCount; i++)
   {
      if ((nTypeFilter & (1u << i)) &&
          (memProps.memoryTypes[i].propertyFlags & nProperties) == nProperties)
      {
         return i;
      }
   }

   return 0;
}

} // namespace vox
