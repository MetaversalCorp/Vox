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
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>

static int nTotal  = 0;
static int nPassed = 0;

#define TEST_ASSERT(expr) \
   do { \
      nTotal++; \
      if (expr) { nPassed++; } \
      else { std::printf ("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
   } while (0)

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

static void TestCreateDevice ()
{
   std::printf ("--- CreateDevice ---\n");

   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::Auto);
   TEST_ASSERT (pDevice != nullptr);

   if (pDevice)
   {
      vox::Backend eBackend = pDevice->GetBackend ();
      TEST_ASSERT (eBackend == vox::Backend::Vulkan ||
                   eBackend == vox::Backend::DX12 ||
                   eBackend == vox::Backend::Metal);

      const char* szName = "Unknown";
      switch (eBackend)
      {
         case vox::Backend::Vulkan: szName = "Vulkan"; break;
         case vox::Backend::DX12:   szName = "DX12";   break;
         case vox::Backend::Metal:  szName = "Metal";  break;
         default: break;
      }
      std::printf ("  Backend: %s\n", szName);

      delete pDevice;
   }
}

static void TestCreateBuffer ()
{
   std::printf ("--- CreateBuffer ---\n");

   vox::DEVICE* pDevice = vox::DEVICE::Create ();
   if (!pDevice)
   {
      std::printf ("  SKIP: no GPU backend available\n");
      return;
   }

   vox::BUFFER_DESC desc;
   desc.nSize       = 1024;
   desc.bHostVisible = true;

   vox::BUFFER* pBuffer = pDevice->CreateBuffer (desc);
   TEST_ASSERT (pBuffer != nullptr);

   float aData[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
   pBuffer->SetData (aData, sizeof (aData));

   float aReadback[4] = {};
   pBuffer->GetData (aReadback, sizeof (aReadback));

   TEST_ASSERT (aReadback[0] == 1.0f);
   TEST_ASSERT (aReadback[1] == 2.0f);
   TEST_ASSERT (aReadback[2] == 3.0f);
   TEST_ASSERT (aReadback[3] == 4.0f);

   pDevice->DestroyBuffer (pBuffer);
   delete pDevice;
}

static void TestNullDevice ()
{
   std::printf ("--- NullDevice ---\n");

   // Request a backend that doesn't exist on this platform
#ifdef __APPLE__
   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::DX12);
#elif defined(_WIN32)
   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::Metal);
#else
   vox::DEVICE* pDevice = vox::DEVICE::Create (vox::Backend::Metal);
#endif

   TEST_ASSERT (pDevice == nullptr);
}

static std::vector<uint8_t> LoadFile (const char* szPath)
{
   std::vector<uint8_t> aBytes;
   FILE* pFile = std::fopen (szPath, "rb");
   if (!pFile)
      return aBytes;

   std::fseek (pFile, 0, SEEK_END);
   long nSize = std::ftell (pFile);
   std::fseek (pFile, 0, SEEK_SET);

   aBytes.resize (static_cast<size_t> (nSize));
   std::fread (aBytes.data (), 1, aBytes.size (), pFile);
   std::fclose (pFile);

   return aBytes;
}

static void TestComputeDispatch (const char* szSpvPath)
{
   std::printf ("--- ComputeDispatch (proximity kernel) ---\n");

   std::vector<uint8_t> aSpv = LoadFile (szSpvPath);
   if (aSpv.empty ())
   {
      std::printf ("  SKIP: could not load %s\n", szSpvPath);
      return;
   }
   std::printf ("  Loaded %zu bytes of SPIR-V\n", aSpv.size ());
   TEST_ASSERT (aSpv.size () > 16);

   vox::DEVICE* pDevice = vox::DEVICE::Create ();
   if (!pDevice)
   {
      std::printf ("  SKIP: no GPU backend available\n");
      return;
   }

   // The proximity shader computes: distance(position.xyz, queryPoint.xyz)
   // for each element. We'll test with 4 positions queried from the origin.
   //
   // Positions (vec4 each):
   //   (3,0,0,0) -> distance = 3.0
   //   (0,4,0,0) -> distance = 4.0
   //   (0,0,5,0) -> distance = 5.0
   //   (1,1,1,0) -> distance = sqrt(3) ~ 1.732

   static const uint32_t N_COUNT = 4;
   float aPositions[N_COUNT * 4] =
   {
      3.0f, 0.0f, 0.0f, 0.0f,
      0.0f, 4.0f, 0.0f, 0.0f,
      0.0f, 0.0f, 5.0f, 0.0f,
      1.0f, 1.0f, 1.0f, 0.0f,
   };

   struct PUSH_CONSTANTS
   {
      float    dX, dY, dZ, dW;
      uint32_t nCount;
   };
   PUSH_CONSTANTS pc = { 0.0f, 0.0f, 0.0f, 0.0f, N_COUNT };

   vox::BUFFER_DESC inputDesc  = { sizeof (aPositions), true };
   vox::BUFFER_DESC outputDesc = { N_COUNT * sizeof (float), true };

   vox::BUFFER* pInput  = pDevice->CreateBuffer (inputDesc);
   vox::BUFFER* pOutput = pDevice->CreateBuffer (outputDesc);

   pInput->SetData (aPositions, sizeof (aPositions));

   vox::KERNEL* pKernel = pDevice->CreateKernel (aSpv.data (), aSpv.size (), "main");
   TEST_ASSERT (pKernel != nullptr);

   if (pKernel)
   {
      pDevice->SetKernel (pKernel);
      pDevice->SetBuffer (pInput, 0, true);
      pDevice->SetBuffer (pOutput, 1, false);
      pDevice->SetPushConstants (&pc, sizeof (pc));
      pDevice->Dispatch ({ 1, 1, 1 });
      pDevice->Finish ();

      float aDistances[N_COUNT] = {};
      pOutput->GetData (aDistances, sizeof (aDistances));

      std::printf ("  Results: %.3f, %.3f, %.3f, %.3f\n",
                   aDistances[0], aDistances[1], aDistances[2], aDistances[3]);

      float dEpsilon = 0.001f;
      TEST_ASSERT (std::fabs (aDistances[0] - 3.0f) < dEpsilon);
      TEST_ASSERT (std::fabs (aDistances[1] - 4.0f) < dEpsilon);
      TEST_ASSERT (std::fabs (aDistances[2] - 5.0f) < dEpsilon);
      TEST_ASSERT (std::fabs (aDistances[3] - std::sqrt (3.0f)) < dEpsilon);

      pDevice->DestroyKernel (pKernel);
   }

   pDevice->DestroyBuffer (pInput);
   pDevice->DestroyBuffer (pOutput);
   delete pDevice;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main (int nArgc, char* aArgv[])
{
   std::printf ("=== VoxTest ===\n\n");

   TestCreateDevice ();
   TestCreateBuffer ();
   TestNullDevice ();

   const char* szSpvPath = (nArgc > 1) ? aArgv[1] : "test_proximity.spv";
   TestComputeDispatch (szSpvPath);

   std::printf ("\n%d / %d passed\n", nPassed, nTotal);

   return (nPassed == nTotal) ? 0 : 1;
}
