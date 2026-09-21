-- ============================================
-- 工业检测系统 数据库初始化脚本
-- ============================================

CREATE DATABASE IF NOT EXISTS industrial_inspection
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE industrial_inspection;

-- 采样批次表（速度记录的领用批次，起止时间构成有效领用窗口）
-- 删除顺序：先子表后父表，避免外键约束冲突
DROP TABLE IF EXISTS SPEED_CLAIM_HISTORY;
DROP TABLE IF EXISTS SPEED;
DROP TABLE IF EXISTS SPEED_BATCH;

CREATE TABLE SPEED_BATCH (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    batch_no   VARCHAR(64) NOT NULL UNIQUE COMMENT '批次标识，如 B-20260920-001',
    start_time DATETIME NOT NULL COMMENT '批次起始时刻',
    end_time   DATETIME NOT NULL COMMENT '批次截止时刻，必须晚于起始时刻',
    status     TINYINT NOT NULL DEFAULT 0 COMMENT '批次状态，0开放领用，1已关闭',
    remark     VARCHAR(255) NOT NULL DEFAULT '' COMMENT '备注',
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '创建时间',
    CONSTRAINT chk_speed_batch_window CHECK (end_time > start_time)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度采样批次表';

-- 速度表（保留 date/flag 原有字段与按日期查询方式，新增批次、采样时刻与领用状态）
CREATE TABLE SPEED (
    id               INT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    value            FLOAT NOT NULL COMMENT '速度值',
    date             VARCHAR(32) NOT NULL COMMENT '日期（保留原有按日期查询口径）',
    flag             TINYINT DEFAULT 0 COMMENT '使用标志，1为已使用（废弃）；确认消费时置1',
    batch_no         VARCHAR(64) NULL COMMENT '所属采样批次标识',
    sampled_at       DATETIME NULL COMMENT '采样时刻',
    status           VARCHAR(16) NOT NULL DEFAULT 'AVAILABLE' COMMENT '领用状态：AVAILABLE可领用 / CLAIMED已领用待消费 / CONSUMED已确认消费（终态）',
    claimed_by       VARCHAR(64) NULL COMMENT '当前领用者（任务标识）',
    claimed_at       DATETIME(3) NULL COMMENT '最近一次领用时刻',
    lease_expires_at DATETIME(3) NULL COMMENT '领用租约到期时刻，超时未消费可重新释放',
    consumed_at      DATETIME(3) NULL COMMENT '确认消费时刻（终态时间戳）',
    KEY idx_speed_status (status),
    KEY idx_speed_batch (batch_no),
    KEY idx_speed_lease (status, lease_expires_at),
    KEY idx_speed_date (date),
    CONSTRAINT fk_speed_batch FOREIGN KEY (batch_no)
        REFERENCES SPEED_BATCH (batch_no)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度表';

-- 速度记录领用状态历史表（每次领用/释放/消费都追加一行，只增不改，供运维追溯）
CREATE TABLE SPEED_CLAIM_HISTORY (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    speed_id   INT NOT NULL COMMENT '速度记录ID',
    batch_no   VARCHAR(64) NULL COMMENT '领用发生时所属批次标识',
    action     VARCHAR(16) NOT NULL COMMENT '动作：CLAIM领用 / RELEASE释放 / CONSUME确认消费',
    actor      VARCHAR(64) NOT NULL DEFAULT '' COMMENT '操作者（任务标识；超时回收时为原领用者）',
    reason     VARCHAR(255) NOT NULL DEFAULT '' COMMENT '动作原因，如任务取消、租约超时、结果确认',
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '动作发生时刻',
    KEY idx_hist_speed (speed_id),
    KEY idx_hist_batch (batch_no),
    KEY idx_hist_action (action),
    CONSTRAINT fk_hist_speed FOREIGN KEY (speed_id)
        REFERENCES SPEED (id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度领用状态历史表';

-- 接缝表
DROP TABLE IF EXISTS SPLICE;
CREATE TABLE SPLICE (
    id       INT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    location FLOAT NOT NULL COMMENT '当前位置',
    distance FLOAT NOT NULL COMMENT '距离维修区距离',
    time     VARCHAR(32) NOT NULL COMMENT '倒计时时间（秒）',
    url      TEXT NOT NULL COMMENT '保存路径',
    last     TINYINT DEFAULT 0 COMMENT '当前检测接头标志，1有效',
    flag     TINYINT DEFAULT 0 COMMENT '准备标志，不为0则准备停机',
    stop     TINYINT DEFAULT 0 COMMENT '停机标志，不为0则可以停机'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '接缝表';

-- 损伤表
DROP TABLE IF EXISTS FLAW;
CREATE TABLE FLAW (
    id         BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    category   VARCHAR(64) NOT NULL COMMENT '损伤类型',
    level      INT NOT NULL COMMENT '损伤级别',
    url        TEXT NOT NULL COMMENT '损伤记录保存路径',
    camera     INT NOT NULL COMMENT '监控摄像头编号',
    location   FLOAT NOT NULL COMMENT '当前位置',
    distance   FLOAT NOT NULL COMMENT '距离维修区距离',
    size       VARCHAR(64) NOT NULL COMMENT '损伤尺寸',
    coordinate VARCHAR(64) NOT NULL COMMENT '损伤坐标',
    date       VARCHAR(32) NOT NULL COMMENT '记录日期',
    time       FLOAT NOT NULL COMMENT '倒计时时间（秒）',
    flag       TINYINT DEFAULT 0 COMMENT '准备标志，不为0则准备停机',
    stop       TINYINT DEFAULT 0 COMMENT '停机标志，不为0则可以停机',
    epoch      INT DEFAULT 0 COMMENT '追踪当前缺陷的圈数'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '损伤表';

-- 停机表
DROP TABLE IF EXISTS STOP;
CREATE TABLE STOP (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    category INT NOT NULL COMMENT '损伤类型',
    distance FLOAT NOT NULL COMMENT '距离维修区距离',
    flag     TINYINT DEFAULT 0 COMMENT '停机标志，1为允许停机',
    command  TINYINT DEFAULT 0 COMMENT '停机命令标志，1为下发'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '停机表';

-- 对比表
DROP TABLE IF EXISTS COMPARE;
CREATE TABLE COMPARE (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    new_url  TEXT NOT NULL COMMENT '较新对比记录',
    old_url  TEXT NOT NULL COMMENT '较旧对比记录',
    value    FLOAT NOT NULL COMMENT '对比结果',
    category INT NOT NULL COMMENT '类型',
    level    INT NOT NULL COMMENT '结果级别',
    old_size VARCHAR(64) NOT NULL COMMENT '较旧记录尺寸（只对损伤有效）'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '对比表';

-- 历史表
DROP TABLE IF EXISTS HISTORY;
CREATE TABLE HISTORY (
    id       BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '长ID为损伤，短ID为接缝',
    category VARCHAR(64) NOT NULL COMMENT '损伤类型',
    level    INT NOT NULL COMMENT '损伤级别',
    url      TEXT NOT NULL COMMENT '损伤记录保存路径',
    camera   INT NOT NULL COMMENT '监控摄像头编号',
    size     VARCHAR(64) NOT NULL COMMENT '损伤尺寸',
    date     VARCHAR(32) NOT NULL COMMENT '记录日期'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '历史表';

-- 移除表
DROP TABLE IF EXISTS REMOVE;
CREATE TABLE REMOVE (
    id BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '移除记录ID'
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '移除表';
