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

#ifndef VOX_DX12_DEVICE_H
#define VOX_DX12_DEVICE_H

#include <vox/Vox.h>

#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace vox
{

class DX12_BUFFER : public BUFFER
{
public:
   DX12_BUFFER (ID3D12Device* pDevice, const BUFFER_DESC& desc);
   ~DX12_BUFFER () override;

   void SetData (const void* pSrc, size_t nSize, size_t nOffset = 0) override;
   void GetData (void* pDst, size_t nSize, size_t nOffset = 0) override;

   ID3D12Resource* GetDefaultResource () const { return m_pDefault.Get (); }
   ID3D12Resource* GetUploadResource () const   { return m_pUpload.Get (); }
   ID3D12Resource* GetReadbackResource () const { return m_pReadback.Get (); }
   size_t          GetSize () const             { return m_nSize; }

private:
   ComPtr<ID3D12Resource> m_pDefault;
   ComPtr<ID3D12Resource> m_pUpload;
   ComPtr<ID3D12Resource> m_pReadback;
   size_t                 m_nSize;
};

class DX12_KERNEL : public KERNEL
{
public:
   DX12_KERNEL (ID3D12Device* pDevice, const void* pSpvBytes, size_t nSpvSize,
                const char* szEntryPoint);
   ~DX12_KERNEL () override;

   ID3D12PipelineState* GetPipelineState () const { return m_pPipelineState.Get (); }
   ID3D12RootSignature* GetRootSignature () const { return m_pRootSignature.Get (); }

private:
   ComPtr<ID3D12PipelineState> m_pPipelineState;
   ComPtr<ID3D12RootSignature> m_pRootSignature;
};

class DX12_DEVICE : public DEVICE
{
public:
   DX12_DEVICE ();
   ~DX12_DEVICE () override;

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
   ComPtr<ID3D12Device>              m_pDevice;
   ComPtr<ID3D12CommandQueue>        m_pCommandQueue;
   ComPtr<ID3D12CommandAllocator>    m_pCommandAllocator;
   ComPtr<ID3D12GraphicsCommandList> m_pCommandList;
   ComPtr<ID3D12Fence>               m_pFence;
   HANDLE                            m_pFenceEvent;
   UINT64                            m_nFenceValue;

   DX12_KERNEL*                      m_pCurrentKernel;

   struct BOUND_BUFFER
   {
      DX12_BUFFER* pBuffer;
      uint32_t     nBinding;
      bool         bReadOnly;
   };
   std::vector<BOUND_BUFFER> m_aBoundBuffers;

   std::vector<uint8_t> m_aPushConstantData;
};

} // namespace vox

#endif // VOX_DX12_DEVICE_H
