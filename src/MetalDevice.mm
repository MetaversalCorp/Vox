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

#include "MetalDevice.h"

#import <Metal/Metal.h>

#include <spirv_cross/spirv_msl.hpp>
#include <cstring>
#include <string>
#include <vector>

namespace vox
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct MSL_RESULT
{
   std::string sSource;
   std::string sEntryPoint;

   struct BUFFER_BINDING
   {
      uint32_t nSpvBinding;
      uint32_t nMslIndex;
      bool     bReadOnly;
   };
   std::vector<BUFFER_BINDING> aBufferBindings;

   bool     bHasPushConstants;
   uint32_t nPushConstantIndex;
};

static MSL_RESULT CrossCompileToMSL (const void* pSpvBytes, size_t nSpvSize)
{
   MSL_RESULT result = {};

   std::vector<uint32_t> aSpirv (nSpvSize / sizeof (uint32_t));
   std::memcpy (aSpirv.data (), pSpvBytes, nSpvSize);

   spirv_cross::CompilerMSL compiler (std::move (aSpirv));

   spirv_cross::CompilerMSL::Options opts;
   opts.set_msl_version (2, 0);
   compiler.set_msl_options (opts);

   result.sSource = compiler.compile ();

   auto aEntryPoints = compiler.get_entry_points_and_stages ();
   for (auto& ep : aEntryPoints)
   {
      if (ep.execution_model == spv::ExecutionModelGLCompute)
      {
         result.sEntryPoint = compiler.get_cleansed_entry_point_name (
            ep.name, ep.execution_model);
         break;
      }
   }

   spirv_cross::ShaderResources resources = compiler.get_shader_resources ();

   for (auto& res : resources.storage_buffers)
   {
      uint32_t nSpvBinding = compiler.get_decoration (res.id, spv::DecorationBinding);
      uint32_t nMslIndex   = compiler.get_automatic_msl_resource_binding (res.id);

      spirv_cross::Bitset flags = compiler.get_buffer_block_flags (res.id);

      MSL_RESULT::BUFFER_BINDING bb;
      bb.nSpvBinding = nSpvBinding;
      bb.nMslIndex   = nMslIndex;
      bb.bReadOnly   = flags.get (spv::DecorationNonWritable);
      result.aBufferBindings.push_back (bb);
   }

   result.bHasPushConstants = !resources.push_constant_buffers.empty ();
   if (result.bHasPushConstants)
   {
      result.nPushConstantIndex = compiler.get_automatic_msl_resource_binding (
         resources.push_constant_buffers[0].id);
   }

   return result;
}

// ---------------------------------------------------------------------------
// METAL_BUFFER
// ---------------------------------------------------------------------------

METAL_BUFFER::METAL_BUFFER (void* pDevice, const BUFFER_DESC& desc)
   : m_pBuffer (nullptr)
   , m_nSize (desc.nSize)
{
   id<MTLDevice> device = (__bridge id<MTLDevice>) pDevice;
   id<MTLBuffer> buffer = [device newBufferWithLength: desc.nSize
                                  options: MTLResourceStorageModeShared];
   m_pBuffer = (__bridge_retained void*) buffer;
}

METAL_BUFFER::~METAL_BUFFER ()
{
   if (m_pBuffer)
   {
      id<MTLBuffer> buffer = (__bridge_transfer id<MTLBuffer>) m_pBuffer;
      buffer = nil;
   }
}

void METAL_BUFFER::SetData (const void* pSrc, size_t nSize, size_t nOffset)
{
   id<MTLBuffer> buffer = (__bridge id<MTLBuffer>) m_pBuffer;
   std::memcpy ((uint8_t*) [buffer contents] + nOffset, pSrc, nSize);
}

void METAL_BUFFER::GetData (void* pDst, size_t nSize, size_t nOffset)
{
   id<MTLBuffer> buffer = (__bridge id<MTLBuffer>) m_pBuffer;
   std::memcpy (pDst, (const uint8_t*) [buffer contents] + nOffset, nSize);
}

// ---------------------------------------------------------------------------
// METAL_KERNEL
// ---------------------------------------------------------------------------

METAL_KERNEL::METAL_KERNEL (void* pDevice, const void* pSpvBytes, size_t nSpvSize,
                            const char* szEntryPoint)
   : m_pPipelineState (nullptr)
   , m_bHasPushConstants (false)
   , m_nPushConstantIndex (-1)
{
   id<MTLDevice> device = (__bridge id<MTLDevice>) pDevice;

   MSL_RESULT msl = CrossCompileToMSL (pSpvBytes, nSpvSize);
   if (msl.sSource.empty ())
      return;

   m_bHasPushConstants  = msl.bHasPushConstants;
   m_nPushConstantIndex = msl.bHasPushConstants
      ? static_cast<int> (msl.nPushConstantIndex) : -1;

   for (auto& bb : msl.aBufferBindings)
   {
      BINDING_MAP bm;
      bm.nSpvBinding = bb.nSpvBinding;
      bm.nMslIndex   = bb.nMslIndex;
      bm.bReadOnly   = bb.bReadOnly;
      m_aBindingMap.push_back (bm);
   }

   NSString* pSource = [[NSString alloc] initWithUTF8String: msl.sSource.c_str ()];

   NSError* pError = nil;
   id<MTLLibrary> library = [device newLibraryWithSource: pSource
                                     options: nil
                                     error: &pError];
   if (!library)
      return;

   const char* szEntry = msl.sEntryPoint.empty () ? szEntryPoint
                                                   : msl.sEntryPoint.c_str ();
   NSString* pEntryName = [[NSString alloc] initWithUTF8String: szEntry];
   id<MTLFunction> function = [library newFunctionWithName: pEntryName];
   if (!function)
      return;

   id<MTLComputePipelineState> pipeline =
      [device newComputePipelineStateWithFunction: function error: &pError];

   m_pPipelineState = (__bridge_retained void*) pipeline;
}

int METAL_KERNEL::GetMslIndexForBinding (uint32_t nSpvBinding, bool bReadOnly) const
{
   for (size_t i = 0; i < m_aBindingMap.size (); i++)
   {
      if (m_aBindingMap[i].nSpvBinding == nSpvBinding &&
          m_aBindingMap[i].bReadOnly == bReadOnly)
         return static_cast<int> (m_aBindingMap[i].nMslIndex);
   }
   return -1;
}

int METAL_KERNEL::GetPushConstantIndex () const
{
   return m_nPushConstantIndex;
}

METAL_KERNEL::~METAL_KERNEL ()
{
   if (m_pPipelineState)
   {
      id<MTLComputePipelineState> pipeline =
         (__bridge_transfer id<MTLComputePipelineState>) m_pPipelineState;
      pipeline = nil;
   }
}

// ---------------------------------------------------------------------------
// METAL_DEVICE
// ---------------------------------------------------------------------------

METAL_DEVICE::METAL_DEVICE ()
   : m_pDevice (nullptr)
   , m_pCommandQueue (nullptr)
   , m_pCurrentKernel (nullptr)
{
   id<MTLDevice> device = MTLCreateSystemDefaultDevice ();
   m_pDevice = (__bridge_retained void*) device;

   id<MTLCommandQueue> queue = [device newCommandQueue];
   m_pCommandQueue = (__bridge_retained void*) queue;
}

METAL_DEVICE::~METAL_DEVICE ()
{
   if (m_pCommandQueue)
   {
      id<MTLCommandQueue> queue = (__bridge_transfer id<MTLCommandQueue>) m_pCommandQueue;
      queue = nil;
   }
   if (m_pDevice)
   {
      id<MTLDevice> device = (__bridge_transfer id<MTLDevice>) m_pDevice;
      device = nil;
   }
}

Backend METAL_DEVICE::GetBackend () const
{
   return Backend::Metal;
}

BUFFER* METAL_DEVICE::CreateBuffer (const BUFFER_DESC& desc)
{
   return new METAL_BUFFER (m_pDevice, desc);
}

KERNEL* METAL_DEVICE::CreateKernel (const void* pSpvBytes, size_t nSpvSize,
                                    const char* szEntryPoint)
{
   return new METAL_KERNEL (m_pDevice, pSpvBytes, nSpvSize, szEntryPoint);
}

void METAL_DEVICE::DestroyBuffer (BUFFER* pBuffer)
{
   delete static_cast<METAL_BUFFER*> (pBuffer);
}

void METAL_DEVICE::DestroyKernel (KERNEL* pKernel)
{
   delete static_cast<METAL_KERNEL*> (pKernel);
}

void METAL_DEVICE::SetKernel (KERNEL* pKernel)
{
   m_pCurrentKernel = static_cast<METAL_KERNEL*> (pKernel);
}

void METAL_DEVICE::SetBuffer (BUFFER* pBuffer, uint32_t nBinding, bool bReadOnly)
{
   BOUND_BUFFER bound;
   bound.pBuffer   = static_cast<METAL_BUFFER*> (pBuffer);
   bound.nBinding  = nBinding;
   bound.bReadOnly = bReadOnly;
   m_aBoundBuffers.push_back (bound);
}

void METAL_DEVICE::SetPushConstants (const void* pData, size_t nSize)
{
   m_aPushConstantData.resize (nSize);
   std::memcpy (m_aPushConstantData.data (), pData, nSize);
}

void METAL_DEVICE::Dispatch (const DISPATCH_ARGS& args)
{
   if (!m_pCurrentKernel || !m_pCurrentKernel->IsValid ())
      return;

   id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>) m_pCommandQueue;
   id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
   id<MTLComputeCommandEncoder> encoder = [cmdBuf computeCommandEncoder];

   id<MTLComputePipelineState> pipeline =
      (__bridge id<MTLComputePipelineState>) m_pCurrentKernel->GetPipelineState ();
   [encoder setComputePipelineState: pipeline];

   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      int nMslIndex = m_pCurrentKernel->GetMslIndexForBinding (
         m_aBoundBuffers[i].nBinding, m_aBoundBuffers[i].bReadOnly);

      if (nMslIndex < 0)
         continue;

      id<MTLBuffer> buffer = (__bridge id<MTLBuffer>) m_aBoundBuffers[i].pBuffer->GetHandle ();
      [encoder setBuffer: buffer offset: 0 atIndex: static_cast<NSUInteger> (nMslIndex)];
   }

   if (m_pCurrentKernel->HasPushConstants () && !m_aPushConstantData.empty ())
   {
      int nPcIndex = m_pCurrentKernel->GetPushConstantIndex ();
      if (nPcIndex >= 0)
      {
         [encoder setBytes: m_aPushConstantData.data ()
                    length: m_aPushConstantData.size ()
                   atIndex: static_cast<NSUInteger> (nPcIndex)];
      }
   }

   NSUInteger nThreadsPerGroup = [pipeline maxTotalThreadsPerThreadgroup];
   MTLSize threadGroupSize  = MTLSizeMake (nThreadsPerGroup, 1, 1);
   MTLSize threadGroupCount = MTLSizeMake (args.nGroupsX, args.nGroupsY, args.nGroupsZ);

   [encoder dispatchThreadgroups: threadGroupCount
            threadsPerThreadgroup: threadGroupSize];
   [encoder endEncoding];
   [cmdBuf commit];
   [cmdBuf waitUntilCompleted];
}

void METAL_DEVICE::Finish ()
{
   m_aBoundBuffers.clear ();
   m_aPushConstantData.clear ();
}

} // namespace vox
