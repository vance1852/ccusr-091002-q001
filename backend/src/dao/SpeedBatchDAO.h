#pragma once

#include "BaseDAO.h"
#include "../entity/SpeedBatch.h"
#include <vector>
#include <optional>

namespace dao {

    class SpeedBatchDAO : public BaseDAO {
    public:
        using BaseDAO::BaseDAO;

        // 创建批次。起止时间必须有效（非空且 end > start），否则拒绝并返回 0。
        // 返回自增ID；batchNo 唯一冲突时抛 DatabaseException。
        long long create(const entity::SpeedBatch& batch) {
            if (batch.batchNo.empty() || batch.startTime.empty() || batch.endTime.empty()) {
                LOG_ERROR("SpeedBatchDAO", "create rejected: batchNo/start/end must not be empty");
                return 0;
            }
            // 起止时间有效性：非空且 end > start。数据库还有 CHECK(end_time > start_time)
            // 兜底，这里提前按 DATETIME 解析比较，让无效窗口在进入领用事务前就被拦下；
            // 无法解析的时间串 STR_TO_DATE 返回 NULL，比较结果为 NULL，同样拒绝。
            auto rows = db().query(
                "SELECT CASE WHEN STR_TO_DATE(" + esc(batch.endTime)
                + ", '%Y-%m-%d %H:%i:%s') > STR_TO_DATE(" + esc(batch.startTime)
                + ", '%Y-%m-%d %H:%i:%s') THEN 1 ELSE 0 END AS ok");
            if (rows.empty() || getVal(rows[0], "ok") != "1") {
                LOG_ERROR("SpeedBatchDAO",
                          "create rejected: end_time must be later than start_time");
                return 0;
            }

            std::string sql = "INSERT INTO SPEED_BATCH (batch_no, start_time, end_time, status, remark) VALUES ("
                + esc(batch.batchNo) + ", "
                + esc(batch.startTime) + ", "
                + esc(batch.endTime) + ", "
                + itos(batch.status) + ", "
                + esc(batch.remark) + ")";
            LOG_INFO("SpeedBatchDAO", "Create speed batch " + batch.batchNo);
            return db().insertAndGetId(sql);
        }

        std::optional<entity::SpeedBatch> findByNo(const std::string& batchNo) {
            auto rows = db().query(
                "SELECT * FROM SPEED_BATCH WHERE batch_no = " + esc(batchNo) + " LIMIT 1");
            if (rows.empty()) return std::nullopt;
            return mapRow(rows[0]);
        }

        std::vector<entity::SpeedBatch> findAll() {
            return mapRows(db().query("SELECT * FROM SPEED_BATCH ORDER BY id DESC"));
        }

        // 关闭批次，关闭后不再允许向其新增/领用记录
        int close(const std::string& batchNo) {
            int affected = db().execute(
                "UPDATE SPEED_BATCH SET status = 1 WHERE batch_no = " + esc(batchNo)
                + " AND status = 0");
            LOG_INFO("SpeedBatchDAO", "Close speed batch " + batchNo);
            return affected;
        }

        // 批次是否处于开放领用窗口（存在、status=0 且当前时间落在窗口内）
        bool isOpen(const std::string& batchNo) {
            auto rows = db().query(
                "SELECT CASE WHEN status = 0 AND NOW(3) BETWEEN start_time AND end_time "
                "THEN 1 ELSE 0 END AS ok FROM SPEED_BATCH WHERE batch_no = "
                + esc(batchNo) + " LIMIT 1");
            return !rows.empty() && getVal(rows[0], "ok") == "1";
        }

    private:
        entity::SpeedBatch mapRow(const db::Row& row) {
            entity::SpeedBatch b;
            b.id = getLong(row, "id");
            b.batchNo = getVal(row, "batch_no");
            b.startTime = getVal(row, "start_time");
            b.endTime = getVal(row, "end_time");
            b.status = getInt(row, "status");
            b.remark = getVal(row, "remark");
            b.createdAt = getVal(row, "created_at");
            return b;
        }

        std::vector<entity::SpeedBatch> mapRows(const db::ResultSet& rows) {
            std::vector<entity::SpeedBatch> result;
            result.reserve(rows.size());
            for (auto& row : rows) result.push_back(mapRow(row));
            return result;
        }
    };

} // namespace dao
