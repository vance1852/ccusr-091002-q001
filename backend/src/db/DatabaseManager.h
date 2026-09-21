#pragma once

#ifdef _WIN32
#include <winsock2.h>
#endif
#include <mysql.h>

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <functional>
#include <type_traits>
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

        // 初始化连接
        void init(const config::DatabaseConfig& cfg);

        // 关闭连接
        void close();

        // 执行非查询 SQL (INSERT/UPDATE/DELETE)，返回受影响行数
        int execute(const std::string& sql);

        // 执行查询 SQL，返回结果集
        ResultSet query(const std::string& sql);

        // 执行 INSERT 并返回自增 ID
        long long insertAndGetId(const std::string& sql);

        // 转义字符串防 SQL 注入
        std::string escape(const std::string& str);

        // 检查连接是否存活
        bool isConnected() const;

        // 事务边界：开启事务
        void beginTransaction();

        // 事务边界：提交。提交后的结果持久化，连接重建也不会倒退
        void commit();

        // 事务边界：回滚。未提交的修改全部撤销
        void rollback();

        // 在事务中执行 fn：全程持有连接锁（单连接串行化，多线程调用安全），
        // fn 正常返回则自动提交，抛出异常则自动回滚。
        template <typename Fn>
        auto transaction(Fn&& fn) -> decltype(fn()) {
            std::lock_guard<std::recursive_mutex> lock(mtx_);
            beginTransaction();
            try {
                if constexpr (std::is_void<decltype(fn())>::value) {
                    fn();
                    commit();
                } else {
                    decltype(fn()) result = fn();
                    commit();
                    return result;
                }
            } catch (...) {
                try {
                    rollback();
                } catch (...) {
                    // 连接可能已断开；未提交的事务会由服务端在连接关闭时回滚
                }
                throw;
            }
        }

    private:
        DatabaseManager() = default;
        ~DatabaseManager();

        DatabaseManager(const DatabaseManager&) = delete;
        DatabaseManager& operator=(const DatabaseManager&) = delete;

        MYSQL* conn_ = nullptr;
        config::DatabaseConfig config_;
        bool connected_ = false;
        // 单连接互斥锁（递归）：事务边界内可多次进入 execute/query
        mutable std::recursive_mutex mtx_;

        void ensureConnected();
        void reconnect();
    };

} // namespace db
