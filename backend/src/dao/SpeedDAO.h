#pragma once

#include "BaseDAO.h"
#include "../entity/Speed.h"
#include "../entity/SpeedClaimHistory.h"
#include "SpeedClaimHistoryDAO.h"
#include <vector>
#include <optional>

namespace dao {

    class SpeedDAO : public BaseDAO {
    public:
        SpeedDAO() = default;

        // 使用独立连接（每个并发任务一条连接，保证各自事务与行锁相互独立）
        explicit SpeedDAO(db::DatabaseManager& conn)
            : BaseDAO(conn), history_(conn) {}

        // ============================================================
        // 原有 CRUD（签名与按日期查询方式保持不变）
        // ============================================================

        // 插入速度记录，返回自增ID（不带批次，批次相关列取默认值）
        int insert(const entity::Speed& speed) {
            std::string sql = "INSERT INTO SPEED (value, date, flag) VALUES ("
                + ftos(speed.value) + ", "
                + esc(speed.date) + ", "
                + itos(speed.flag) + ")";
            LOG_INFO("SpeedDAO", "Insert speed record");
            return static_cast<int>(db().insertAndGetId(sql));
        }

        // 按批次与采样时刻插入速度记录（采样入库）。
        // 批次必须存在，否则回滚并抛异常。
        int insertWithBatch(const entity::Speed& speed) {
            if (speed.batchNo.empty() || speed.sampledAt.empty()) {
                LOG_ERROR("SpeedDAO", "insertWithBatch rejected: batchNo/sampledAt must not be empty");
                return 0;
            }
            auto& dm = db();
            dm.beginTransaction();
            try {
                auto exists = dm.query(
                    "SELECT id FROM SPEED_BATCH WHERE batch_no = " + esc(speed.batchNo) + " LIMIT 1");
                if (exists.empty()) {
                    dm.rollback();
                    LOG_ERROR("SpeedDAO", "insertWithBatch rejected: batch not found " + speed.batchNo);
                    return 0;
                }
                std::string sql =
                    "INSERT INTO SPEED (value, date, flag, batch_no, sampled_at, status) VALUES ("
                    + ftos(speed.value) + ", "
                    + esc(speed.date) + ", "
                    + itos(speed.flag) + ", "
                    + esc(speed.batchNo) + ", "
                    + esc(speed.sampledAt) + ", "
                    + esc(entity::speed_status::AVAILABLE) + ")";
                long long id = dm.insertAndGetId(sql);
                dm.commit();
                LOG_INFO("SpeedDAO", "Insert speed record into batch " + speed.batchNo);
                return static_cast<int>(id);
            } catch (const std::exception&) {
                dm.rollback();
                throw;
            }
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

        // 按日期查询（原有调用方式保持不变）
        std::vector<entity::Speed> findByDate(const std::string& date) {
            std::string sql = "SELECT * FROM SPEED WHERE date = " + esc(date) + " ORDER BY id DESC";
            return mapRows(db().query(sql));
        }

        // 按批次查询，按采样时刻排序
        std::vector<entity::Speed> findByBatchNo(const std::string& batchNo) {
            std::string sql = "SELECT * FROM SPEED WHERE batch_no = " + esc(batchNo)
                + " ORDER BY sampled_at ASC, id ASC";
            return mapRows(db().query(sql));
        }

        // 查询未使用的记录（原语义：flag = 0）
        std::vector<entity::Speed> findUnused() {
            return mapRows(db().query("SELECT * FROM SPEED WHERE flag = 0 ORDER BY id DESC"));
        }

        // 查询处于领用在途（已领用未消费）的记录
        std::vector<entity::Speed> findClaimed() {
            return mapRows(db().query(
                "SELECT * FROM SPEED WHERE status = " + esc(entity::speed_status::CLAIMED)
                + " ORDER BY claimed_at ASC"));
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

        // ============================================================
        // 一次性领用流程（领用 / 释放 / 超时回收 / 确认消费）
        // 每个方法都是一个完整事务边界：状态翻转与历史写入同生共死，
        // 借助 InnoDB 行锁（FOR UPDATE SKIP LOCKED）保证并发下不重复发放。
        // ============================================================

        // 领用一条批次内、处于开放窗口的可领用记录。
        // 并发竞争时只有一个领取者成功；其余领取者拿到下一条或 nullopt。
        // leaseSeconds 为领用租约秒数，超时未确认可被回收重新发放。
        std::optional<entity::Speed> claim(const std::string& batchNo,
                                           const std::string& taskId,
                                           int leaseSeconds,
                                           const std::string& reason = "") {
            auto& dm = db();
            dm.beginTransaction();
            int claimedId = 0;
            try {
                // 锁定候选行：必须 AVAILABLE、批次开放且当前时间在有效起止窗口内。
                // SKIP LOCKED 让并发领取者直接跳过被他人锁住的记录，从根上杜绝重复发放。
                auto rows = dm.query(
                    "SELECT s.id AS id FROM SPEED s "
                    "JOIN SPEED_BATCH b ON s.batch_no = b.batch_no "
                    "WHERE s.status = " + esc(entity::speed_status::AVAILABLE)
                    + " AND s.batch_no = " + esc(batchNo)
                    + " AND b.status = 0 AND NOW(3) BETWEEN b.start_time AND b.end_time "
                    "ORDER BY s.sampled_at ASC, s.id ASC LIMIT 1 FOR UPDATE SKIP LOCKED");
                if (rows.empty()) {
                    dm.rollback();
                    LOG_INFO("SpeedDAO", "No claimable speed in batch " + batchNo
                             + " for task " + taskId);
                    return std::nullopt;
                }
                claimedId = getInt(rows[0], "id");

                // 双重保险：条件更新，只有仍是 AVAILABLE 才能翻成 CLAIMED。
                int affected = dm.execute(
                    "UPDATE SPEED SET "
                    "status = " + esc(entity::speed_status::CLAIMED) + ", "
                    "claimed_by = " + esc(taskId) + ", "
                    "claimed_at = NOW(3), "
                    "lease_expires_at = DATE_ADD(NOW(3), INTERVAL " + itos(leaseSeconds) + " SECOND) "
                    "WHERE id = " + itos(claimedId)
                    + " AND status = " + esc(entity::speed_status::AVAILABLE));
                if (affected != 1) {
                    dm.rollback(); // 已被并发竞争者抢先
                    return std::nullopt;
                }

                history_.append(claimedId, batchNo,
                                entity::speed_claim_action::CLAIM, taskId, reason);
                dm.commit();
            } catch (const std::exception&) {
                dm.rollback();
                throw;
            }

            LOG_INFO("SpeedDAO", "Speed id=" + itos(claimedId) + " claimed by " + taskId);
            return findById(claimedId);
        }

        // 释放在途领用（典型场景：任务取消）。仅当前持有者可释放，CONSUMED 终态不可回退。
        // 返回 false 表示记录不存在、不在 CLAIMED 状态或非持有者。
        bool release(int id, const std::string& taskId, const std::string& reason) {
            auto& dm = db();
            dm.beginTransaction();
            try {
                auto rows = dm.query(
                    "SELECT * FROM SPEED WHERE id = " + itos(id) + " FOR UPDATE");
                if (rows.empty() || getVal(rows[0], "status") != entity::speed_status::CLAIMED
                    || getVal(rows[0], "claimed_by") != taskId) {
                    dm.rollback();
                    return false;
                }
                std::string batchNo = getVal(rows[0], "batch_no");
                int affected = dm.execute(
                    "UPDATE SPEED SET status = " + esc(entity::speed_status::AVAILABLE) + ", "
                    "claimed_by = NULL, claimed_at = NULL, lease_expires_at = NULL "
                    "WHERE id = " + itos(id)
                    + " AND status = " + esc(entity::speed_status::CLAIMED)
                    + " AND claimed_by = " + esc(taskId));
                if (affected != 1) {
                    dm.rollback();
                    return false;
                }
                history_.append(id, batchNo,
                                entity::speed_claim_action::RELEASE, taskId, reason);
                dm.commit();
                LOG_INFO("SpeedDAO", "Speed id=" + itos(id) + " released by " + taskId
                         + " (" + reason + ")");
                return true;
            } catch (const std::exception&) {
                dm.rollback();
                throw;
            }
        }

        // 超时回收：把租约到期、尚未消费的记录重新释放为可领用。
        // 返回回收条数。每条都追加 RELEASE 历史，actor 记录原领用者，reason 标明超时。
        int reclaimExpiredLeases(const std::string& batchNo = "", int limit = 500) {
            auto& dm = db();
            dm.beginTransaction();
            int reclaimed = 0;
            try {
                std::string sql =
                    "SELECT * FROM SPEED WHERE status = " + esc(entity::speed_status::CLAIMED)
                    + " AND lease_expires_at IS NOT NULL AND lease_expires_at <= NOW(3)";
                if (!batchNo.empty()) sql += " AND batch_no = " + esc(batchNo);
                sql += " ORDER BY id ASC LIMIT " + itos(limit) + " FOR UPDATE SKIP LOCKED";
                auto rows = dm.query(sql);

                for (auto& row : rows) {
                    int id = getInt(row, "id");
                    std::string holder = getVal(row, "claimed_by");
                    std::string bNo = getVal(row, "batch_no");
                    int affected = dm.execute(
                        "UPDATE SPEED SET status = " + esc(entity::speed_status::AVAILABLE) + ", "
                        "claimed_by = NULL, claimed_at = NULL, lease_expires_at = NULL "
                        "WHERE id = " + itos(id)
                        + " AND status = " + esc(entity::speed_status::CLAIMED));
                    if (affected == 1) {
                        history_.append(id, bNo,
                                        entity::speed_claim_action::RELEASE,
                                        holder, "租约超时，系统自动回收释放");
                        reclaimed++;
                    }
                }
                dm.commit();
            } catch (const std::exception&) {
                dm.rollback();
                throw;
            }
            if (reclaimed > 0) {
                LOG_INFO("SpeedDAO", "Reclaimed " + itos(reclaimed) + " expired speed leases");
            }
            return reclaimed;
        }

        // 确认消费（终态，不可逆）。仅租约仍有效的当前持有者可确认；
        // 同时置 flag=1。CONSUMED 不会被任何释放/领用路径倒退。
        // 同一持有者重复确认（例如连接重建后重试）幂等返回 true。
        bool consume(int id, const std::string& taskId, const std::string& reason) {
            auto& dm = db();
            dm.beginTransaction();
            try {
                auto rows = dm.query(
                    "SELECT * FROM SPEED WHERE id = " + itos(id) + " FOR UPDATE");
                if (rows.empty()) {
                    dm.rollback();
                    return false;
                }
                std::string status = getVal(rows[0], "status");
                std::string holder = getVal(rows[0], "claimed_by");
                std::string batchNo = getVal(rows[0], "batch_no");

                // 已确认终态：同任务重试视为幂等成功；其他任务无权触碰，绝不回退。
                if (status == entity::speed_status::CONSUMED) {
                    dm.rollback();
                    return holder == taskId;
                }
                if (status != entity::speed_status::CLAIMED || holder != taskId) {
                    dm.rollback();
                    return false;
                }

                int affected = dm.execute(
                    "UPDATE SPEED SET status = " + esc(entity::speed_status::CONSUMED) + ", "
                    "flag = 1, consumed_at = NOW(3), lease_expires_at = NULL "
                    "WHERE id = " + itos(id)
                    + " AND status = " + esc(entity::speed_status::CLAIMED)
                    + " AND claimed_by = " + esc(taskId)
                    + " AND (lease_expires_at IS NULL OR lease_expires_at > NOW(3))");
                if (affected != 1) {
                    dm.rollback(); // 租约已过期或已被他人取得，拒绝迟到的结果
                    return false;
                }
                history_.append(id, batchNo,
                                entity::speed_claim_action::CONSUME, taskId, reason);
                dm.commit();
                LOG_INFO("SpeedDAO", "Speed id=" + itos(id) + " consumed by " + taskId);
                return true;
            } catch (const std::exception&) {
                dm.rollback();
                throw;
            }
        }

        // 单条记录的历次领用/释放/消费历史
        std::vector<entity::SpeedClaimHistory> historyOf(int speedId) {
            return history_.findBySpeedId(speedId);
        }

    private:
        SpeedClaimHistoryDAO history_;

        entity::Speed mapRow(const db::Row& row) {
            entity::Speed s;
            s.id = getInt(row, "id");
            s.value = getFloat(row, "value");
            s.date = getVal(row, "date");
            s.flag = getInt(row, "flag");
            s.batchNo = getVal(row, "batch_no");
            s.sampledAt = getVal(row, "sampled_at");
            s.status = getVal(row, "status");
            if (s.status.empty()) s.status = entity::speed_status::AVAILABLE;
            s.claimedBy = getVal(row, "claimed_by");
            s.claimedAt = getVal(row, "claimed_at");
            s.leaseExpiresAt = getVal(row, "lease_expires_at");
            s.consumedAt = getVal(row, "consumed_at");
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
    };

} // namespace dao
