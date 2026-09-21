#pragma once
#include <string>

namespace entity {

    // 速度采样批次：起止时间必须有效（validTo 必须晚于 validFrom）
    struct SpeedBatch {
        std::string batchId;    // 批次标识
        std::string validFrom;  // 批次有效起始时间
        std::string validTo;    // 批次有效截止时间
        std::string createdAt;  // 批次登记时间
    };

} // namespace entity
