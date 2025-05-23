/*
 *  Copyright 2019-2025 Diligent Graphics LLC
 *  Copyright 2015-2019 Egor Yusov
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  In no event and under no legal theory, whether in tort (including negligence),
 *  contract, or otherwise, unless required by applicable law (such as deliberate
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental,
 *  or consequential damages of any character arising as a result of this License or
 *  out of the use or inability to use the software (including but not limited to damages
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and
 *  all other commercial damages or losses), even if such Contributor has been advised
 *  of the possibility of such damages.
 */

#pragma once

/// \file
/// Definition of the Diligent::IRenderDeviceD3D12 interface

#include "../../GraphicsEngine/interface/RenderDevice.h"

DILIGENT_BEGIN_NAMESPACE(Diligent)

// {C7987C98-87FE-4309-AE88-E98F044B00F6}
static DILIGENT_CONSTEXPR INTERFACE_ID IID_RenderDeviceD3D12 =
    {0xc7987c98, 0x87fe, 0x4309, {0xae, 0x88, 0xe9, 0x8f, 0x4, 0x4b, 0x0, 0xf6}};

#define DILIGENT_INTERFACE_NAME IRenderDeviceD3D12
#include "../../../Primitives/interface/DefineInterfaceHelperMacros.h"

#define IRenderDeviceD3D12InclusiveMethods \
    IRenderDeviceInclusiveMethods;         \
    IRenderDeviceD3D12Methods RenderDeviceD3D12

// clang-format off

/// Exposes Direct3D12-specific functionality of a render device.
DILIGENT_BEGIN_INTERFACE(IRenderDeviceD3D12, IRenderDevice)
{
    /// Returns `ID3D12Device` interface of the internal Direct3D12 device object.

    /// The method does **NOT** increment the reference counter of the returned object,
    /// so Release() **must not** be called.
    VIRTUAL ID3D12Device* METHOD(GetD3D12Device)(THIS) CONST PURE;

    /// Creates a texture object from native d3d12 resource

    /// \param [in]  pd3d12Texture - pointer to the native D3D12 texture
    /// \param [in]  InitialState  - Initial texture state. See Diligent::RESOURCE_STATE.
    /// \param [out] ppTexture     - Address of the memory location where the pointer to the
    ///                              texture interface will be stored.
    ///                              The function calls AddRef(), so that the new object will contain
    ///                              one reference.
    VIRTUAL void METHOD(CreateTextureFromD3DResource)(THIS_
                                                      ID3D12Resource* pd3d12Texture,
                                                      RESOURCE_STATE  InitialState,
                                                      ITexture**      ppTexture) PURE;

    /// Creates a buffer object from native d3d12 resource

    /// \param [in]  pd3d12Buffer - Pointer to the native d3d12 buffer resource
    /// \param [in]  BuffDesc     - Buffer description. The system can recover buffer size, but
    ///                             the rest of the fields need to be populated by the client
    ///                             as they cannot be recovered from d3d12 resource description
    /// \param [in]  InitialState - Initial buffer state. See Diligent::RESOURCE_STATE.
    /// \param [out] ppBuffer     - Address of the memory location where the pointer to the
    ///                             buffer interface will be stored.
    ///                             The function calls AddRef(), so that the new object will contain
    ///                             one reference.
    VIRTUAL void METHOD(CreateBufferFromD3DResource)(THIS_
                                                     ID3D12Resource*      pd3d12Buffer,
                                                     const BufferDesc REF BuffDesc,
                                                     RESOURCE_STATE       InitialState,
                                                     IBuffer**            ppBuffer) PURE;

    /// Creates a bottom-level AS object from native d3d12 resource

    /// \param [in]  pd3d12BLAS   - Pointer to the native d3d12 acceleration structure resource
    /// \param [in]  Desc         - Bottom-level AS description.
    /// \param [in]  InitialState - Initial BLAS state. Can be Diligent::RESOURCE_STATE_UNKNOWN,
    ///                             Diligent::RESOURCE_STATE_BUILD_AS_READ, Diligent::RESOURCE_STATE_BUILD_AS_WRITE.
    ///                             See Diligent::RESOURCE_STATE.
    /// \param [out] ppBLAS       - Address of the memory location where the pointer to the
    ///                             bottom-level AS interface will be stored.
    ///                             The function calls AddRef(), so that the new object will contain
    ///                             one reference.
    VIRTUAL void METHOD(CreateBLASFromD3DResource)(THIS_
                                                   ID3D12Resource*             pd3d12BLAS,
                                                   const BottomLevelASDesc REF Desc,
                                                   RESOURCE_STATE              InitialState,
                                                   IBottomLevelAS**            ppBLAS) PURE;

    /// Creates a top-level AS object from native d3d12 resource

    /// \param [in]  pd3d12TLAS   - Pointer to the native d3d12 acceleration structure resource
    /// \param [in]  Desc         - Top-level AS description.
    /// \param [in]  InitialState - Initial TLAS state. Can be Diligent::RESOURCE_STATE_UNKNOWN,
    ///                             Diligent::RESOURCE_STATE_BUILD_AS_READ, Diligent::RESOURCE_STATE_BUILD_AS_WRITE,
    ///                             Diligent::RESOURCE_STATE_RAY_TRACING. See Diligent::RESOURCE_STATE.
    /// \param [out] ppTLAS       - Address of the memory location where the pointer to the
    ///                             top-level AS interface will be stored.
    ///                             The function calls AddRef(), so that the new object will contain
    ///                             one reference.
    VIRTUAL void METHOD(CreateTLASFromD3DResource)(THIS_
                                                   ID3D12Resource*          pd3d12TLAS,
                                                   const TopLevelASDesc REF Desc,
                                                   RESOURCE_STATE           InitialState,
                                                   ITopLevelAS**            ppTLAS) PURE;
};
DILIGENT_END_INTERFACE

#include "../../../Primitives/interface/UndefInterfaceHelperMacros.h"

#if DILIGENT_C_INTERFACE

// clang-format off

#    define IRenderDeviceD3D12_GetD3D12Device(This)                    CALL_IFACE_METHOD(RenderDeviceD3D12, GetD3D12Device,               This)
#    define IRenderDeviceD3D12_CreateTextureFromD3DResource(This, ...) CALL_IFACE_METHOD(RenderDeviceD3D12, CreateTextureFromD3DResource, This, __VA_ARGS__)
#    define IRenderDeviceD3D12_CreateBufferFromD3DResource(This, ...)  CALL_IFACE_METHOD(RenderDeviceD3D12, CreateBufferFromD3DResource,  This, __VA_ARGS__)
#    define IRenderDeviceD3D12_CreateBLASFromD3DResource(This, ...)    CALL_IFACE_METHOD(RenderDeviceD3D12, CreateBLASFromD3DResource,    This, __VA_ARGS__)
#    define IRenderDeviceD3D12_CreateTLASFromD3DResource(This, ...)    CALL_IFACE_METHOD(RenderDeviceD3D12, CreateTLASFromD3DResource,    This, __VA_ARGS__)

// clang-format on

#endif

DILIGENT_END_NAMESPACE // namespace Diligent
