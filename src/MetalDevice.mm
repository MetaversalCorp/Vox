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

static std::string CrossCompileToMSL (const void* pSpvBytes, size_t nSpvSize)
{
   std::vector<uint32_t> aSpirv (nSpvSize / sizeof (uint32_t));
   std::memcpy (aSpirv.data (), pSpvBytes, nSpvSize);

   spirv_cross::CompilerMSL compiler (std::move (aSpirv));

   spirv_cross::CompilerMSL::Options opts;
   opts.set_msl_version (2, 0);
   compiler.set_msl_options (opts);

   return compiler.compile ();
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
{
   id<MTLDevice> device = (__bridge id<MTLDevice>) pDevice;

   std::string sMSL = CrossCompileToMSL (pSpvBytes, nSpvSize);
   NSString* pSource = [[NSString alloc] initWithUTF8String: sMSL.c_str ()];

   NSError* pError = nil;
   id<MTLLibrary> library = [device newLibraryWithSource: pSource
                                     options: nil
                                     error: &pError];

   NSString* pEntryName = [[NSString alloc] initWithUTF8String: szEntryPoint];
   id<MTLFunction> function = [library newFunctionWithName: pEntryName];

   id<MTLComputePipelineState> pipeline =
      [device newComputePipelineStateWithFunction: function error: &pError];

   m_pPipelineState = (__bridge_retained void*) pipeline;
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
   if (!m_pCurrentKernel)
      return;

   id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>) m_pCommandQueue;
   id<MTLCommandBuffer> cmdBuf = [queue commandBuffer];
   id<MTLComputeCommandEncoder> encoder = [cmdBuf computeCommandEncoder];

   id<MTLComputePipelineState> pipeline =
      (__bridge id<MTLComputePipelineState>) m_pCurrentKernel->GetPipelineState ();
   [encoder setComputePipelineState: pipeline];

   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      id<MTLBuffer> buffer = (__bridge id<MTLBuffer>) m_aBoundBuffers[i].pBuffer->GetHandle ();
      [encoder setBuffer: buffer offset: 0 atIndex: m_aBoundBuffers[i].nBinding];
   }

   if (!m_aPushConstantData.empty ())
   {
      [encoder setBytes: m_aPushConstantData.data ()
                 length: m_aPushConstantData.size ()
                atIndex: m_aBoundBuffers.size ()];
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
