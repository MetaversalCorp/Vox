# Copyright 2026 Metaversal Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required (VERSION 3.20)
project (Vox LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Cross-compile: Android/iOS toolchains default CMAKE_FIND_ROOT_PATH_MODE_*
# to ONLY, which blocks find_package from honoring user-set ROOT vars (e.g.
# SPIRV_CROSS_ROOT pointing to our downloaded artifact) and find_library
# from searching outside the sysroot. Set BOTH so both resolve cleanly.
if (ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
   set (CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
   set (CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
   set (CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
endif ()

# ---------------------------------------------------------------------------
# Sources common to all platforms
# ---------------------------------------------------------------------------

set (VOX_SOURCES
   src/DeviceFactory.cpp
)

set (VOX_HEADERS
   include/vox/Vox.h
)

# ---------------------------------------------------------------------------
# Backend detection
# ---------------------------------------------------------------------------

# Vulkan
find_package (Vulkan QUIET)
if (Vulkan_FOUND)
   list (APPEND VOX_SOURCES src/VulkanDevice.cpp)
   list (APPEND VOX_DEFINES VOX_HAS_VULKAN)
   list (APPEND VOX_LIBS    Vulkan::Vulkan)
   message (STATUS "Vox: Vulkan backend enabled")
endif ()

# DX12 (Windows only)
if (WIN32)
   list (APPEND VOX_SOURCES src/DX12Device.cpp)
   list (APPEND VOX_DEFINES VOX_HAS_DX12)
   list (APPEND VOX_LIBS    d3d12 dxgi d3dcompiler)
   message (STATUS "Vox: DX12 backend enabled")
endif ()

# Metal (macOS only)
if (APPLE)
   enable_language (OBJCXX)
   list (APPEND VOX_SOURCES src/MetalDevice.mm)
   list (APPEND VOX_DEFINES VOX_HAS_METAL)
   find_library (METAL_FRAMEWORK Metal REQUIRED)
   find_library (FOUNDATION_FRAMEWORK Foundation REQUIRED)
   list (APPEND VOX_LIBS ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
   message (STATUS "Vox: Metal backend enabled")
endif ()

# SPIRV-Cross (required for DX12 and Metal shader translation)
if (SPIRV_CROSS_ROOT)
   list (APPEND VOX_INCLUDE_DIRS "${SPIRV_CROSS_ROOT}/include")

   if (WIN32)
      list (APPEND VOX_LIBS
         "${SPIRV_CROSS_ROOT}/lib/spirv-cross-hlsl.lib"
         "${SPIRV_CROSS_ROOT}/lib/spirv-cross-glsl.lib"
         "${SPIRV_CROSS_ROOT}/lib/spirv-cross-core.lib"
      )
      message (STATUS "Vox: SPIRV-Cross HLSL linked from ${SPIRV_CROSS_ROOT}")
   endif ()

   if (APPLE)
      list (APPEND VOX_LIBS
         "${SPIRV_CROSS_ROOT}/lib/libspirv-cross-msl.a"
         "${SPIRV_CROSS_ROOT}/lib/libspirv-cross-glsl.a"
         "${SPIRV_CROSS_ROOT}/lib/libspirv-cross-core.a"
      )
      message (STATUS "Vox: SPIRV-Cross MSL linked from ${SPIRV_CROSS_ROOT}")
   endif ()
else ()
   message (STATUS "Vox: SPIRV_CROSS_ROOT not set — DX12 and Metal backends will lack shader translation")
endif ()

# ---------------------------------------------------------------------------
# Library target
# ---------------------------------------------------------------------------

add_library (vox STATIC ${VOX_SOURCES} ${VOX_HEADERS})

target_include_directories (vox
   PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:include>
   PRIVATE
      ${CMAKE_CURRENT_SOURCE_DIR}/src
      ${VOX_INCLUDE_DIRS}
)

target_compile_definitions (vox PRIVATE ${VOX_DEFINES})
target_link_libraries (vox PRIVATE ${VOX_LIBS})

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------

include (GNUInstallDirs)

install (TARGETS vox
   EXPORT  vox-targets
   ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
   LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install (DIRECTORY include/vox DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install (EXPORT vox-targets
   FILE        vox-config.cmake
   NAMESPACE   vox::
   DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vox
)

# ---------------------------------------------------------------------------
# Test executable (optional, controlled by BUILD_TESTING)
# ---------------------------------------------------------------------------

option (BUILD_TESTING "Build Vox test executable" ON)
if (BUILD_TESTING)
   add_executable (VoxTest test/VoxTest.cpp)
   target_link_libraries (VoxTest PRIVATE vox)
endif ()
