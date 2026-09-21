#pragma once

#include "BaseDAO.h"
#include "../entity/Speed.h"
#include "../entity/SpeedBatch.h"
#include "../entity/SpeedClaimEvent.h"
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <vector>
#include <optional>

namespace dao {

    class SpeedDAO : public BaseDAO {
    public:
        // ============================================
        // 批次管理
        // ============================================

        // 登记采样批次。批次的起止时间必须有效（validTo 晚于 validFrom），
        // 除此处校验外，数据库还有 CHECK 约束兜底。
        void registerBatch(const entity::SpeedBatch& batch) {
            if (batch.batchId.empty() || batch.validFrom.empty() || batch.validTo.empty()
                || !(batch.validFrom < batch.validTo)) {
                throw std::invalid_argument("SpeedDAO: invalid batch window, batch=" + batch.batchId);
            }
            std::string sql = "INSERT INTO SPEED_BATCH (batch_id, valid_from, valid_to) VALUES ("
                + esc(batch.batchId) + ", "
                + esc(batch.validFrom) + ", "
                + esc(batch.validTo) + ")";
            LOG_INFO("SpeedDAO", "Register batch " + batch.batchId
                + " window=[" + batch.validFrom + ", " + batch.validTo + "]");
            db().execute(sql);
        }

        // 按标识查询批次
        std::optional<entity::SpeedBatch> findBatch(const std::string& batchId) {
            auto rows = db().query("SELECT * FROM SPEED_BATCH WHERE batch_id = " + esc(batchId));
            if (rows.empty()) return std::nullopt;
            entity::SpeedBatch b;
            b.batchId = getVal(rows[0], "batch_id");
            b.validFrom = getVal(rows[0], "valid_from");
            b.validTo = getVal(rows[0], "valid_to");
            b.createdAt = getVal(rows[0], "created_at");
            return b;
        }

        // ============================================
        // 一次性领用流程
        // ============================================

        // 原子领取批次内最早一条可领用记录。
        // 单条 UPDATE 即完成竞争：并发时只有第一个事务 affected=1，其余返回空；
        // 状态变更与领用事件在同一事务边界内提交，要么同时成功要么同时回滚。
        std::optional<entity::Speed> claimNext(const std::string& batchId, const std::string& actor,
                                               const std::string& reason = "") {
            std::string token = newClaimToken(actor);
            return db().transaction([&]() -> std::optional<entity::Speed> {
                std::string update =
                    "UPDATE SPEED s JOIN ("
                    "SELECT s2.id FROM SPEED s2 "
                    "JOIN SPEED_BATCH b ON b.batch_id = s2.batch_id "
                    "WHERE s2.batch_id = " + esc(batchId) + " AND s2.status = 'AVAILABLE' "
                    "AND NOW(3) BETWEEN b.valid_from AND b.valid_to "
                    "ORDER BY s2.id LIMIT 1"
                    ") pick ON pick.id = s.id "
                    "SET s.status = 'CLAIMED', s.claimed_by = " + esc(actor) + ", "
                    "s.claimed_at = NOW(3), s.claim_token = " + esc(token) + " "
                    "WHERE s.status = 'AVAILABLE'";
                if (db().execute(update) == 0) {
                    LOG_INFO("SpeedDAO", "Claim failed (no available record), batch="
                        + batchId + " actor=" + actor);
                    return std::nullopt;
                }
                auto rows = db().query("SELECT * FROM SPEED WHERE claim_token = " + esc(token));
                if (rows.empty()) return std::nullopt;
                entity::Speed claimed = mapRow(rows[0]);
                insertEvent(claimed.id, claimed.batchId, "CLAIM", actor, reason, token);
                LOG_INFO("SpeedDAO", "Claimed speed id=" + itos(claimed.id)
                    + " batch=" + claimed.batchId + " actor=" + actor);
                return claimed;
            });
        }

        // 按记录ID原子领取，同样校验批次有效期与当前状态
        std::optional<entity::Speed> claimById(int id, const std::string& actor,
                                               const std::string& reason = "") {
            std::string token = newClaimToken(actor);
            return db().transaction([&]() -> std::optional<entity::Speed> {
                std::string update =
                    "UPDATE SPEED s "
                    "SET s.status = 'CLAIMED', s.claimed_by = " + esc(actor) + ", "
                    "s.claimed_at = NOW(3), s.claim_token = " + esc(token) + " "
                    "WHERE s.id = " + itos(id) + " AND s.status = 'AVAILABLE' "
                    "AND EXISTS (SELECT 1 FROM SPEED_BATCH b "
                    "WHERE b.batch_id = s.batch_id "
                    "AND NOW(3) BETWEEN b.valid_from AND b.valid_to)";
                if (db().execute(update) == 0) {
                    LOG_INFO("SpeedDAO", "Claim failed, id=" + itos(id) + " actor=" + actor);
                    return std::nullopt;
                }
                auto found = findById(id);
                if (!found) return std::nullopt;
                insertEvent(id, found->batchId, "CLAIM", actor, reason, token);
                LOG_INFO("SpeedDAO", "Claimed speed id=" + itos(id) + " actor=" + actor);
                return found;
            });
        }

        // 释放已领取但尚未消费的记录（任务取消等原因），释放后可重新领取。
        // 必须持有当前领用令牌；已消费的记录不受影响。
        bool releaseClaim(int id, const std::string& claimToken, const std::string& actor,
                          const std::string& reason) {
            return db().transaction([&]() -> bool {
                auto rows = db().query("SELECT batch_id FROM SPEED WHERE id = " + itos(id));
                if (rows.empty()) return false;
                std::string batchId = getVal(rows[0], "batch_id");
                std::string update =
                    "UPDATE SPEED SET status = 'AVAILABLE', claimed_by = NULL, "
                    "claimed_at = NULL, claim_token = NULL "
                    "WHERE id = " + itos(id) + " AND status = 'CLAIMED' "
                    "AND claim_token = " + esc(claimToken);
                if (db().execute(update) == 0) {
                    LOG_WARN("SpeedDAO", "Release rejected, id=" + itos(id) + " actor=" + actor);
                    return false;
                }
                insertEvent(id, batchId, "RELEASE", actor, reason, claimToken);
                LOG_INFO("SpeedDAO", "Released speed id=" + itos(id) + " reason=" + reason);
                return true;
            });
        }

        // 释放批次内超时未消费的记录，返回释放条数
        int releaseExpired(const std::string& batchId, int claimedForSeconds,
                           const std::string& actor, const std::string& reason) {
            return db().transaction([&]() -> int {
                auto rows = db().query(
                    "SELECT id, claim_token FROM SPEED WHERE batch_id = " + esc(batchId)
                    + " AND status = 'CLAIMED' AND claimed_at < NOW(3) - INTERVAL "
                    + itos(claimedForSeconds) + " SECOND");
                int released = 0;
                for (auto& row : rows) {
                    int id = getInt(row, "id");
                    std::string token = getVal(row, "claim_token");
                    std::string update =
                        "UPDATE SPEED SET status = 'AVAILABLE', claimed_by = NULL, "
                        "claimed_at = NULL, claim_token = NULL "
                        "WHERE id = " + itos(id) + " AND status = 'CLAIMED' "
                        "AND claim_token = " + esc(token);
                    if (db().execute(update) == 1) {
                        insertEvent(id, batchId, "RELEASE", actor, reason, token);
                        ++released;
                    }
                }
                if (released > 0) {
                    LOG_INFO("SpeedDAO", "Released " + itos(released)
                        + " expired claims, batch=" + batchId + " reason=" + reason);
                }
                return released;
            });
        }

        // 确认消费已领取的记录。状态机只允许 CLAIMED -> CONSUMED，
        // 确认随事务提交持久化，之后即使数据库连接重建也不会倒退。
        bool consumeClaim(int id, const std::string& claimToken, const std::string& actor,
                          const std::string& reason) {
            return db().transaction([&]() -> bool {
                auto rows = db().query("SELECT batch_id FROM SPEED WHERE id = " + itos(id));
                if (rows.empty()) return false;
                std::string batchId = getVal(rows[0], "batch_id");
                std::string update =
                    "UPDATE SPEED SET status = 'CONSUMED' "
                    "WHERE id = " + itos(id) + " AND status = 'CLAIMED' "
                    "AND claim_token = " + esc(claimToken);
                if (db().execute(update) == 0) {
                    LOG_WARN("SpeedDAO", "Consume rejected, id=" + itos(id) + " actor=" + actor);
                    return false;
                }
                insertEvent(id, batchId, "CONSUME", actor, reason, claimToken);
                LOG_INFO("SpeedDAO", "Consumed speed id=" + itos(id) + " reason=" + reason);
                return true;
            });
        }

        // ============================================
        // 领用历史（运维审计）
        // ============================================

        // 查询某条速度记录的全部领用事件，按发生先后排序
        std::vector<entity::SpeedClaimEvent> findEventsBySpeedId(int speedId) {
            std::string sql = "SELECT * FROM SPEED_CLAIM_EVENT WHERE speed_id = "
                + itos(speedId) + " ORDER BY id ASC";
            return mapEvents(db().query(sql));
        }

        // 查询某批次的全部领用事件，按发生先后排序
        std::vector<entity::SpeedClaimEvent> findEventsByBatch(const std::string& batchId) {
            std::string sql = "SELECT * FROM SPEED_CLAIM_EVENT WHERE batch_id = "
                + esc(batchId) + " ORDER BY id ASC";
            return mapEvents(db().query(sql));
        }

        // ============================================
        // 原有 CRUD（调用方式保持不变）
        // ============================================

        // 插入速度记录，返回自增ID。
        // 兼容旧调用：未设置 batchId / sampledAt 时使用数据库默认值（未入批 / 当前时刻）。
        int insert(const entity::Speed& speed) {
            std::string cols = "value, date, flag";
            std::string vals = ftos(speed.value) + ", " + esc(speed.date) + ", " + itos(speed.flag);
            if (!speed.batchId.empty()) {
                cols += ", batch_id";
                vals += ", " + esc(speed.batchId);
            }
            if (!speed.sampledAt.empty()) {
                cols += ", sampled_at";
                vals += ", " + esc(speed.sampledAt);
            }
            std::string sql = "INSERT INTO SPEED (" + cols + ") VALUES (" + vals + ")";
            LOG_INFO("SpeedDAO", "Insert speed record, batch="
                + (speed.batchId.empty() ? std::string("<none>") : speed.batchId));
            return static_cast<int>(db().insertAndGetId(sql));
        }

        // 根据ID查询
        std::optional<entity::Speed> findById(int id) {
            std::string sql = "SELECT * FROM SPEED WHERE id = " + itos(id);
            auto rows = db().query(sql);
            if (rows.empty()) return std::nullopt;
            return mapRow(rows[0]);
        }

        // 查询所有记录
        std::vector<entity::Speed> findAll() {
            auto rows = db().query("SELECT * FROM SPEED ORDER BY id DESC");
            return mapRows(rows);
        }

        // 按日期查询
        std::vector<entity::Speed> findByDate(const std::string& date) {
            std::string sql = "SELECT * FROM SPEED WHERE date = " + esc(date) + " ORDER BY id DESC";
            return mapRows(db().query(sql));
        }

        // 查询未使用的记录
        std::vector<entity::Speed> findUnused() {
            return mapRows(db().query("SELECT * FROM SPEED WHERE flag = 0 ORDER BY id DESC"));
        }

        // 更新速度值
        int updateValue(int id, float value) {
            std::string sql = "UPDATE SPEED SET value = " + ftos(value) + " WHERE id = " + itos(id);
            LOG_INFO("SpeedDAO", "Update speed id=" + itos(id));
            return db().execute(sql);
        }

        // 标记为已使用
        int markUsed(int id) {
            std::string sql = "UPDATE SPEED SET flag = 1 WHERE id = " + itos(id);
            LOG_INFO("SpeedDAO", "Mark speed used id=" + itos(id));
            return db().execute(sql);
        }

        // 删除记录
        int deleteById(int id) {
            std::string sql = "DELETE FROM SPEED WHERE id = " + itos(id);
            LOG_INFO("SpeedDAO", "Delete speed id=" + itos(id));
            return db().execute(sql);
        }

        // 获取记录总数
        int count() {
            auto rows = db().query("SELECT COUNT(*) AS cnt FROM SPEED");
            return rows.empty() ? 0 : getInt(rows[0], "cnt");
        }

    private:
        // 生成每次领用唯一的令牌
        static std::string newClaimToken(const std::string& actor) {
            static std::atomic<unsigned long> seq{0};
            long long us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return actor + "-" + std::to_string(us) + "-" + std::to_string(seq.fetch_add(1));
        }

        // 在当前事务内追加一条领用事件
        void insertEvent(int speedId, const std::string& batchId, const std::string& event,
                         const std::string& actor, const std::string& reason,
                         const std::string& token) {
            std::string sql =
                "INSERT INTO SPEED_CLAIM_EVENT (speed_id, batch_id, event, actor, reason, claim_token) VALUES ("
                + itos(speedId) + ", " + esc(batchId) + ", " + esc(event) + ", " + esc(actor) + ", "
                + esc(reason) + ", " + (token.empty() ? std::string("NULL") : esc(token)) + ")";
            db().execute(sql);
        }

        entity::Speed mapRow(const db::Row& row) {
            entity::Speed s;
            s.id = getInt(row, "id");
            s.batchId = getVal(row, "batch_id");
            s.value = getFloat(row, "value");
            s.date = getVal(row, "date");
            s.sampledAt = getVal(row, "sampled_at");
            s.status = getVal(row, "status");
            s.claimedBy = getVal(row, "claimed_by");
            s.claimedAt = getVal(row, "claimed_at");
            s.claimToken = getVal(row, "claim_token");
            s.flag = getInt(row, "flag");
            return s;
        }

        std::vector<entity::Speed> mapRows(const db::ResultSet& rows) {
            std::vector<entity::Speed> result;
            result.reserve(rows.size());
            for (auto& row : rows) {
                result.push_back(mapRow(row));
            }
            return result;
        }

        entity::SpeedClaimEvent mapEvent(const db::Row& row) {
            entity::SpeedClaimEvent e;
            e.id = getLong(row, "id");
            e.speedId = getInt(row, "speed_id");
            e.batchId = getVal(row, "batch_id");
            e.event = getVal(row, "event");
            e.actor = getVal(row, "actor");
            e.reason = getVal(row, "reason");
            e.claimToken = getVal(row, "claim_token");
            e.createdAt = getVal(row, "created_at");
            return e;
        }

        std::vector<entity::SpeedClaimEvent> mapEvents(const db::ResultSet& rows) {
            std::vector<entity::SpeedClaimEvent> result;
            result.reserve(rows.size());
            for (auto& row : rows) {
                result.push_back(mapEvent(row));
            }
            return result;
        }
    };

} // namespace dao
