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
    SPEED {
        INT id PK "主键"
        FLOAT value "速度值"
        VARCHAR date "日期（保留按日期查询）"
        TINYINT flag "使用标志，消费时置1"
        VARCHAR batch_no FK "采样批次标识"
        DATETIME sampled_at "采样时刻"
        VARCHAR status "AVAILABLE/CLAIMED/CONSUMED"
        VARCHAR claimed_by "当前领用者"
        DATETIME claimed_at "领用时刻"
        DATETIME lease_expires_at "租约到期时刻"
        DATETIME consumed_at "确认消费时刻（终态）"
    }
    SPEED_BATCH {
        BIGINT id PK "主键"
        VARCHAR batch_no UK "批次标识"
        DATETIME start_time "起始时刻"
        DATETIME end_time "截止时刻（>起始）"
        TINYINT status "0开放/1关闭"
    }
    SPEED_CLAIM_HISTORY {
        BIGINT id PK "主键"
        INT speed_id FK "速度记录ID"
        VARCHAR batch_no "批次标识"
        VARCHAR action "CLAIM/RELEASE/CONSUME"
        VARCHAR actor "任务标识"
        VARCHAR reason "动作原因"
        DATETIME created_at "发生时刻"
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
    SPEED_BATCH ||--o{ SPEED : "批次包含采样"
    SPEED ||--o{ SPEED_CLAIM_HISTORY : "领用状态历史"
```

## 2.1 SPEED 一次性领用 / 可追溯流程

针对“两个检测任务同时刻拿到同一条速度记录、日志只有日期无法追溯批次与领用时刻”的问题：

- **批次有效**：`SPEED_BATCH` 的 `start_time < end_time` 由 CHECK 约束强制；`claim` 仅在批次 `status=0` 且 `NOW()` 落在窗口内时发放。
- **并发不重复发放**：`claim()` 在单条事务内用 `SELECT ... FOR UPDATE SKIP LOCKED` 锁定候选行，再以 `WHERE status='AVAILABLE'` 条件更新。两个连接上的领取者同时竞争时，一个拿到记录，另一个跳到下一条或得到空结果。
- **取消/超时可重新释放**：持有者 `release()`（任务取消）或系统 `reclaimExpiredLeases()`（租约超时）把 `CLAIMED` 复位为 `AVAILABLE`，清空持有者与租约。
- **确认消费不可逆**：`consume()` 将记录置为 `CONSUMED` 终态并 `flag=1`；终态拒绝任何释放/他人确认，同任务重连后重试幂等成功。事务一旦 COMMIT，即使数据库连接重建，状态也不倒退。
- **全程可追溯**：每次 CLAIM / RELEASE / CONSUME 都向 `SPEED_CLAIM_HISTORY` 追加一行（动作、操作者、原因、时刻），运维可按记录或批次查清历次领取、释放、消费的原因。
- **向后兼容**：保留 `date`、`flag` 字段及 `findByDate()` 等原有调用方式。

### 领用状态机

```
AVAILABLE ──claim──▶ CLAIMED ──consume──▶ CONSUMED(终态)
     ▲                  │  ▲
     └──release/超时回收┘  └──仅在租约有效期内由持有者确认
```

### 事务边界

`insertWithBatch / claim / release / reclaimExpiredLeases / consume` 每个方法都是
`START TRANSACTION` → 行锁 + 状态翻转 + 历史写入 → `COMMIT`（异常 `ROLLBACK`）的完整边界，
保证状态变更与历史记录同生共死。多任务并发时各自使用独立连接（`DatabaseManager::create`）。

## 3. 模块清单

| 模块 | 文件 | 职责 |
|------|------|------|
| DatabaseManager | db/DatabaseManager.h/.cpp | 连接池管理、SQL执行 |
| Entity | entity/*.h | 各表实体类定义 |
| DAO | dao/*.h/*.cpp | 各表CRUD操作 |
| Logger | utils/Logger.h/.cpp | 日志记录 |
| Main | main.cpp | 入口与演示 |

## 4. 技术选型

- C++17
- MySQL Connector/C++ 8.0 (X DevAPI / Legacy C API)
- CMake 3.16+
- spdlog (日志，可选，本项目使用自实现轻量Logger)
