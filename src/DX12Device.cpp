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

#include "DX12Device.h"

#include <d3dcompiler.h>
#include <spirv_cross/spirv_hlsl.hpp>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

namespace vox
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct RESOURCE_BINDING
{
   uint32_t nBinding;
   bool     bReadOnly;
};

struct HLSL_RESULT
{
   std::string                   sSource;
   std::vector<RESOURCE_BINDING> aSrvBindings;
   std::vector<RESOURCE_BINDING> aUavBindings;
   bool                          bHasPushConstants;
   uint32_t                      nPushConstantSize;
};

static HLSL_RESULT CrossCompileToHLSL (const void* pSpvBytes, size_t nSpvSize)
{
   HLSL_RESULT result = {};

   std::vector<uint32_t> aSpirv (nSpvSize / sizeof (uint32_t));
   std::memcpy (aSpirv.data (), pSpvBytes, nSpvSize);

   spirv_cross::CompilerHLSL compiler (std::move (aSpirv));

   spirv_cross::CompilerHLSL::Options opts;
   opts.shader_model = 50;
   compiler.set_hlsl_options (opts);

   result.sSource = compiler.compile ();

   spirv_cross::ShaderResources resources = compiler.get_shader_resources ();

   for (auto& res : resources.storage_buffers)
   {
      uint32_t nBinding = compiler.get_decoration (res.id, spv::DecorationBinding);
      spirv_cross::Bitset flags = compiler.get_buffer_block_flags (res.id);
      RESOURCE_BINDING rb = { nBinding, flags.get (spv::DecorationNonWritable) };

      if (rb.bReadOnly)
         result.aSrvBindings.push_back (rb);
      else
         result.aUavBindings.push_back (rb);
   }

   result.bHasPushConstants = !resources.push_constant_buffers.empty ();
   if (result.bHasPushConstants)
   {
      auto& pcType = compiler.get_type (resources.push_constant_buffers[0].base_type_id);
      result.nPushConstantSize = static_cast<uint32_t> (compiler.get_declared_struct_size (pcType));
   }

   return result;
}

// ---------------------------------------------------------------------------
// DX12_BUFFER
// ---------------------------------------------------------------------------

DX12_BUFFER::DX12_BUFFER (ID3D12Device* pDevice, const BUFFER_DESC& desc)
   : m_nSize (desc.nSize)
   , m_aShadow (desc.nSize, 0)
   , m_bHasGpuResults (false)
{
   D3D12_HEAP_PROPERTIES defaultHeap = {};
   defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

   D3D12_RESOURCE_DESC resDesc = {};
   resDesc.Dimension        = D3D12_RESOURCE_DIMENSION_BUFFER;
   resDesc.Width            = desc.nSize;
   resDesc.Height           = 1;
   resDesc.DepthOrArraySize = 1;
   resDesc.MipLevels        = 1;
   resDesc.SampleDesc.Count = 1;
   resDesc.Layout           = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
   resDesc.Flags            = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

   pDevice->CreateCommittedResource (&defaultHeap, D3D12_HEAP_FLAG_NONE, &resDesc,
      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS (&m_pDefault));

   D3D12_HEAP_PROPERTIES uploadHeap = {};
   uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

   D3D12_RESOURCE_DESC uploadDesc = resDesc;
   uploadDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

   pDevice->CreateCommittedResource (&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
      D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS (&m_pUpload));

   D3D12_HEAP_PROPERTIES readbackHeap = {};
   readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;

   pDevice->CreateCommittedResource (&readbackHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc,
      D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS (&m_pReadback));
}

DX12_BUFFER::~DX12_BUFFER ()
{
}

void DX12_BUFFER::SetData (const void* pSrc, size_t nSize, size_t nOffset)
{
   std::memcpy (m_aShadow.data () + nOffset, pSrc, nSize);

   void* pMapped = nullptr;
   D3D12_RANGE readRange = { 0, 0 };
   m_pUpload->Map (0, &readRange, &pMapped);
   std::memcpy (static_cast<uint8_t*> (pMapped) + nOffset, pSrc, nSize);
   m_pUpload->Unmap (0, nullptr);
}

void DX12_BUFFER::GetData (void* pDst, size_t nSize, size_t nOffset)
{
   if (m_bHasGpuResults)
   {
      void* pMapped = nullptr;
      D3D12_RANGE readRange = { nOffset, nOffset + nSize };
      m_pReadback->Map (0, &readRange, &pMapped);
      std::memcpy (pDst, static_cast<const uint8_t*> (pMapped) + nOffset, nSize);
      D3D12_RANGE writeRange = { 0, 0 };
      m_pReadback->Unmap (0, &writeRange);
   }
   else
   {
      std::memcpy (pDst, m_aShadow.data () + nOffset, nSize);
   }
}

void DX12_BUFFER::MarkGpuResults ()
{
   m_bHasGpuResults = true;
}

// ---------------------------------------------------------------------------
// DX12_KERNEL
// ---------------------------------------------------------------------------

DX12_KERNEL::DX12_KERNEL (ID3D12Device* pDevice, const void* pSpvBytes, size_t nSpvSize,
                          const char* szEntryPoint)
   : m_bHasPushConstants (false)
   , m_nPushConstantDwords (0)
{
   HLSL_RESULT hlsl = CrossCompileToHLSL (pSpvBytes, nSpvSize);

   m_bHasPushConstants  = hlsl.bHasPushConstants;
   m_nPushConstantDwords = (hlsl.nPushConstantSize + 3) / 4;

   ComPtr<ID3DBlob> pBytecode;
   ComPtr<ID3DBlob> pErrors;
   HRESULT hr = D3DCompile (hlsl.sSource.c_str (), hlsl.sSource.size (), nullptr,
                            nullptr, nullptr, szEntryPoint, "cs_5_0", 0, 0,
                            &pBytecode, &pErrors);
   if (FAILED (hr))
   {
      if (pErrors)
         std::printf ("  [Vox DX12] D3DCompile error: %s\n", (const char*) pErrors->GetBufferPointer ());
      return;
   }

   // Build root signature dynamically from SPIRV-Cross reflection:
   //   Param 0: root constants (push constants) at b0
   //   Param 1+: root SRVs/UAVs using actual HLSL register numbers from SPIRV-Cross
   std::vector<D3D12_ROOT_PARAMETER> aParams;

   D3D12_ROOT_PARAMETER pcParam = {};
   pcParam.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
   pcParam.Constants.ShaderRegister  = 0;
   pcParam.Constants.RegisterSpace   = 0;
   pcParam.Constants.Num32BitValues  = (m_nPushConstantDwords > 0) ? m_nPushConstantDwords : 1;
   pcParam.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
   aParams.push_back (pcParam);

   for (size_t i = 0; i < hlsl.aSrvBindings.size (); i++)
   {
      D3D12_ROOT_PARAMETER srvParam = {};
      srvParam.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_SRV;
      srvParam.Descriptor.ShaderRegister = hlsl.aSrvBindings[i].nBinding;
      srvParam.Descriptor.RegisterSpace  = 0;
      srvParam.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
      uint32_t nRootIdx = static_cast<uint32_t> (aParams.size ());
      aParams.push_back (srvParam);

      PARAM_MAP pm = { hlsl.aSrvBindings[i].nBinding, nRootIdx, true };
      m_aParamMap.push_back (pm);
   }

   for (size_t i = 0; i < hlsl.aUavBindings.size (); i++)
   {
      D3D12_ROOT_PARAMETER uavParam = {};
      uavParam.ParameterType             = D3D12_ROOT_PARAMETER_TYPE_UAV;
      uavParam.Descriptor.ShaderRegister = hlsl.aUavBindings[i].nBinding;
      uavParam.Descriptor.RegisterSpace  = 0;
      uavParam.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;
      uint32_t nRootIdx = static_cast<uint32_t> (aParams.size ());
      aParams.push_back (uavParam);

      PARAM_MAP pm = { hlsl.aUavBindings[i].nBinding, nRootIdx, false };
      m_aParamMap.push_back (pm);
   }

   D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
   rootDesc.NumParameters = static_cast<UINT> (aParams.size ());
   rootDesc.pParameters   = aParams.data ();

   ComPtr<ID3DBlob> pSigBlob;
   ComPtr<ID3DBlob> pSigErrors;
   hr = D3D12SerializeRootSignature (&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                     &pSigBlob, &pSigErrors);
   if (FAILED (hr))
      return;

   pDevice->CreateRootSignature (0, pSigBlob->GetBufferPointer (),
      pSigBlob->GetBufferSize (), IID_PPV_ARGS (&m_pRootSignature));

   D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
   psoDesc.pRootSignature     = m_pRootSignature.Get ();
   psoDesc.CS.pShaderBytecode = pBytecode->GetBufferPointer ();
   psoDesc.CS.BytecodeLength  = pBytecode->GetBufferSize ();
   pDevice->CreateComputePipelineState (&psoDesc, IID_PPV_ARGS (&m_pPipelineState));
}

int DX12_KERNEL::GetRootParamForBinding (uint32_t nBinding, bool bReadOnly) const
{
   for (size_t i = 0; i < m_aParamMap.size (); i++)
   {
      if (m_aParamMap[i].nBinding == nBinding && m_aParamMap[i].bReadOnly == bReadOnly)
         return static_cast<int> (m_aParamMap[i].nRootParam);
   }
   return -1;
}

DX12_KERNEL::~DX12_KERNEL ()
{
}

// ---------------------------------------------------------------------------
// DX12_DEVICE
// ---------------------------------------------------------------------------

DX12_DEVICE::DX12_DEVICE ()
   : m_pFenceEvent (nullptr)
   , m_nFenceValue (0)
   , m_pCurrentKernel (nullptr)
{
   ComPtr<IDXGIFactory4> pFactory;
   CreateDXGIFactory1 (IID_PPV_ARGS (&pFactory));

   ComPtr<IDXGIAdapter1> pAdapter;
   pFactory->EnumAdapters1 (0, &pAdapter);

   D3D12CreateDevice (pAdapter.Get (), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS (&m_pDevice));

   D3D12_COMMAND_QUEUE_DESC queueDesc = {};
   queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
   m_pDevice->CreateCommandQueue (&queueDesc, IID_PPV_ARGS (&m_pCommandQueue));

   m_pDevice->CreateCommandAllocator (D3D12_COMMAND_LIST_TYPE_COMPUTE,
      IID_PPV_ARGS (&m_pCommandAllocator));

   m_pDevice->CreateCommandList (0, D3D12_COMMAND_LIST_TYPE_COMPUTE,
      m_pCommandAllocator.Get (), nullptr, IID_PPV_ARGS (&m_pCommandList));
   m_pCommandList->Close ();

   m_pDevice->CreateFence (0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS (&m_pFence));
   m_pFenceEvent = CreateEvent (nullptr, FALSE, FALSE, nullptr);
}

DX12_DEVICE::~DX12_DEVICE ()
{
   if (m_pFenceEvent)
      CloseHandle (m_pFenceEvent);
}

Backend DX12_DEVICE::GetBackend () const
{
   return Backend::DX12;
}

BUFFER* DX12_DEVICE::CreateBuffer (const BUFFER_DESC& desc)
{
   return new DX12_BUFFER (m_pDevice.Get (), desc);
}

KERNEL* DX12_DEVICE::CreateKernel (const void* pSpvBytes, size_t nSpvSize,
                                   const char* szEntryPoint)
{
   return new DX12_KERNEL (m_pDevice.Get (), pSpvBytes, nSpvSize, szEntryPoint);
}

void DX12_DEVICE::DestroyBuffer (BUFFER* pBuffer)
{
   delete static_cast<DX12_BUFFER*> (pBuffer);
}

void DX12_DEVICE::DestroyKernel (KERNEL* pKernel)
{
   delete static_cast<DX12_KERNEL*> (pKernel);
}

void DX12_DEVICE::SetKernel (KERNEL* pKernel)
{
   m_pCurrentKernel = static_cast<DX12_KERNEL*> (pKernel);
}

void DX12_DEVICE::SetBuffer (BUFFER* pBuffer, uint32_t nBinding, bool bReadOnly)
{
   BOUND_BUFFER bound;
   bound.pBuffer   = static_cast<DX12_BUFFER*> (pBuffer);
   bound.nBinding  = nBinding;
   bound.bReadOnly = bReadOnly;
   m_aBoundBuffers.push_back (bound);
}

void DX12_DEVICE::SetPushConstants (const void* pData, size_t nSize)
{
   m_aPushConstantData.resize (nSize);
   std::memcpy (m_aPushConstantData.data (), pData, nSize);
}

void DX12_DEVICE::Dispatch (const DISPATCH_ARGS& args)
{
   if (!m_pCurrentKernel || !m_pCurrentKernel->IsValid ())
      return;

   m_pCommandAllocator->Reset ();
   m_pCommandList->Reset (m_pCommandAllocator.Get (), m_pCurrentKernel->GetPipelineState ());
   m_pCommandList->SetComputeRootSignature (m_pCurrentKernel->GetRootSignature ());

   if (m_pCurrentKernel->HasPushConstants () && !m_aPushConstantData.empty ())
   {
      m_pCommandList->SetComputeRoot32BitConstants (
         m_pCurrentKernel->GetPushConstantParamIndex (),
         static_cast<UINT> (m_aPushConstantData.size () / 4),
         m_aPushConstantData.data (), 0);
   }

   // Copy upload -> default for all buffers
   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      DX12_BUFFER* pBuf = m_aBoundBuffers[i].pBuffer;

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource   = pBuf->GetDefaultResource ();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
      barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_pCommandList->ResourceBarrier (1, &barrier);

      m_pCommandList->CopyResource (pBuf->GetDefaultResource (),
                                    pBuf->GetUploadResource ());

      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
      barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      m_pCommandList->ResourceBarrier (1, &barrier);
   }

   // Bind buffers as root SRVs/UAVs using the kernel's binding map
   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      DX12_BUFFER* pBuf = m_aBoundBuffers[i].pBuffer;
      D3D12_GPU_VIRTUAL_ADDRESS gpuAddr = pBuf->GetDefaultResource ()->GetGPUVirtualAddress ();

      int nParam = m_pCurrentKernel->GetRootParamForBinding (
         m_aBoundBuffers[i].nBinding, m_aBoundBuffers[i].bReadOnly);

      if (nParam < 0)
         continue;

      if (m_aBoundBuffers[i].bReadOnly)
         m_pCommandList->SetComputeRootShaderResourceView (nParam, gpuAddr);
      else
         m_pCommandList->SetComputeRootUnorderedAccessView (nParam, gpuAddr);
   }

   m_pCommandList->Dispatch (args.nGroupsX, args.nGroupsY, args.nGroupsZ);

   // Copy default -> readback for output buffers
   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
   {
      DX12_BUFFER* pBuf = m_aBoundBuffers[i].pBuffer;

      D3D12_RESOURCE_BARRIER barrier = {};
      barrier.Type  = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
      barrier.Transition.pResource   = pBuf->GetDefaultResource ();
      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
      barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
      barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
      m_pCommandList->ResourceBarrier (1, &barrier);

      m_pCommandList->CopyResource (pBuf->GetReadbackResource (),
                                    pBuf->GetDefaultResource ());

      barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
      barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;
      m_pCommandList->ResourceBarrier (1, &barrier);
   }

   m_pCommandList->Close ();

   ID3D12CommandList* aLists[] = { m_pCommandList.Get () };
   m_pCommandQueue->ExecuteCommandLists (1, aLists);

   m_nFenceValue++;
   m_pCommandQueue->Signal (m_pFence.Get (), m_nFenceValue);
}

void DX12_DEVICE::Finish ()
{
   if (m_pFence->GetCompletedValue () < m_nFenceValue)
   {
      m_pFence->SetEventOnCompletion (m_nFenceValue, m_pFenceEvent);
      WaitForSingleObject (m_pFenceEvent, INFINITE);
   }

   for (size_t i = 0; i < m_aBoundBuffers.size (); i++)
      m_aBoundBuffers[i].pBuffer->MarkGpuResults ();

   m_aBoundBuffers.clear ();
   m_aPushConstantData.clear ();
}

} // namespace vox
