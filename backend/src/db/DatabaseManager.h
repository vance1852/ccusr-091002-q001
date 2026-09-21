#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <functional>
#include "../config/AppConfig.h"
#include "../utils/Logger.h"

namespace db {

    // 查询结果行: 列名 -> 值
    using Row = std::map<std::string, std::string>;
    using ResultSet = std::vector<Row>;

    // 数据库异常
    class DatabaseException : public std::runtime_error {
    public:
        explicit DatabaseException(const std::string& msg) : std::runtime_error(msg) {}
    };

    class DatabaseManager {
    public:
        static DatabaseManager& instance() {
            static DatabaseManager inst;
            return inst;
        }

        // 创建并初始化一条独立连接（并发任务/演示用）。
        // 调用方负责持有返回对象；连接在析构时关闭。
        static std::unique_ptr<DatabaseManager> create(const config::DatabaseConfig& cfg) {
            std::unique_ptr<DatabaseManager> dm(new DatabaseManager());
            dm->init(cfg);
            return dm;
        }

        DatabaseManager() = default;
        ~DatabaseManager();

        // 初始化连接
        void init(const config::DatabaseConfig& cfg);

        // 关闭连接
        void close();

        // 主动断开并按原配置重建连接（用于演示/验证已提交状态在重连后仍然存在）
        void reconnect();

        // 执行非查询 SQL (INSERT/UPDATE/DELETE)，返回受影响行数
        int execute(const std::string& sql);

        // 执行查询 SQL，返回结果集
        ResultSet query(const std::string& sql);

        // 执行 INSERT 并返回自增 ID
        long long insertAndGetId(const std::string& sql);

        // —— 事务边界 ——
        // 领用/释放/消费等多语句操作必须包裹在事务中，
        // 借助 InnoDB 行锁保证并发竞争下同一条记录只能被一个领取者拿到。
        void beginTransaction();
        void commit();
        void rollback() noexcept;

        // 转义字符串防 SQL 注入
        std::string escape(const std::string& str);

        // 检查连接是否存活
        bool isConnected() const;

    private:
        DatabaseManager(const DatabaseManager&) = delete;
        DatabaseManager& operator=(const DatabaseManager&) = delete;

        MYSQL* conn_ = nullptr;
        config::DatabaseConfig config_;
        bool connected_ = false;
        bool inTransaction_ = false;

        void ensureConnected();
    };

} // namespace db
