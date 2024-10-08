/*
 * Copyright (c) Galib Arrieta (aka lumbermixalot@github, aka galibzon@github).
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <AzCore/Name/Name.h>
#include <AzCore/std/containers/vector.h>

#include <Atom/RPI.Public/Base.h>
#include <Atom/RPI.Public/Buffer/Buffer.h>
#include <Atom/RPI.Public/FeatureProcessor.h>
#include <Atom/RPI.Public/Pass/ComputePass.h>
#include <Atom/RPI.Public/PipelineState.h>
#include <Atom/RPI.Public/Shader/ShaderResourceGroup.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <Atom/RPI.Reflect/Image/StreamingImageAsset.h>

#include <Renderer/CloudTexturePresentationData.h>
#include <Renderer/CloudscapeShaderConstantData.h>
#include <Renderer/Passes/CloudTextureComputeData.h>

namespace AZ::RPI
{
    class Scene;
}

namespace VolumetricClouds
{
    class CloudscapeComputePass;
    class CloudscapeRenderPass;

    class CloudscapeFeatureProcessor final : public AZ::RPI::FeatureProcessor
    {
    public:
        AZ_CLASS_ALLOCATOR(CloudscapeFeatureProcessor, AZ::SystemAllocator);
        AZ_RTTI(CloudscapeFeatureProcessor, "{B644679D-5585-4335-92C3-DFE0DD6AA186}", AZ::RPI::FeatureProcessor);

        static void Reflect(AZ::ReflectContext* context);

        CloudscapeFeatureProcessor() = default;
        virtual ~CloudscapeFeatureProcessor() = default;

        void UpdateShaderConstantData(const CloudscapeShaderConstantData& shaderData);

    private:
        CloudscapeFeatureProcessor(const CloudscapeFeatureProcessor&) = delete;

        friend class CloudscapeComputePass;
        friend class CloudscapeRenderPass;
        // friend class DepthBufferCopyPass;

        static constexpr char LogName[] = "CloudscapeFeatureProcessor";

        void ActivateInternal();

        AZ::Data::Instance<AZ::RPI::AttachmentImage> CreateCloudscapeOutputAttachment(
            const AZ::Name& attachmentName, const AzFramework::WindowSize attachmentSize) const;

        // Call by the passes owned by this feature processor.
        AZ::Data::Instance<AZ::RPI::AttachmentImage> GetOutput0ImageAttachment(unsigned int index)
        {
            return m_cloudOutputPairs[index].first;
        }
        AZ::Data::Instance<AZ::RPI::AttachmentImage> GetOutput1ImageAttachment(unsigned int index)
        {
            return m_cloudOutputPairs[index].second;
        }

        //////////////////////////////////////////////////////////////////
        //! AZ::RPI::FeatureProcessor overrides START...
        void Activate() override;
        void Deactivate() override;
        void Render(const RenderPacket& renderPacket) override;
        void AddRenderPasses(AZ::RPI::RenderPipeline* renderPipeline) override;
        //! AZ::RPI::FeatureProcessor overrides END ...
        ///////////////////////////////////////////////////////////////////

        static constexpr const char* FeatureProcessorName = "CloudscapeFeatureProcessor";

        // There are two fullscreen sized "render attachments" for the cloudscape.
        // These attachments exist per render pipeline.
        // They are owned by this feature processor.
        // Each frame one of the attachments is the current attachment and the other
        // represents the previous frame. This means that for all the passes involved
        // in cloudscape rendering these attachments become "Imported" attachments.

        AZStd::vector<AZStd::pair<AZ::Data::Instance<AZ::RPI::AttachmentImage>, AZ::Data::Instance<AZ::RPI::AttachmentImage>>>
            m_cloudOutputPairs;

        // We need a copy of the previous frame depth buffer, because we reproject 15/16 pixels each frame.
        // This causes visible artifacts at the borders of moving objects. The solution is that if
        // in the current frame a pixel is one of those non-raymarched pixels, and it is visible now, but was not visible
        // in the previous frame then we can choose to ray march it, or interpolate it.
        AZ::Data::Instance<AZ::RPI::AttachmentImage> m_previousFrameDepthBuffer;

        // The passes managed by this feature processor.
        AZStd::vector<CloudscapeComputePass*> m_cloudscapeComputePasses;
        AZStd::vector<AZ::RPI::ComputePass*> m_cloudscapeReprojectionPasses;
        AZStd::vector<CloudscapeRenderPass*> m_cloudscapeRenderPasses;

        // View to pass index map used when updating the pixel index of the passes.
        AZStd::map<AZ::RPI::ViewPtr, uint32_t> m_viewToIndexMap;

        // Shader constants for m_cloudscapeReprojectionPass
        AZ::RHI::ShaderInputNameIndex m_pixelIndex4x4Index = "m_pixelIndex4x4";

        const CloudscapeShaderConstantData* m_shaderConstantData = nullptr;

        ////////////////////////////////////////////////////

        AzFramework::WindowSize m_viewportSize{ 0, 0 };
    };
} // namespace VolumetricClouds
