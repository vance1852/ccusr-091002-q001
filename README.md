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

初始化脚本创建 SPEED、SPEED_BATCH、SPEED_CLAIM_EVENT、SPLICE、FLAW、STOP、COMPARE、HISTORY 和 REMOVE 九张表。字段含义、默认值和索引以 SQL 脚本为准；应用通过 DAO 执行增删改查并在关键操作处记录日志。

注意：schema 变更后 MySQL 数据卷不会自动重建，需要 `docker compose down -v` 后再 `docker compose up --build -d` 让初始化脚本重新生效。

## SPEED 一次性领用流程

SPEED 记录按批次（SPEED_BATCH）管理，批次登记时校验起止时间有效（`valid_to > valid_from`，数据库 CHECK 约束兜底），只有处于有效窗口内的批次允许领取。每条记录有状态机 `AVAILABLE -> CLAIMED -> CONSUMED`：

- `SpeedDAO::claimNext / claimById`：单条原子 UPDATE 完成竞争，并发领取时只有一个调用成功，不会重复发放；
- `SpeedDAO::releaseClaim / releaseExpired`：任务取消或超时后，尚未消费的记录被释放回 AVAILABLE，可重新领取；
- `SpeedDAO::consumeClaim`：确认消费，只允许 CLAIMED -> CONSUMED，提交后即使数据库连接重建也不会倒退；
- 每次领取、释放、消费都会在同一事务内写入 SPEED_CLAIM_EVENT（操作者、原因、令牌、时间），运维可用 `findEventsBySpeedId / findEventsByBatch` 查清每条记录的历次流转。

原有 `findByDate` 等按日期查询的调用方式保持不变。应用启动演示（`demoSpeedClaim`）会实际跑一遍双线程并发领取、取消/超时释放、确认后重连的完整流程。
