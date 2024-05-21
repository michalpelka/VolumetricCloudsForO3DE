#pragma once

#include <ROS2/RobotControl/ControlSubscriptionHandler.h>
#include <std_msgs/msg/float32.hpp>

namespace VolumetricClouds
{
    class CloudscapeSubscriptionHandler : public ROS2::ControlSubscriptionHandler<std_msgs::msg::Float32>
    {
    public:
        using MessageCallback = AZStd::function<void(const std_msgs::msg::Float32&)>;

        explicit CloudscapeSubscriptionHandler(MessageCallback messageCallback);

    private:
        // ROS2::ControlSubscriptionHandler overrides
        void SendToBus(const std_msgs::msg::Float32& message) override;

        MessageCallback m_messageCallback;
    };
} // VolumetricClouds
