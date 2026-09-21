# 工业检测系统 - C++ MySQL 数据访问层

## 1. 系统架构

```mermaid
flowchart TD
    A[C++ Application Main] --> B[DatabaseManager]
    B --> C[MySQL Connector/C++]
    C --> D[(MySQL 8.0)]
    
    A --> E[SpeedDAO]
    A --> F[SpliceDAO]
    A --> G[FlawDAO]
    A --> H[StopDAO]
    A --> I[CompareDAO]
    A --> J[HistoryDAO]
    A --> K[RemoveDAO]
    
    E --> B
    F --> B
    G --> B
    H --> B
    I --> B
    J --> B
    K --> B
```

## 2. ER 图

```mermaid
erDiagram
    SPEED_BATCH {
        VARCHAR batch_id PK "批次标识"
        DATETIME valid_from "批次有效起始时间"
        DATETIME valid_to "批次有效截止时间"
        DATETIME created_at "批次登记时间"
    }
    SPEED {
        INT id PK "主键"
        VARCHAR batch_id "采样批次标识"
        FLOAT value "速度值"
        VARCHAR date "日期"
        DATETIME sampled_at "采样时刻"
        VARCHAR status "领用状态 AVAILABLE/CLAIMED/CONSUMED"
        VARCHAR claimed_by "当前领取者"
        DATETIME claimed_at "当前领用时刻"
        VARCHAR claim_token "当前领用令牌"
        TINYINT flag "使用标志（废弃）"
    }
    SPEED_CLAIM_EVENT {
        BIGINT id PK "事件ID"
        INT speed_id FK "速度记录ID"
        VARCHAR batch_id "批次标识"
        VARCHAR event "CLAIM/RELEASE/CONSUME"
        VARCHAR actor "操作者"
        VARCHAR reason "事件原因"
        VARCHAR claim_token "关联领用令牌"
        DATETIME created_at "事件发生时间"
    }
    SPLICE {
        INT id PK "主键"
        FLOAT location "当前位置"
        FLOAT distance "距离维修区距离"
        VARCHAR time "倒计时时间"
        TEXT url "保存路径"
        TINYINT last "当前检测接头标志"
        TINYINT flag "准备标志"
        TINYINT stop "停机标志"
    }
    FLAW {
        BIGINT id PK "主键"
        VARCHAR category "损伤类型"
        INT level "损伤级别"
        TEXT url "保存路径"
        INT camera "摄像头编号"
        FLOAT location "当前位置"
        FLOAT distance "距离维修区距离"
        VARCHAR size "损伤尺寸"
        VARCHAR coordinate "损伤坐标"
        VARCHAR date "记录日期"
        FLOAT time "倒计时时间"
        TINYINT flag "准备标志"
        TINYINT stop "停机标志"
        INT epoch "追踪圈数"
    }
    STOP {
        BIGINT id PK "主键"
        INT category "损伤类型"
        FLOAT distance "距离维修区距离"
        TINYINT flag "停机标志"
        TINYINT command "停机命令标志"
    }
    COMPARE {
        BIGINT id PK "主键"
        TEXT new_url "较新对比记录"
        TEXT old_url "较旧对比记录"
        FLOAT value "对比结果"
        INT category "类型"
        INT level "结果级别"
        VARCHAR old_size "较旧记录尺寸"
    }
    HISTORY {
        BIGINT id PK "主键"
        VARCHAR category "损伤类型"
        INT level "损伤级别"
        TEXT url "保存路径"
        INT camera "摄像头编号"
        VARCHAR size "损伤尺寸"
        VARCHAR date "记录日期"
    }
    REMOVE {
        BIGINT id PK "移除记录ID"
    }

    FLAW ||--o{ STOP : "损伤触发停机"
    SPLICE ||--o{ STOP : "接缝触发停机"
    FLAW ||--o{ COMPARE : "损伤对比"
    SPLICE ||--o{ COMPARE : "接缝对比"
    FLAW ||--o{ HISTORY : "损伤归档"
    FLAW ||--o{ REMOVE : "损伤移除"
    SPEED_BATCH ||--o{ SPEED : "批次包含记录"
    SPEED ||--o{ SPEED_CLAIM_EVENT : "领用历史"
```

## 3. 模块清单

| 模块 | 文件 | 职责 |
|------|------|------|
| DatabaseManager | db/DatabaseManager.h/.cpp | 连接管理、SQL执行、事务边界（transaction/begin/commit/rollback） |
| Entity | entity/*.h | 各表实体类定义 |
| DAO | dao/*.h/*.cpp | 各表CRUD操作；SpeedDAO 额外承担批次登记与一次性领用流程 |
| Logger | utils/Logger.h/.cpp | 日志记录 |
| Main | main.cpp | 入口与演示 |

## 5. SPEED 一次性领用流程

- 批次：`SPEED_BATCH` 登记采样批次，`CHECK(valid_to > valid_from)` 保证起止时间有效，只有窗口内的批次允许领取。
- 领取：`SpeedDAO::claimNext/claimById` 用单条原子 UPDATE 完成 `AVAILABLE -> CLAIMED`，并发竞争只有一个事务成功，不会重复发放。
- 释放：任务取消（`releaseClaim`）或超时（`releaseExpired`）后，未消费的记录回到 `AVAILABLE` 可重新领取。
- 消费：`consumeClaim` 只允许 `CLAIMED -> CONSUMED`，提交后持久化，连接重建也不会倒退。
- 审计：状态变更与 `SPEED_CLAIM_EVENT` 事件在同一事务内提交，运维可按记录或批次查询历次领取、释放、消费的原因。

## 4. 技术选型

- C++17
- MySQL Connector/C++ 8.0 (X DevAPI / Legacy C API)
- CMake 3.16+
- spdlog (日志，可选，本项目使用自实现轻量Logger)
