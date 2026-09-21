#pragma once
#include <string>

namespace entity {

    // 速度记录的领用状态
    // AVAILABLE 可领用；CLAIMED 已领用待消费；CONSUMED 已确认消费（终态，不可回退）
    namespace speed_status {
        inline const std::string AVAILABLE = "AVAILABLE";
        inline const std::string CLAIMED   = "CLAIMED";
        inline const std::string CONSUMED  = "CONSUMED";
    }

    struct Speed {
        int id = 0;
        float value = 0.0f;
        std::string date;            // 日期（保留原有按日期查询口径）
        int flag = 0;                // 使用标志，1为已使用（废弃）；确认消费时置1

        // —— 批次与采样 ——
        std::string batchNo;         // 所属采样批次标识
        std::string sampledAt;       // 采样时刻（DATETIME）

        // —— 一次性领用状态 ——
        std::string status = speed_status::AVAILABLE;
        std::string claimedBy;       // 当前领用者（任务标识）
        std::string claimedAt;       // 最近一次领用时刻
        std::string leaseExpiresAt;  // 租约到期时刻，超时未消费可重新释放
        std::string consumedAt;      // 确认消费时刻（终态）
    };

} // namespace entity
