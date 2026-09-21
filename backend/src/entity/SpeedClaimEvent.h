#pragma once
#include <string>

namespace entity {

    // 速度记录的一次领用事件（领取/释放/消费），供运维审计
    struct SpeedClaimEvent {
        long long id = 0;
        int speedId = 0;           // 速度记录ID
        std::string batchId;       // 批次标识
        std::string event;         // 事件类型：CLAIM/RELEASE/CONSUME
        std::string actor;         // 操作者
        std::string reason;        // 事件原因（任务取消/超时未消费/检测完成确认等）
        std::string claimToken;    // 关联的领用令牌
        std::string createdAt;     // 事件发生时间
    };

} // namespace entity
