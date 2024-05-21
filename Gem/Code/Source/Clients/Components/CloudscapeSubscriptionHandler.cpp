#include "CloudscapeSubscriptionHandler.h"
#include <VolumetricClouds/VolumetricCloudsBus.h>

namespace VolumetricClouds
{
    CloudscapeSubscriptionHandler::CloudscapeSubscriptionHandler(MessageCallback messageCallback)
        : m_messageCallback(AZStd::move(messageCallback))
    {
    };

    void CloudscapeSubscriptionHandler::SendToBus(const std_msgs::msg::Float32& message)
    {
        m_messageCallback(message);
    }
} // namespace VolumetricClouds
