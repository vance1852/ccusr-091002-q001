-- ============================================
-- 工业检测系统 数据库初始化脚本
-- ============================================

CREATE DATABASE IF NOT EXISTS industrial_inspection
    DEFAULT CHARACTER SET utf8mb4
    DEFAULT COLLATE utf8mb4_unicode_ci;

USE industrial_inspection;

-- 速度领用相关表：先删子表再删父表
DROP TABLE IF EXISTS SPEED_CLAIM_EVENT;
DROP TABLE IF EXISTS SPEED;
DROP TABLE IF EXISTS SPEED_BATCH;

-- 速度采样批次表：批次的起止时间必须有效（valid_to 必须晚于 valid_from）
CREATE TABLE SPEED_BATCH (
    batch_id   VARCHAR(64) PRIMARY KEY COMMENT '批次标识',
    valid_from DATETIME(3) NOT NULL COMMENT '批次有效起始时间',
    valid_to   DATETIME(3) NOT NULL COMMENT '批次有效截止时间',
    created_at DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '批次登记时间',
    CONSTRAINT chk_speed_batch_window CHECK (valid_to > valid_from)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度采样批次表';

-- 速度表
CREATE TABLE SPEED (
    id          INT AUTO_INCREMENT PRIMARY KEY COMMENT '主键',
    batch_id    VARCHAR(64) NOT NULL DEFAULT '' COMMENT '采样批次标识，空串为未入批的历史数据',
    value       FLOAT NOT NULL COMMENT '速度值',
    date        VARCHAR(32) NOT NULL COMMENT '日期',
    sampled_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '采样时刻',
    status      VARCHAR(16) NOT NULL DEFAULT 'AVAILABLE' COMMENT '领用状态：AVAILABLE可领用/CLAIMED已领用/CONSUMED已消费',
    claimed_by  VARCHAR(64) DEFAULT NULL COMMENT '当前领取者',
    claimed_at  DATETIME(3) DEFAULT NULL COMMENT '当前领用时刻',
    claim_token VARCHAR(64) DEFAULT NULL COMMENT '当前领用令牌，每次领用唯一',
    flag        TINYINT DEFAULT 0 COMMENT '使用标志，1为已使用（废弃，请使用status）',
    INDEX idx_speed_date (date),
    INDEX idx_speed_batch_status (batch_id, status),
    UNIQUE INDEX uq_speed_claim_token (claim_token),
    CONSTRAINT chk_speed_status CHECK (status IN ('AVAILABLE', 'CLAIMED', 'CONSUMED'))
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度表';

-- 速度领用事件表：每条记录历次被领取、释放、消费的原因，供运维审计
CREATE TABLE SPEED_CLAIM_EVENT (
    id          BIGINT AUTO_INCREMENT PRIMARY KEY COMMENT '事件ID',
    speed_id    INT NOT NULL COMMENT '速度记录ID',
    batch_id    VARCHAR(64) NOT NULL COMMENT '批次标识',
    event       VARCHAR(16) NOT NULL COMMENT '事件类型：CLAIM领取/RELEASE释放/CONSUME消费',
    actor       VARCHAR(64) NOT NULL COMMENT '操作者',
    reason      VARCHAR(255) NOT NULL DEFAULT '' COMMENT '事件原因，如 任务取消/超时未消费/检测完成确认',
    claim_token VARCHAR(64) DEFAULT NULL COMMENT '关联的领用令牌',
    created_at  DATETIME(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) COMMENT '事件发生时间',
    INDEX idx_speed_claim_event_speed (speed_id),
    INDEX idx_speed_claim_event_batch (batch_id),
    CONSTRAINT chk_speed_claim_event_type CHECK (event IN ('CLAIM', 'RELEASE', 'CONSUME')),
    CONSTRAINT fk_speed_claim_event_speed FOREIGN KEY (speed_id) REFERENCES SPEED (id)
) ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COMMENT = '速度领用事件表';

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
