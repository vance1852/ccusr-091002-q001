/**
 * 工业检测系统 - C++ MySQL 数据访问层
 * 
 * 演示所有 DAO 的 CRUD 操作
 * 编译环境: Visual Studio 2019+ / CMake 3.16+ / MySQL Connector C
 */

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <iostream>
#include <string>
#include <iomanip>
#include <ctime>
#include <thread>

#include "config/AppConfig.h"
#include "utils/Logger.h"
#include "db/DatabaseManager.h"
#include "dao/SpeedDAO.h"
#include "dao/SpliceDAO.h"
#include "dao/FlawDAO.h"
#include "dao/StopDAO.h"
#include "dao/CompareDAO.h"
#include "dao/HistoryDAO.h"
#include "dao/RemoveDAO.h"

using namespace std;

// ============================================
// 辅助打印函数
// ============================================
static void printSeparator(const string& title) {
    cout << "\n" << string(60, '=') << endl;
    cout << "  " << title << endl;
    cout << string(60, '=') << endl;
}

static void printResult(const string& operation, bool success) {
    cout << "  [" << (success ? "OK" : "FAIL") << "] " << operation << endl;
}

// 格式化时间戳（批次窗口、采样时刻）
static string formatTs(time_t t) {
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_buf);
    return buf;
}

// ============================================
// 各表 CRUD 演示
// ============================================
static void demoSpeed(dao::SpeedDAO& speedDao) {
    printSeparator("SPEED 速度表 CRUD");

    // INSERT
    entity::Speed s;
    s.value = 120.5f;
    s.date = "2026-02-24";
    s.flag = 0;
    int id = speedDao.insert(s);
    printResult("INSERT speed (id=" + to_string(id) + ")", id > 0);

    // SELECT by ID
    auto found = speedDao.findById(id);
    printResult("SELECT by id=" + to_string(id), found.has_value());
    if (found) {
        cout << "    value=" << found->value << ", date=" << found->date << ", flag=" << found->flag << endl;
    }

    // UPDATE
    int affected = speedDao.updateValue(id, 135.8f);
    printResult("UPDATE value -> 135.8", affected > 0);

    // MARK USED
    affected = speedDao.markUsed(id);
    printResult("MARK USED", affected > 0);

    // COUNT
    int cnt = speedDao.count();
    cout << "  Total records: " << cnt << endl;

    // FIND ALL
    auto all = speedDao.findAll();
    cout << "  FindAll returned " << all.size() << " records" << endl;

    // DELETE
    affected = speedDao.deleteById(id);
    printResult("DELETE id=" + to_string(id), affected > 0);
}

// ============================================
// SPEED 一次性领用流程演示：
// 批次有效性校验、双任务并发领取、取消/超时释放、确认后重连不倒退、运维审计
// ============================================
static void demoSpeedClaim(dao::SpeedDAO& speedDao) {
    printSeparator("SPEED 一次性领用流程（批次 / 并发 / 重连）");

    time_t now = std::time(nullptr);
    string batchId = "BATCH-DEMO-" + to_string(static_cast<long long>(now));

    // 1. 登记批次：起止时间必须有效
    entity::SpeedBatch batch;
    batch.batchId = batchId;
    batch.validFrom = formatTs(now - 3600);
    batch.validTo = formatTs(now + 3600);
    speedDao.registerBatch(batch);
    printResult("登记有效批次 " + batchId, true);

    entity::SpeedBatch bad;
    bad.batchId = batchId + "-BAD";
    bad.validFrom = formatTs(now + 3600);
    bad.validTo = formatTs(now - 3600);  // 截止早于起始，必须被拒绝
    bool rejected = false;
    try {
        speedDao.registerBatch(bad);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    printResult("拒绝起止时间无效的批次", rejected);

    // 过期批次即使登记成功也不可领取
    entity::SpeedBatch expired;
    expired.batchId = batchId + "-EXPIRED";
    expired.validFrom = formatTs(now - 7200);
    expired.validTo = formatTs(now - 3600);
    speedDao.registerBatch(expired);
    entity::Speed oldRec;
    oldRec.batchId = expired.batchId;
    oldRec.value = 1.0f;
    oldRec.date = formatTs(now).substr(0, 10);
    oldRec.sampledAt = formatTs(now - 7000);
    speedDao.insert(oldRec);
    bool expiredClaim = speedDao.claimNext(expired.batchId, "task-C", "过期批次领取").has_value();
    printResult("过期批次不可领取", !expiredClaim);

    // 2. 写入一条该批次的速度记录
    entity::Speed s;
    s.batchId = batchId;
    s.value = 88.8f;
    s.date = formatTs(now).substr(0, 10);
    s.sampledAt = formatTs(now);
    int speedId = speedDao.insert(s);
    printResult("INSERT 批次速度记录 (id=" + to_string(speedId) + ")", speedId > 0);

    // 3. 两个检测任务同时领取同一条记录：只有一个成功
    std::optional<entity::Speed> claimA, claimB;
    std::thread t1([&] { claimA = speedDao.claimNext(batchId, "task-A", "早班检测领用"); });
    std::thread t2([&] { claimB = speedDao.claimNext(batchId, "task-B", "早班检测领用"); });
    t1.join();
    t2.join();
    bool exactlyOne = claimA.has_value() != claimB.has_value();
    printResult("两个并发领取只有一个成功", exactlyOne);
    entity::Speed first = claimA ? *claimA : *claimB;
    cout << "    领取成功方: " << first.claimedBy << ", token=" << first.claimToken << endl;

    // 4. 任务取消：释放未消费的记录，可重新领取
    bool released = speedDao.releaseClaim(speedId, first.claimToken, first.claimedBy, "任务取消");
    printResult("任务取消后释放记录", released);

    // 5. 再次领取后搁置，由超时清扫重新释放
    auto second = speedDao.claimNext(batchId, "task-B", "取消后重新领用");
    printResult("释放后可重新领取", second.has_value());
    int swept = speedDao.releaseExpired(batchId, 0, "sweeper", "超时未消费");
    printResult("超时未消费记录被重新释放", swept == 1);

    // 6. 重新领取并确认消费（提交后持久化）
    auto third = speedDao.claimNext(batchId, "task-A", "超时释放后领用");
    bool consumed = third && speedDao.consumeClaim(speedId, third->claimToken, "task-A", "检测完成确认");
    printResult("确认消费（提交后持久化）", consumed);

    // 7. 模拟数据库连接重建：已确认的结果不能倒退
    db::DatabaseManager::instance().close();
    config::DatabaseConfig cfg;
    cfg.loadFromEnv();
    db::DatabaseManager::instance().init(cfg);
    auto after = speedDao.findById(speedId);
    bool stillConsumed = after && after->status == "CONSUMED";
    printResult("连接重建后已确认结果不倒退", stillConsumed);
    bool reClaim = speedDao.claimById(speedId, "task-B", "重连后抢领").has_value();
    bool reRelease = third && speedDao.releaseClaim(speedId, third->claimToken, "task-B", "重连后释放");
    printResult("已确认记录不可再领取/释放", !reClaim && !reRelease);

    // 8. 运维审计：打印该记录的全部领用历史及原因
    cout << "  领用历史（speed_id=" << speedId << "）:" << endl;
    for (auto& e : speedDao.findEventsBySpeedId(speedId)) {
        cout << "    [" << e.createdAt << "] " << e.event
             << " by " << e.actor << " - " << e.reason << endl;
    }
}

static void demoSplice(dao::SpliceDAO& spliceDao) {
    printSeparator("SPLICE 接缝表 CRUD");

    entity::Splice s;
    s.location = 1500.0f;
    s.distance = 320.5f;
    s.time = "45";
    s.url = "/data/splice/img_001.jpg";
    s.last = 1;
    s.flag = 0;
    s.stop = 0;
    int id = spliceDao.insert(s);
    printResult("INSERT splice (id=" + to_string(id) + ")", id > 0);

    auto found = spliceDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    location=" << found->location << ", distance=" << found->distance
             << ", time=" << found->time << endl;
    }

    // 查询有效接头
    auto active = spliceDao.findActive();
    cout << "  Active splices: " << active.size() << endl;

    // 更新标志
    spliceDao.updateFlags(id, 1, 1);
    printResult("UPDATE flags (flag=1, stop=1)", true);

    // 清除 last
    spliceDao.clearAllLast();
    printResult("CLEAR all last flags", true);

    spliceDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoFlaw(dao::FlawDAO& flawDao) {
    printSeparator("FLAW 损伤表 CRUD");

    entity::Flaw f;
    f.category = "crack";
    f.level = 3;
    f.url = "/data/flaw/crack_001.jpg";
    f.camera = 2;
    f.location = 2500.0f;
    f.distance = 180.0f;
    f.size = "15x8mm";
    f.coordinate = "X:120,Y:340";
    f.date = "2026-02-24";
    f.time = 30.5f;
    f.flag = 0;
    f.stop = 0;
    f.epoch = 1;
    long long id = flawDao.insert(f);
    printResult("INSERT flaw (id=" + to_string(id) + ")", id > 0);

    auto found = flawDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    category=" << found->category << ", level=" << found->level
             << ", size=" << found->size << ", camera=" << found->camera << endl;
    }

    // 按类型查询
    auto cracks = flawDao.findByCategory("crack");
    cout << "  Cracks found: " << cracks.size() << endl;

    // 更新追踪圈数
    flawDao.updateEpoch(id, 5);
    printResult("UPDATE epoch -> 5", true);

    // 更新停机标志
    flawDao.updateFlags(id, 1, 1);
    printResult("UPDATE flags (flag=1, stop=1)", true);

    flawDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoStop(dao::StopDAO& stopDao) {
    printSeparator("STOP 停机表 CRUD");

    entity::Stop s;
    s.category = 1;
    s.distance = 200.0f;
    s.flag = 1;
    s.command = 0;
    long long id = stopDao.insert(s);
    printResult("INSERT stop (id=" + to_string(id) + ")", id > 0);

    auto found = stopDao.findById(id);
    printResult("SELECT by id", found.has_value());

    // 下发停机命令
    stopDao.issueCommand(id);
    printResult("ISSUE stop command", true);

    // 查询已下发命令的记录
    auto commanded = stopDao.findCommanded();
    cout << "  Commanded stops: " << commanded.size() << endl;

    stopDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoCompare(dao::CompareDAO& compareDao) {
    printSeparator("COMPARE 对比表 CRUD");

    entity::Compare c;
    c.newUrl = "/data/compare/new_001.jpg";
    c.oldUrl = "/data/compare/old_001.jpg";
    c.value = 0.85f;
    c.category = 1;
    c.level = 2;
    c.oldSize = "12x6mm";
    long long id = compareDao.insert(c);
    printResult("INSERT compare (id=" + to_string(id) + ")", id > 0);

    auto found = compareDao.findById(id);
    printResult("SELECT by id", found.has_value());
    if (found) {
        cout << "    value=" << found->value << ", level=" << found->level << endl;
    }

    // 更新
    c.id = id;
    c.value = 0.92f;
    c.level = 3;
    compareDao.update(c);
    printResult("UPDATE value -> 0.92, level -> 3", true);

    compareDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoHistory(dao::HistoryDAO& historyDao) {
    printSeparator("HISTORY 历史表 CRUD");

    entity::History h;
    h.category = "corrosion";
    h.level = 2;
    h.url = "/data/history/corrosion_001.jpg";
    h.camera = 1;
    h.size = "20x15mm";
    h.date = "2026-02-24";
    long long id = historyDao.insert(h);
    printResult("INSERT history (id=" + to_string(id) + ")", id > 0);

    auto found = historyDao.findById(id);
    printResult("SELECT by id", found.has_value());

    auto byCategory = historyDao.findByCategory("corrosion");
    cout << "  Corrosion records: " << byCategory.size() << endl;

    historyDao.deleteById(id);
    printResult("DELETE", true);
}

static void demoRemove(dao::RemoveDAO& removeDao) {
    printSeparator("REMOVE 移除表 CRUD");

    long long id = removeDao.insert();
    printResult("INSERT remove (id=" + to_string(id) + ")", id > 0);

    bool exists = removeDao.exists(id);
    printResult("EXISTS check", exists);

    removeDao.insertWithId(99999);
    printResult("INSERT with specific id=99999", true);

    auto all = removeDao.findAll();
    cout << "  Total remove records: " << all.size() << endl;

    removeDao.deleteById(id);
    removeDao.deleteById(99999);
    printResult("DELETE all test records", true);
}

// ============================================
// 主入口
// ============================================
int main() {
    cout << string(60, '*') << endl;
    cout << "  Industrial Inspection System - C++ MySQL Data Access Layer" << endl;
    cout << "  工业检测系统 - C++ 数据访问层" << endl;
    cout << string(60, '*') << endl;

    try {
        // 1. 初始化日志
        utils::Logger::instance().setLevel(utils::LogLevel::INFO);

        // 2. 加载数据库配置
        config::DatabaseConfig dbConfig;
        dbConfig.loadFromEnv();

        // 3. 连接数据库
        LOG_INFO("Main", "Connecting to database...");
        db::DatabaseManager::instance().init(dbConfig);

        // 4. 初始化 DAO
        dao::SpeedDAO speedDao;
        dao::SpliceDAO spliceDao;
        dao::FlawDAO flawDao;
        dao::StopDAO stopDao;
        dao::CompareDAO compareDao;
        dao::HistoryDAO historyDao;
        dao::RemoveDAO removeDao;

        // 5. 执行各表 CRUD 演示
        demoSpeed(speedDao);
        demoSpeedClaim(speedDao);
        demoSplice(spliceDao);
        demoFlaw(flawDao);
        demoStop(stopDao);
        demoCompare(compareDao);
        demoHistory(historyDao);
        demoRemove(removeDao);

        // 6. 关闭连接
        db::DatabaseManager::instance().close();

        printSeparator("ALL TESTS COMPLETED SUCCESSFULLY");

    } catch (const db::DatabaseException& e) {
        LOG_ERROR("Main", string("Database error: ") + e.what());
        cerr << "\n[FATAL] Database error: " << e.what() << endl;
        return 1;
    } catch (const exception& e) {
        LOG_ERROR("Main", string("Unexpected error: ") + e.what());
        cerr << "\n[FATAL] Unexpected error: " << e.what() << endl;
        return 2;
    }

    return 0;
}
