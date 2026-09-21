# 工业检测系统 - C++ MySQL 数据访问层

## 运行

使用 Docker Compose 启动 MySQL 和应用：

```bash
docker compose up --build -d
docker compose run --rm test
```

查看应用输出：

```bash
docker compose logs -f backend
```

本地构建需要 CMake 3.16、C++17 编译器和 MySQL Connector/C。进入 `backend` 后执行 `cmake ..` 和 `cmake --build .`，连接参数通过 `DB_HOST`、`DB_PORT`、`DB_USER`、`DB_PASSWORD`、`DB_NAME` 环境变量提供。生产环境应从运行时安全存储注入密码，不要把凭据提交到仓库或打印到日志。

## 目录职责

`backend/src/entity` 定义检测数据结构，`backend/src/dao` 封装各业务表的数据访问，`backend/src/db` 管理 MySQL 连接和查询，`backend/src/utils` 提供日志，`backend/src/test` 保存回归测试。`backend/sql/schema.sql` 是数据库初始化脚本，`docker-compose.yml` 描述本地服务依赖。

## 数据范围

初始化脚本创建 SPEED、SPLICE、FLAW、STOP、COMPARE、HISTORY 和 REMOVE 七张业务表，以及 SPEED 一次性领用所需的 SPEED_BATCH（采样批次）、SPEED_CLAIM_HISTORY（领用状态历史）两张配套表。字段含义、默认值和索引以 SQL 脚本为准；应用通过 DAO 执行增删改查并在关键操作处记录日志。

## SPEED 一次性领用与追溯

为解决“两个检测任务在同一时刻拿到同一条速度记录、日志只有日期无法追溯批次与领用时刻”的问题：

- 每条速度记录归属一个采样批次（`SPEED_BATCH`），批次起止时间由 `CHECK(end_time > start_time)` 保证有效，领用还要求批次开放且当前时刻落在窗口内。
- 领用状态机：`AVAILABLE → CLAIMED → CONSUMED`；任务取消或租约超时会把 `CLAIMED` 复位为 `AVAILABLE` 重新发放，`CONSUMED` 为不可倒退终态。
- `SpeedDAO::claim/release/reclaimExpiredLeases/consume` 各为一条事务边界，`claim` 使用 `SELECT ... FOR UPDATE SKIP LOCKED` + 条件更新，保证并发竞争时同一条记录只发放给一个领取者；已提交的确认即使数据库连接重建也不会回退。
- 每次 CLAIM/RELEASE/CONSUME 都向 `SPEED_CLAIM_HISTORY` 追加动作、操作者与原因，运维可用 `SpeedDAO::historyOf(id)` 或 `SPEED_CLAIM_HISTORY` 表查清每条记录的历次领取、释放、消费。
- 原有按日期查询（`findByDate`）与 `date`/`flag` 字段保持不变。

应用启动时（`demoSpeedTraceability`）会用两个独立连接上的并发线程同时领取同一条记录、演示取消释放与租约超时重发，并在确认消费后重建连接验证终态不倒退，最后打印每条记录的状态历史。

