#pragma once
#include <string>

namespace entity {

    struct Speed {
        int id = 0;
        std::string batchId;     // 采样批次标识，空串为未入批的历史数据
        float value = 0.0f;
        std::string date;        // 日期（保留原有按日期查询）
        std::string sampledAt;   // 采样时刻
        std::string status;      // 领用状态：AVAILABLE/CLAIMED/CONSUMED
        std::string claimedBy;   // 当前领取者
        std::string claimedAt;   // 当前领用时刻
        std::string claimToken;  // 当前领用令牌，每次领用唯一
        int flag = 0;  // 使用标志，1为已使用（废弃，请使用 status）
    };

} // namespace entity
