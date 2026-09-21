#pragma once
#include <string>

namespace entity {

    // 速度采样批次：起止时间构成有效领用窗口
    struct SpeedBatch {
        long long id = 0;
        std::string batchNo;     // 批次标识（唯一）
        std::string startTime;   // 起始时刻（DATETIME）
        std::string endTime;     // 截止时刻（DATETIME，必须晚于起始时刻）
        int status = 0;          // 0开放领用，1已关闭
        std::string remark;
        std::string createdAt;
    };

} // namespace entity
