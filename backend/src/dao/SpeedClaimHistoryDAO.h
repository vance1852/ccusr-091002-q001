#pragma once

#include "BaseDAO.h"
#include "../entity/SpeedClaimHistory.h"
#include <vector>

namespace dao {

    class SpeedClaimHistoryDAO : public BaseDAO {
    public:
        using BaseDAO::BaseDAO;

        // 追加一条状态历史（只增不改）。必须在调用方的事务内执行。
        long long append(int speedId, const std::string& batchNo,
                         const std::string& action, const std::string& actor,
                         const std::string& reason) {
            std::string sql =
                "INSERT INTO SPEED_CLAIM_HISTORY (speed_id, batch_no, action, actor, reason) VALUES ("
                + itos(speedId) + ", "
                + (batchNo.empty() ? "NULL" : esc(batchNo)) + ", "
                + esc(action) + ", "
                + esc(actor) + ", "
                + esc(reason) + ")";
            return db().insertAndGetId(sql);
        }

        // 单条记录的完整历史（运维可查清历次领取/释放/消费的原因）
        std::vector<entity::SpeedClaimHistory> findBySpeedId(int speedId) {
            return mapRows(db().query(
                "SELECT * FROM SPEED_CLAIM_HISTORY WHERE speed_id = " + itos(speedId)
                + " ORDER BY id ASC"));
        }

        // 按批次追溯
        std::vector<entity::SpeedClaimHistory> findByBatchNo(const std::string& batchNo) {
            return mapRows(db().query(
                "SELECT * FROM SPEED_CLAIM_HISTORY WHERE batch_no = " + esc(batchNo)
                + " ORDER BY id ASC"));
        }

        // 全量历史，按批次、记录排序
        std::vector<entity::SpeedClaimHistory> findAll() {
            return mapRows(db().query(
                "SELECT * FROM SPEED_CLAIM_HISTORY ORDER BY batch_no, speed_id, id ASC"));
        }

    private:
        entity::SpeedClaimHistory mapRow(const db::Row& row) {
            entity::SpeedClaimHistory h;
            h.id = getLong(row, "id");
            h.speedId = getInt(row, "speed_id");
            h.batchNo = getVal(row, "batch_no");
            h.action = getVal(row, "action");
            h.actor = getVal(row, "actor");
            h.reason = getVal(row, "reason");
            h.createdAt = getVal(row, "created_at");
            return h;
        }

        std::vector<entity::SpeedClaimHistory> mapRows(const db::ResultSet& rows) {
            std::vector<entity::SpeedClaimHistory> result;
            result.reserve(rows.size());
            for (auto& row : rows) result.push_back(mapRow(row));
            return result;
        }
    };

} // namespace dao
