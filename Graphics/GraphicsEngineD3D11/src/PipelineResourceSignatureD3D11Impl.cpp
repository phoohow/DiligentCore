/*
 *  Copyright 2019-2024 Diligent Graphics LLC
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

#include "pch.h"

#include "PipelineResourceSignatureD3D11Impl.hpp"

#include <algorithm>

#include "RenderDeviceD3D11Impl.hpp"
#include "ShaderVariableD3D.hpp"

namespace Diligent
{

namespace
{

void ValidatePipelineResourceSignatureDescD3D11(const PipelineResourceSignatureDesc& Desc) noexcept(false)
{
    for (Uint32 i = 0; i < Desc.NumResources; ++i)
    {
        const PipelineResourceDesc& ResDesc = Desc.Resources[i];
        const D3D11_RESOURCE_RANGE  Range   = PipelineResourceSignatureD3D11Impl::ShaderResourceTypeToRange(ResDesc.ResourceType);

        constexpr SHADER_TYPE UAVStages = SHADER_TYPE_PIXEL | SHADER_TYPE_COMPUTE;
        if (Range == D3D11_RESOURCE_RANGE_UAV && (ResDesc.ShaderStages & ~UAVStages) != 0)
        {
            LOG_ERROR_AND_THROW("Description of a pipeline resource signature '", (Desc.Name ? Desc.Name : ""), "' is invalid: ",
                                "Desc.Resources[", i, "].ShaderStages (", GetShaderStagesString(ResDesc.ShaderStages),
                                ") is not valid in Direct3D11 as UAVs are only supported in pixel and compute shader stages.");
        }
    }
}

} // namespace

PipelineResourceSignatureD3D11Impl::PipelineResourceSignatureD3D11Impl(IReferenceCounters*                  pRefCounters,
                                                                       RenderDeviceD3D11Impl*               pDeviceD3D11,
                                                                       const PipelineResourceSignatureDesc& Desc,
                                                                       SHADER_TYPE                          ShaderStages,
                                                                       bool                                 bIsDeviceInternal) :
    TPipelineResourceSignatureBase{pRefCounters, pDeviceD3D11, Desc, ShaderStages, bIsDeviceInternal}
{
    try
    {
        ValidatePipelineResourceSignatureDescD3D11(Desc);

        Initialize(
            GetRawAllocator(), DecoupleCombinedSamplers(Desc), /*CreateImmutableSamplers = */ true,
            [this]() //
            {
                CreateLayout(/*IsSerialized*/ false);
            },
            [this]() //
            {
                return ShaderResourceCacheD3D11::GetRequiredMemorySize(m_ResourceCounters);
            });
    }
    catch (...)
    {
        Destruct();
        throw;
    }
}

D3D11_RESOURCE_RANGE PipelineResourceSignatureD3D11Impl::ShaderResourceTypeToRange(SHADER_RESOURCE_TYPE Type)
{
    static_assert(SHADER_RESOURCE_TYPE_LAST == 8, "Please update the switch below to handle the new shader resource type");
    switch (Type)
    {
        // clang-format off
        case SHADER_RESOURCE_TYPE_CONSTANT_BUFFER:  return D3D11_RESOURCE_RANGE_CBV;
        case SHADER_RESOURCE_TYPE_TEXTURE_SRV:      return D3D11_RESOURCE_RANGE_SRV;
        case SHADER_RESOURCE_TYPE_BUFFER_SRV:       return D3D11_RESOURCE_RANGE_SRV;
        case SHADER_RESOURCE_TYPE_TEXTURE_UAV:      return D3D11_RESOURCE_RANGE_UAV;
        case SHADER_RESOURCE_TYPE_BUFFER_UAV:       return D3D11_RESOURCE_RANGE_UAV;
        case SHADER_RESOURCE_TYPE_SAMPLER:          return D3D11_RESOURCE_RANGE_SAMPLER;
        case SHADER_RESOURCE_TYPE_INPUT_ATTACHMENT: return D3D11_RESOURCE_RANGE_SRV;
            // clang-format on
        default:
            UNEXPECTED("Unsupported resource type");
            return D3D11_RESOURCE_RANGE_UNKNOWN;
    }
}

void PipelineResourceSignatureD3D11Impl::CreateLayout(const bool IsSerialized)
{
    const auto AllocBindPoints = [](D3D11ShaderResourceCounters& ResCounters,
                                    D3D11ResourceBindPoints&     BindPoints,
                                    SHADER_TYPE                  ShaderStages,
                                    Uint32                       ArraySize,
                                    D3D11_RESOURCE_RANGE         Range) //
    {
        while (ShaderStages != SHADER_TYPE_UNKNOWN)
        {
            const Int32 ShaderInd = ExtractFirstShaderStageIndex(ShaderStages);
            BindPoints[ShaderInd] = ResCounters[Range][ShaderInd];
            ResCounters[Range][ShaderInd] += ArraySize;
        }
    };

    // Index of the immutable sampler for every sampler in m_Desc.Resources, or InvalidImmutableSamplerIndex.
    std::vector<Uint32> ResourceToImmutableSamplerInd(m_Desc.NumResources, InvalidImmutableSamplerIndex);
    for (Uint32 i = 0; i < m_Desc.NumResources; ++i)
    {
        const PipelineResourceDesc& ResDesc = m_Desc.Resources[i];
        if (ResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER)
        {
            // We only need to search for immutable samplers for SHADER_RESOURCE_TYPE_SAMPLER.
            // For SHADER_RESOURCE_TYPE_TEXTURE_SRV, we will look for the assigned sampler and check if it is immutable.

            // If there is an immutable sampler that is not defined as resource, e.g.:
            //
            //      PipelineResourceDesc Resources[] = {SHADER_TYPE_PIXEL, "g_Texture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, ...}
            //      ImmutableSamplerDesc ImtblSams[] = {SHADER_TYPE_PIXEL, "g_Texture", ...}
            //
            // the sampler will not be assigned to the texture. It will be initialized directly in the SRB resource cache,
            // will be added to bindings map by UpdateShaderResourceBindingMap and then properly mapped to the shader sampler register.

            // Note that FindImmutableSampler() below will work properly both when combined texture samplers are used and when not.
            const Uint32 SrcImmutableSamplerInd = FindImmutableSampler(ResDesc.ShaderStages, ResDesc.Name);
            if (SrcImmutableSamplerInd != InvalidImmutableSamplerIndex)
            {
                ResourceToImmutableSamplerInd[i] = SrcImmutableSamplerInd;
                // Set the immutable sampler array size to match the resource array size
                ImmutableSamplerAttribsD3D11& DstImtblSampAttribs = m_pImmutableSamplerAttribs[SrcImmutableSamplerInd];
                // One immutable sampler may be used by different arrays in different shader stages - use the maximum array size
                DstImtblSampAttribs.ArraySize = std::max(DstImtblSampAttribs.ArraySize, ResDesc.ArraySize);
            }
        }
    }

    // Allocate registers for immutable samplers first
    for (Uint32 i = 0; i < m_Desc.NumImmutableSamplers; ++i)
    {
        const ImmutableSamplerDesc&   ImtblSamp        = GetImmutableSamplerDesc(i);
        ImmutableSamplerAttribsD3D11& ImtblSampAttribs = m_pImmutableSamplerAttribs[i];
        D3D11ResourceBindPoints       BindPoints;
        AllocBindPoints(m_ResourceCounters, BindPoints, ImtblSamp.ShaderStages, ImtblSampAttribs.ArraySize, D3D11_RESOURCE_RANGE_SAMPLER);
        if (!IsSerialized)
        {
            ImtblSampAttribs.BindPoints = BindPoints;
        }
        else
        {
            DEV_CHECK_ERR(ImtblSampAttribs.BindPoints == BindPoints, "Deserialized immutable sampler bind points are invalid");
        }
    }

    D3D11ShaderResourceCounters StaticResCounters;

    for (Uint32 i = 0; i < m_Desc.NumResources; ++i)
    {
        const PipelineResourceDesc& ResDesc = GetResourceDesc(i);
        VERIFY(i == 0 || ResDesc.VarType >= m_Desc.Resources[i - 1].VarType, "Resources must be sorted by variable type");

        Uint32 AssignedSamplerInd     = ResourceAttribs::InvalidSamplerInd;
        Uint32 SrcImmutableSamplerInd = ResourceToImmutableSamplerInd[i];
        if (ResDesc.ResourceType == SHADER_RESOURCE_TYPE_TEXTURE_SRV)
        {
            VERIFY_EXPR(SrcImmutableSamplerInd == InvalidImmutableSamplerIndex);
            AssignedSamplerInd = FindAssignedSampler(ResDesc, ResourceAttribs::InvalidSamplerInd);
            if (AssignedSamplerInd != ResourceAttribs::InvalidSamplerInd)
            {
                SrcImmutableSamplerInd = ResourceToImmutableSamplerInd[AssignedSamplerInd];
            }
        }

        D3D11ResourceBindPoints BindPoints;

        // Do not allocate resource slot for samplers that are also defined as immutable samplers
        if (!(ResDesc.ResourceType == SHADER_RESOURCE_TYPE_SAMPLER && SrcImmutableSamplerInd != InvalidImmutableSamplerIndex))
        {
            const D3D11_RESOURCE_RANGE Range = ShaderResourceTypeToRange(ResDesc.ResourceType);

            AllocBindPoints(m_ResourceCounters, BindPoints, ResDesc.ShaderStages, ResDesc.ArraySize, Range);
            if (ResDesc.VarType == SHADER_RESOURCE_VARIABLE_TYPE_STATIC)
            {
                // Since resources in the static cache are indexed by the same bindings, we need to
                // make sure that there is enough space in the cache.
                const D3D11ResourceRangeCounters& SrcRangeCounters = m_ResourceCounters[Range];
                D3D11ResourceRangeCounters&       DstRangeCounters = StaticResCounters[Range];
                for (SHADER_TYPE ShaderStages = ResDesc.ShaderStages; ShaderStages != SHADER_TYPE_UNKNOWN;)
                {
                    const Int32 ShaderInd       = ExtractFirstShaderStageIndex(ShaderStages);
                    DstRangeCounters[ShaderInd] = std::max(Uint8{DstRangeCounters[ShaderInd]}, SrcRangeCounters[ShaderInd]);
                }
            }

            if (Range == D3D11_RESOURCE_RANGE_CBV && (ResDesc.Flags & PIPELINE_RESOURCE_FLAG_NO_DYNAMIC_BUFFERS) == 0)
            {
                // Set corresponding bits in m_DynamicCBSlotsMask
                for (SHADER_TYPE ShaderStages = ResDesc.ShaderStages; ShaderStages != SHADER_TYPE_UNKNOWN;)
                {
                    const Int32  ShaderInd = ExtractFirstShaderStageIndex(ShaderStages);
                    const Uint16 BindPoint = Uint16{BindPoints[ShaderInd]};
                    for (Uint32 elem = 0; elem < ResDesc.ArraySize; ++elem)
                    {
                        VERIFY_EXPR(BindPoint + elem < Uint32{sizeof(m_DynamicCBSlotsMask[0]) * 8});
                        m_DynamicCBSlotsMask[ShaderInd] |= 1u << (BindPoint + elem);
                    }
                }
            }
        }
        else
        {
            VERIFY(AssignedSamplerInd == ResourceAttribs::InvalidSamplerInd, "Sampler can't be assigned to another sampler.");
            // Use bind points from the immutable sampler.
            BindPoints = m_pImmutableSamplerAttribs[SrcImmutableSamplerInd].BindPoints;
            VERIFY_EXPR(!BindPoints.IsEmpty());
        }

        ResourceAttribs* const pAttrib = m_pResourceAttribs + i;
        if (!IsSerialized)
        {
            new (pAttrib) ResourceAttribs //
                {
                    BindPoints,
                    AssignedSamplerInd,
                    SrcImmutableSamplerInd != InvalidImmutableSamplerIndex // For samplers or Tex SRVs combined with samplers
                };
        }
        else
        {
            DEV_CHECK_ERR(pAttrib->BindPoints == BindPoints, "Deserialized bind points are invalid");
            DEV_CHECK_ERR(pAttrib->SamplerInd == AssignedSamplerInd, "Deserialized sampler index is invalid");
            DEV_CHECK_ERR(pAttrib->IsImmutableSamplerAssigned() == (SrcImmutableSamplerInd != InvalidImmutableSamplerIndex),
                          "Deserialized immutable sampler flag is invalid");
        }
    }

    if (m_pStaticResCache)
    {
        m_pStaticResCache->Initialize(StaticResCounters, GetRawAllocator(), nullptr);
        VERIFY_EXPR(m_pStaticResCache->IsInitialized());
    }
}

PipelineResourceSignatureD3D11Impl::~PipelineResourceSignatureD3D11Impl()
{
    Destruct();
}

void PipelineResourceSignatureD3D11Impl::CopyStaticResources(ShaderResourceCacheD3D11& DstResourceCache) const
{
    if (m_pStaticResCache == nullptr)
        return;

    // SrcResourceCache contains only static resources.
    // In case of SRB, DstResourceCache contains static, mutable and dynamic resources.
    // In case of Signature, DstResourceCache contains only static resources.
    const ShaderResourceCacheD3D11& SrcResourceCache = *m_pStaticResCache;
    VERIFY_EXPR(SrcResourceCache.GetContentType() == ResourceCacheContentType::Signature);
    const ResourceCacheContentType DstCacheType = DstResourceCache.GetContentType();

    const auto ResIdxRange = GetResourceIndexRange(SHADER_RESOURCE_VARIABLE_TYPE_STATIC);
    for (Uint32 r = ResIdxRange.first; r < ResIdxRange.second; ++r)
    {
        const PipelineResourceDesc& ResDesc = GetResourceDesc(r);
        const ResourceAttribs&      ResAttr = GetResourceAttribs(r);
        VERIFY_EXPR(ResDesc.VarType == SHADER_RESOURCE_VARIABLE_TYPE_STATIC);

        static_assert(D3D11_RESOURCE_RANGE_COUNT == 4, "Please update the switch below to handle the new descriptor range");
        switch (ShaderResourceTypeToRange(ResDesc.ResourceType))
        {
            case D3D11_RESOURCE_RANGE_CBV:
                for (Uint32 ArrInd = 0; ArrInd < ResDesc.ArraySize; ++ArrInd)
                {
                    if (!DstResourceCache.CopyResource<D3D11_RESOURCE_RANGE_CBV>(SrcResourceCache, ResAttr.BindPoints + ArrInd))
                    {
                        if (DstCacheType == ResourceCacheContentType::SRB)
                            LOG_ERROR_MESSAGE("No resource is assigned to static shader variable '", GetShaderResourcePrintName(ResDesc, ArrInd), "' in pipeline resource signature '", m_Desc.Name, "'.");
                    }
                }
                break;
            case D3D11_RESOURCE_RANGE_SRV:
                for (Uint32 ArrInd = 0; ArrInd < ResDesc.ArraySize; ++ArrInd)
                {
                    if (!DstResourceCache.CopyResource<D3D11_RESOURCE_RANGE_SRV>(SrcResourceCache, ResAttr.BindPoints + ArrInd))
                    {
                        if (DstCacheType == ResourceCacheContentType::SRB)
                            LOG_ERROR_MESSAGE("No resource is assigned to static shader variable '", GetShaderResourcePrintName(ResDesc, ArrInd), "' in pipeline resource signature '", m_Desc.Name, "'.");
                    }
                }
                break;
            case D3D11_RESOURCE_RANGE_SAMPLER:
                if (!ResAttr.IsImmutableSamplerAssigned())
                {
                    for (Uint32 ArrInd = 0; ArrInd < ResDesc.ArraySize; ++ArrInd)
                    {
                        if (!DstResourceCache.CopyResource<D3D11_RESOURCE_RANGE_SAMPLER>(SrcResourceCache, ResAttr.BindPoints + ArrInd))
                        {
                            if (DstCacheType == ResourceCacheContentType::SRB)
                                LOG_ERROR_MESSAGE("No resource is assigned to static shader variable '", GetShaderResourcePrintName(ResDesc, ArrInd), "' in pipeline resource signature '", m_Desc.Name, "'.");
                        }
                    }
                }
#ifdef DILIGENT_DEBUG
                else if (DstCacheType == ResourceCacheContentType::SRB)
                {
                    for (Uint32 ArrInd = 0; ArrInd < ResDesc.ArraySize; ++ArrInd)
                    {
                        VERIFY(DstResourceCache.IsResourceBound<D3D11_RESOURCE_RANGE_SAMPLER>(ResAttr.BindPoints + ArrInd),
                               "Immutable samplers must have been initialized by InitSRBResourceCache(). Null sampler is a bug.");
                    }
                }
#endif
                break;
            case D3D11_RESOURCE_RANGE_UAV:
                for (Uint32 ArrInd = 0; ArrInd < ResDesc.ArraySize; ++ArrInd)
                {
                    if (!DstResourceCache.CopyResource<D3D11_RESOURCE_RANGE_UAV>(SrcResourceCache, ResAttr.BindPoints + ArrInd))
                    {
                        if (DstCacheType == ResourceCacheContentType::SRB)
                            LOG_ERROR_MESSAGE("No resource is assigned to static shader variable '", GetShaderResourcePrintName(ResDesc, ArrInd), "' in pipeline resource signature '", m_Desc.Name, "'.");
                    }
                }
                break;
            default:
                UNEXPECTED("Unsupported descriptor range type.");
        }
    }

#ifdef DILIGENT_DEBUG
    DstResourceCache.DbgVerifyDynamicBufferMasks();
#endif
}

void PipelineResourceSignatureD3D11Impl::InitSRBResourceCache(ShaderResourceCacheD3D11& ResourceCache)
{
    ResourceCache.Initialize(m_ResourceCounters, m_SRBMemAllocator.GetResourceCacheDataAllocator(0), &m_DynamicCBSlotsMask);
    VERIFY_EXPR(ResourceCache.IsInitialized());

    // Copy immutable samplers.
    for (Uint32 i = 0; i < m_Desc.NumImmutableSamplers; ++i)
    {
        const ImmutableSamplerAttribsD3D11& ImtblSampAttr = GetImmutableSamplerAttribs(i);
        VERIFY_EXPR(ImtblSampAttr.IsAllocated());
        VERIFY_EXPR(m_pImmutableSamplers != nullptr && m_pImmutableSamplers[i]);
        VERIFY_EXPR(ImtblSampAttr.ArraySize > 0);

        SamplerD3D11Impl* pSampler = m_pImmutableSamplers[i];
        for (Uint32 ArrInd = 0; ArrInd < ImtblSampAttr.ArraySize; ++ArrInd)
            ResourceCache.SetResource<D3D11_RESOURCE_RANGE_SAMPLER>(ImtblSampAttr.BindPoints + ArrInd, pSampler);
    }
}

void PipelineResourceSignatureD3D11Impl::UpdateShaderResourceBindingMap(ResourceBinding::TMap&             ResourceMap,
                                                                        SHADER_TYPE                        ShaderStage,
                                                                        const D3D11ShaderResourceCounters& BaseBindings) const
{
    VERIFY(ShaderStage != SHADER_TYPE_UNKNOWN && IsPowerOfTwo(ShaderStage), "Only single shader stage must be provided.");
    const Int32 ShaderInd = GetShaderTypeIndex(ShaderStage);

    for (Uint32 r = 0, ResCount = GetTotalResourceCount(); r < ResCount; ++r)
    {
        const PipelineResourceDesc&         ResDesc = GetResourceDesc(r);
        const PipelineResourceAttribsD3D11& ResAttr = GetResourceAttribs(r);
        const D3D11_RESOURCE_RANGE          Range   = ShaderResourceTypeToRange(ResDesc.ResourceType);

        if ((ResDesc.ShaderStages & ShaderStage) != 0)
        {
            VERIFY_EXPR(ResAttr.BindPoints.IsStageActive(ShaderInd));
            ResourceBinding::BindInfo BindInfo //
                {
                    Uint32{BaseBindings[Range][ShaderInd]} + Uint32{ResAttr.BindPoints[ShaderInd]},
                    0u, // register space is not supported
                    ResDesc.ArraySize,
                    ResDesc.ResourceType //
                };
            bool IsUnique = ResourceMap.emplace(HashMapStringKey{ResDesc.Name}, BindInfo).second;
            VERIFY(IsUnique, "Shader resource '", ResDesc.Name,
                   "' already present in the binding map. Every shader resource in PSO must be unambiguously defined by "
                   "only one resource signature. This error should've been caught by ValidatePipelineResourceSignatures().");
        }
    }

    // Add immutable samplers to the map as there may be immutable samplers that are not defined as resources, e.g.:
    //
    //      PipelineResourceDesc Resources[] = {SHADER_TYPE_PIXEL, "g_Texture", 1, SHADER_RESOURCE_TYPE_TEXTURE_SRV, ...}
    //      ImmutableSamplerDesc ImtblSams[] = {SHADER_TYPE_PIXEL, "g_Texture", ...}
    //
    for (Uint32 samp = 0, SampCount = GetImmutableSamplerCount(); samp < SampCount; ++samp)
    {
        const ImmutableSamplerDesc&         ImtblSam = GetImmutableSamplerDesc(samp);
        const ImmutableSamplerAttribsD3D11& SampAttr = GetImmutableSamplerAttribs(samp);
        const D3D11_RESOURCE_RANGE          Range    = D3D11_RESOURCE_RANGE_SAMPLER;

        VERIFY_EXPR(SampAttr.IsAllocated());
        if ((ImtblSam.ShaderStages & ShaderStage) != 0)
        {
            VERIFY_EXPR(SampAttr.BindPoints.IsStageActive(ShaderInd));

            String SampName{ImtblSam.SamplerOrTextureName};
            if (IsUsingCombinedSamplers())
                SampName += GetCombinedSamplerSuffix();

            ResourceBinding::BindInfo BindInfo //
                {
                    Uint32{BaseBindings[Range][ShaderInd]} + Uint32{SampAttr.BindPoints[ShaderInd]},
                    0u, // register space is not supported
                    SampAttr.ArraySize,
                    SHADER_RESOURCE_TYPE_SAMPLER //
                };

            auto it_inserted = ResourceMap.emplace(HashMapStringKey{SampName}, BindInfo);
#ifdef DILIGENT_DEBUG
            if (!it_inserted.second)
            {
                const ResourceBinding::BindInfo& ExistingBindInfo = it_inserted.first->second;
                VERIFY(ExistingBindInfo.BindPoint == BindInfo.BindPoint,
                       "Bind point defined by the immutable sampler attribs is inconsistent with the bind point defined by the sampler resource. "
                       "This may be a bug in CreateLayout().");
                VERIFY(ExistingBindInfo.ArraySize >= BindInfo.ArraySize,
                       "Array size defined by the immutable sampler attribs is smaller than the size defined by the sampler resource. "
                       "This may be a bug in CreateLayout().");
            }
#endif
        }
    }
}

#ifdef DILIGENT_DEVELOPMENT
bool PipelineResourceSignatureD3D11Impl::DvpValidateCommittedResource(const D3DShaderResourceAttribs& D3DAttribs,
                                                                      Uint32                          ResIndex,
                                                                      const ShaderResourceCacheD3D11& ResourceCache,
                                                                      const char*                     ShaderName,
                                                                      const char*                     PSOName) const
{
    VERIFY_EXPR(ResIndex < m_Desc.NumResources);
    const PipelineResourceDesc&         ResDesc = m_Desc.Resources[ResIndex];
    const PipelineResourceAttribsD3D11& ResAttr = m_pResourceAttribs[ResIndex];
    VERIFY(strcmp(ResDesc.Name, D3DAttribs.Name) == 0, "Inconsistent resource names");

    VERIFY_EXPR(D3DAttribs.BindCount <= ResDesc.ArraySize);

    bool BindingsOK = true;
    switch (ShaderResourceTypeToRange(ResDesc.ResourceType))
    {
        case D3D11_RESOURCE_RANGE_CBV:
            for (Uint32 ArrInd = 0; ArrInd < D3DAttribs.BindCount; ++ArrInd)
            {
                if (!ResourceCache.IsResourceBound<D3D11_RESOURCE_RANGE_CBV>(ResAttr.BindPoints + ArrInd))
                {
                    LOG_ERROR_MESSAGE("No resource is bound to variable '", GetShaderResourcePrintName(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd),
                                      "' in shader '", ShaderName, "' of PSO '", PSOName, "'");
                    BindingsOK = false;
                }
            }
            break;

        case D3D11_RESOURCE_RANGE_SAMPLER:
            for (Uint32 ArrInd = 0; ArrInd < D3DAttribs.BindCount; ++ArrInd)
            {
                if (!ResourceCache.IsResourceBound<D3D11_RESOURCE_RANGE_SAMPLER>(ResAttr.BindPoints + ArrInd))
                {
                    LOG_ERROR_MESSAGE("No resource is bound to variable '", GetShaderResourcePrintName(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd),
                                      "' in shader '", ShaderName, "' of PSO '", PSOName, "'");
                    BindingsOK = false;
                }
            }
            break;

        case D3D11_RESOURCE_RANGE_SRV:
            for (Uint32 ArrInd = 0; ArrInd < D3DAttribs.BindCount; ++ArrInd)
            {
                if (!ResourceCache.IsResourceBound<D3D11_RESOURCE_RANGE_SRV>(ResAttr.BindPoints + ArrInd))
                {
                    LOG_ERROR_MESSAGE("No resource is bound to variable '", GetShaderResourcePrintName(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd),
                                      "' in shader '", ShaderName, "' of PSO '", PSOName, "'");
                    BindingsOK = false;
                    continue;
                }

                const ShaderResourceCacheD3D11::CachedResource& SRV = ResourceCache.GetResource<D3D11_RESOURCE_RANGE_SRV>(ResAttr.BindPoints + ArrInd);
                if (SRV.pTexture)
                {
                    if (!ValidateResourceViewDimension(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd, SRV.pView.RawPtr<TextureViewD3D11Impl>(),
                                                       D3DAttribs.GetResourceDimension(), D3DAttribs.IsMultisample()))
                        BindingsOK = false;
                }
                else
                {
                    VERIFY_EXPR(SRV.pBuffer != nullptr);
                    if (!VerifyBufferViewModeD3D(SRV.pView.RawPtr<BufferViewD3D11Impl>(), D3DAttribs, ShaderName))
                        BindingsOK = false;
                }
            }
            break;

        case D3D11_RESOURCE_RANGE_UAV:
            for (Uint32 ArrInd = 0; ArrInd < D3DAttribs.BindCount; ++ArrInd)
            {
                if (!ResourceCache.IsResourceBound<D3D11_RESOURCE_RANGE_UAV>(ResAttr.BindPoints + ArrInd))
                {
                    LOG_ERROR_MESSAGE("No resource is bound to variable '", GetShaderResourcePrintName(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd),
                                      "' in shader '", ShaderName, "' of PSO '", PSOName, "'");
                    BindingsOK = false;
                    continue;
                }
                const ShaderResourceCacheD3D11::CachedResource& UAV = ResourceCache.GetResource<D3D11_RESOURCE_RANGE_UAV>(ResAttr.BindPoints + ArrInd);
                if (UAV.pTexture)
                {
                    if (!ValidateResourceViewDimension(D3DAttribs.Name, D3DAttribs.BindCount, ArrInd, UAV.pView.RawPtr<TextureViewD3D11Impl>(),
                                                       D3DAttribs.GetResourceDimension(), D3DAttribs.IsMultisample()))
                        BindingsOK = false;
                }
                else
                {
                    VERIFY_EXPR(UAV.pBuffer != nullptr);
                    if (!VerifyBufferViewModeD3D(UAV.pView.RawPtr<BufferViewD3D11Impl>(), D3DAttribs, ShaderName))
                        BindingsOK = false;
                }
            }
            break;

        default:
            UNEXPECTED("Unsupported descriptor range type.");
    }

    return BindingsOK;
}
#endif // DILIGENT_DEVELOPMENT


PipelineResourceSignatureD3D11Impl::PipelineResourceSignatureD3D11Impl(IReferenceCounters*                               pRefCounters,
                                                                       RenderDeviceD3D11Impl*                            pDevice,
                                                                       const PipelineResourceSignatureDesc&              Desc,
                                                                       const PipelineResourceSignatureInternalDataD3D11& InternalData) :
    TPipelineResourceSignatureBase{pRefCounters, pDevice, Desc, InternalData}
{
    try
    {
        ValidatePipelineResourceSignatureDescD3D11(Desc);

        Deserialize(
            GetRawAllocator(), DecoupleCombinedSamplers(Desc), InternalData, /*CreateImmutableSamplers = */ true,
            [this]() //
            {
                CreateLayout(/*IsSerialized*/ true);
            },
            [this]() //
            {
                return ShaderResourceCacheD3D11::GetRequiredMemorySize(m_ResourceCounters);
            });
    }
    catch (...)
    {
        Destruct();
        throw;
    }
}

} // namespace Diligent
