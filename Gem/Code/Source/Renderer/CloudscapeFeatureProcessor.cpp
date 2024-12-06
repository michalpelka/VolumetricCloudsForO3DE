/*
 * Copyright (c) Galib Arrieta (aka lumbermixalot@github, aka galibzon@github).
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <AzCore/Name/Name.h>
#include <AzCore/Name/NameDictionary.h>

#include <Atom/RHI.Reflect/InputStreamLayoutBuilder.h>
#include <Atom/RHI/DrawPacketBuilder.h>

#include <Atom/RPI.Public/Image/AttachmentImagePool.h>
#include <Atom/RPI.Public/RenderPipeline.h>

#include <Atom/RPI.Public/RPIUtils.h>
// #include <Atom/RPI.Reflect/Asset/AssetUtils.h> // FIXME: Try removing

#include <Atom/RPI.Public/Image/ImageSystemInterface.h>
#include <Atom/RPI.Public/Pass/PassFilter.h>
#include <Atom/RPI.Public/Pass/RasterPass.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/Shader/Shader.h>
#include <Atom/RPI.Public/View.h>
#include <Atom/RPI.Public/ViewportContext.h>

#include <Renderer/Passes/CloudscapeComputePass.h>
#include <Renderer/Passes/CloudscapeRenderPass.h>
#include <sys/types.h>
// #include <Renderer/Passes/DepthBufferCopyPass.h>
#include "Atom/RPI.Public/Pass/PassAttachment.h"
#include "CloudscapeFeatureProcessor.h"

namespace VolumetricClouds
{
    void CloudscapeFeatureProcessor::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serializeContext = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serializeContext->Class<CloudscapeFeatureProcessor, AZ::RPI::FeatureProcessor>()->Version(1);
        }
    }

    /////////////////////////////////////////////////////////////////////////////
    //! AZ::RPI::FeatureProcessor overrides START ...
    void CloudscapeFeatureProcessor::Activate()
    {
        ActivateInternal();
    }

    void CloudscapeFeatureProcessor::Deactivate()
    {
        DisableSceneNotification();
        m_viewportSize = { 0, 0 };
        m_viewToIndexMap.clear();
        m_renderPipelineToIndexMap.clear();
        m_cloudscapeComputePasses.clear();
        m_cloudscapeReprojectionPasses.clear();
        m_cloudscapeRenderPasses.clear();
    }

    void CloudscapeFeatureProcessor::Render(const RenderPacket& renderPacket)
    {
        // Update the frame counter for the compute and render passes if required.
        for (const auto& renderPipeline : m_renderPipelineToIndexMap)
        {
            const auto& [pipeline, index] = renderPipeline;
            if (pipeline)
            {
                if (pipeline->NeedsRender())
                {
                    if (m_cloudscapeComputePasses[index])
                    {
                        uint32_t pixelIndex = m_cloudscapeComputePasses[index]->GetPixelIndex();
                        pixelIndex++;
                        m_cloudscapeComputePasses[index]->UpdateFrameCounter(pixelIndex);

                        const auto& passSrg = m_cloudscapeReprojectionPasses[index]->GetShaderResourceGroup();
                        const uint32_t pixelIndex4x4 = pixelIndex % 16;
                        passSrg->SetConstant(m_pixelIndex4x4Index, pixelIndex4x4);

                        m_cloudscapeRenderPasses[index]->UpdateFrameCounter(pixelIndex);
                    }
                }
            }
        }
    }

    void CloudscapeFeatureProcessor::AddRenderPasses([[maybe_unused]] AZ::RPI::RenderPipeline* renderPipeline)
    {
        // Extract the image size from the first attachment of the render pipeline.
        auto renderRootPass = renderPipeline->GetRootPass();
        if (!renderRootPass)
        {
            return;
        }
        AZ::RPI::PassAttachmentBinding rootBinding;
        if (renderRootPass->GetInputOutputCount() != 0)
        {
            rootBinding = renderRootPass->GetInputOutputBinding(0);
        }
        else if (renderRootPass->GetOutputCount() != 0)
        {
            rootBinding = renderRootPass->GetOutputBinding(0);
        }
        else
        {
            return;
        }

        auto rootAttachment = rootBinding.GetAttachment();
        if (!rootAttachment)
        {
            return;
        }

        if (rootAttachment->GetAttachmentType() != AZ::RHI::AttachmentType::Image)
        {
            return;
        }
        auto attachmentImageDesc = rootAttachment->GetTransientImageDescriptor();
        [[maybe_unused]] auto imageSize = attachmentImageDesc.m_imageDescriptor.m_size;

        uint32_t width = imageSize.m_width;
        uint32_t height = imageSize.m_height;

        // Add render pipeline to the map to check later if the frame counter needs to be updated.
        m_renderPipelineToIndexMap[renderPipeline] = m_cloudscapeComputePasses.size();

        // Create a pair of output textures used for the modified pipeline..
        m_cloudOutputPairs.push_back({ CreateCloudscapeOutputAttachment(AZ::Name("CloudscapeOutput0"), { width, height }),
                                       CreateCloudscapeOutputAttachment(AZ::Name("CloudscapeOutput1"), { width, height }) });

        // Create a index value for the passes.
        const uint32_t passIndex = m_cloudscapeComputePasses.size();

        // Get the pass requests to create passes from the asset
        AddPassRequestToRenderPipeline(renderPipeline, "Passes/CloudscapeComputePassRequest.azasset", "DepthPrePass", false /*before*/);
        // Hold a reference to the compute pass
        {
            const auto passName = AZ::Name("CloudscapeComputePass");
            AZ::RPI::PassFilter passFilter = AZ::RPI::PassFilter::CreateWithPassName(passName, renderPipeline);
            AZ::RPI::Pass* existingPass = AZ::RPI::PassSystemInterface::Get()->FindFirstPass(passFilter);
            m_cloudscapeComputePasses.push_back(azrtti_cast<CloudscapeComputePass*>(existingPass));

            if (!m_cloudscapeComputePasses.back())
            {
                AZ_Error(LogName, false, "%s Failed to find as RenderPass: %s", __FUNCTION__, passName.GetCStr());
                return;
            }

            m_cloudscapeComputePasses.back()->SetPassIndex(passIndex);

            if (m_shaderConstantData)
            {
                m_cloudscapeComputePasses.back()->UpdateShaderConstantData(*m_shaderConstantData);
            }
        }

        AddPassRequestToRenderPipeline(
            renderPipeline, "Passes/CloudscapeReprojectionComputePassRequest.azasset", "MotionVectorPass", false /*before*/);
        // Hold a reference to the compute pass
        {
            const auto passName = AZ::Name("CloudscapeReprojectionComputePass");
            AZ::RPI::PassFilter passFilter = AZ::RPI::PassFilter::CreateWithPassName(passName, renderPipeline);
            AZ::RPI::Pass* existingPass = AZ::RPI::PassSystemInterface::Get()->FindFirstPass(passFilter);
            m_cloudscapeReprojectionPasses.push_back(azrtti_cast<AZ::RPI::ComputePass*>(existingPass));
            if (!m_cloudscapeReprojectionPasses.back())
            {
                AZ_Error(LogName, false, "%s Failed to find as RenderPass: %s", __FUNCTION__, passName.GetCStr());
                return;
            }
            m_cloudscapeReprojectionPasses.back()->SetTargetThreadCounts(width, height, 1);
        }

        AddPassRequestToRenderPipeline(renderPipeline, "Passes/CloudscapeRenderPassRequest.azasset", "TransparentPass", true /*before*/);
        // Hold a reference to the render pass
        {
            const auto passName = AZ::Name("CloudscapeRenderPass");
            AZ::RPI::PassFilter passFilter = AZ::RPI::PassFilter::CreateWithPassName(passName, renderPipeline);
            AZ::RPI::Pass* existingPass = AZ::RPI::PassSystemInterface::Get()->FindFirstPass(passFilter);
            m_cloudscapeRenderPasses.push_back(azrtti_cast<CloudscapeRenderPass*>(existingPass));
            if (!m_cloudscapeRenderPasses.back())
            {
                AZ_Error(LogName, false, "%s Failed to find as RenderPass: %s", __FUNCTION__, passName.GetCStr());
                return;
            }
        }
    }
    //! AZ::RPI::FeatureProcessor overrides END ...
    /////////////////////////////////////////////////////////////////////////////

    /////////////////////////////////////////////////////////////////////
    //! Functions called by CloudscapeComponentController START
    void CloudscapeFeatureProcessor::UpdateShaderConstantData(const CloudscapeShaderConstantData& shaderData)
    {
        m_shaderConstantData = &shaderData;
        for (unsigned int i = 0; i < m_cloudscapeComputePasses.size(); i++)
        {
            if (m_cloudscapeComputePasses[i])
            {
                m_cloudscapeComputePasses[i]->UpdateShaderConstantData(shaderData);
            }
        }
    }

    //! Functions called by CloudscapeComponentController END
    /////////////////////////////////////////////////////////////////////

    void CloudscapeFeatureProcessor::ActivateInternal()
    {
        auto viewportContextInterface = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        auto viewportContext = viewportContextInterface->GetViewportContextByScene(GetParentScene());
        m_viewportSize = viewportContext->GetViewportSize();

        DisableSceneNotification();
        EnableSceneNotification();
    }

    AZ::Data::Instance<AZ::RPI::AttachmentImage> CloudscapeFeatureProcessor::CreateCloudscapeOutputAttachment(
        const AZ::Name& attachmentName, const AzFramework::WindowSize attachmentSize) const
    {
        AZ::RHI::ImageDescriptor imageDesc = AZ::RHI::ImageDescriptor::Create2D(
            AZ::RHI::ImageBindFlags::ShaderReadWrite, attachmentSize.m_width, attachmentSize.m_height, AZ::RHI::Format::R8G8B8A8_UNORM);
        AZ::RHI::ClearValue clearValue = AZ::RHI::ClearValue::CreateVector4Float(0, 0, 0, 0);
        AZ::Data::Instance<AZ::RPI::AttachmentImagePool> pool = AZ::RPI::ImageSystemInterface::Get()->GetSystemAttachmentPool();
        return AZ::RPI::AttachmentImage::Create(*pool.get(), imageDesc, attachmentName, &clearValue, nullptr);
    }

} // namespace VolumetricClouds
