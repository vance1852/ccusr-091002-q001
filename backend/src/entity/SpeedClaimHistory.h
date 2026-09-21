#pragma once
#include <string>

namespace entity {

    // 领用状态历史动作
    namespace speed_claim_action {
        inline const std::string CLAIM   = "CLAIM";    // 领用
        inline const std::string RELEASE = "RELEASE";  // 释放（任务取消 / 租约超时）
        inline const std::string CONSUME = "CONSUME";  // 确认消费（终态）
    }

    // 一条速度记录的每一次领用/释放/消费都会追加一行，只增不改
    struct SpeedClaimHistory {
        long long id = 0;
        int speedId = 0;
        std::string batchNo;
        std::string action;      // CLAIM / RELEASE / CONSUME
        std::string actor;       // 操作者（任务标识；超时回收时为原领用者）
        std::string reason;      // 动作原因
        std::string createdAt;
    };

} // namespace entity
