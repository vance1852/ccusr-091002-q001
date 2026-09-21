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
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <algorithm>
#include <functional>
#include <ctime>

#include "config/AppConfig.h"
#include "utils/Logger.h"
#include "db/DatabaseManager.h"
#include "dao/SpeedDAO.h"
#include "dao/SpeedBatchDAO.h"
#include "dao/SpeedClaimHistoryDAO.h"
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
// SPEED 一次性领用 / 可追溯流程演示
//   - 两个领取者同时竞争同一条记录，只有一个成功
//   - 任务取消释放、租约超时回收均可重新发放
//   - 确认消费为终态，连接重建后也不倒退
//   - 每条记录历次领取/释放/消费原因均可追溯
// ============================================
static std::string fmtTime(std::time_t t) {
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    return buf;
}

static void printHistory(dao::SpeedDAO& speedDao, int speedId) {
    auto hist = speedDao.historyOf(speedId);
    cout << "    记录 #" << speedId << " 共 " << hist.size() << " 条状态历史：" << endl;
    for (auto& h : hist) {
        cout << "      [" << h.createdAt << "] " << h.action
             << "  actor=" << h.actor
             << "  batch=" << (h.batchNo.empty() ? "-" : h.batchNo)
             << "  reason=" << (h.reason.empty() ? "-" : h.reason) << endl;
    }
}

static void demoSpeedTraceability(const config::DatabaseConfig& dbConfig) {
    printSeparator("SPEED 一次性领用 / 可追溯流程");

    using namespace entity;

    // 每条并发任务使用各自独立的连接，模拟不同检测进程
    auto connA = db::DatabaseManager::create(dbConfig);
    auto connB = db::DatabaseManager::create(dbConfig);

    // 准备一个有效的批次窗口：起止时间必须有效（end > start）
    std::srand(static_cast<unsigned>(std::time(nullptr)));
    std::time_t now = std::time(nullptr);
    std::string today = fmtTime(now).substr(0, 10);
    std::string batchNo = "B-" + today + "-" + std::to_string(now % 100000)
        + "-" + std::to_string(std::rand() % 100000);
    {
        dao::SpeedBatchDAO batchDao;
        entity::SpeedBatch batch;
        batch.batchNo = batchNo;
        batch.startTime = fmtTime(now - 60);
        batch.endTime = fmtTime(now + 3600);
        batch.remark = "一次性领用演示批次";
        long long bid = batchDao.create(batch);
        printResult("创建有效起止时间的批次 " + batchNo, bid > 0);

        // 无效窗口（结束早于开始）必须被拒绝
        entity::SpeedBatch bad;
        bad.batchNo = batchNo + "-BAD";
        bad.startTime = fmtTime(now + 100);
        bad.endTime = fmtTime(now);
        printResult("拒绝无效批次窗口(end <= start)", batchDao.create(bad) == 0);
    }

    auto makeSpeed = [&](float v) {
        entity::Speed s;
        s.value = v;
        s.date = today;                 // 保留原有按日期查询口径
        s.batchNo = batchNo;
        s.sampledAt = fmtTime(now);
        return s;
    };

    // ---------- 场景1：两个领取者同时竞争同一条记录 ----------
    int r1 = 0;
    {
        dao::SpeedDAO dao;
        r1 = dao.insertWithBatch(makeSpeed(101.5f));
        printResult("采样入库记录 #" + to_string(r1), r1 > 0);
    }

    struct ClaimOutcome { bool got = false; int id = -1; };
    ClaimOutcome oa, ob;
    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    auto racer = [&](std::string taskId, ClaimOutcome& out, db::DatabaseManager& conn) {
        dao::SpeedDAO dao(conn);
        ready.fetch_add(1);
        while (!go.load()) { std::this_thread::yield(); } // 起跑栅栏，尽量同时进入
        auto rec = dao.claim(batchNo, taskId, 120, "早班并发领用");
        if (rec) { out.got = true; out.id = rec->id; }
    };

    std::thread ta(racer, "task-A", std::ref(oa), std::ref(*connA));
    std::thread tb(racer, "task-B", std::ref(ob), std::ref(*connB));
    while (ready.load() < 2) std::this_thread::yield();
    go.store(true);
    ta.join(); tb.join();

    int winners = (oa.got ? 1 : 0) + (ob.got ? 1 : 0);
    cout << "  同时领用结果: task-A " << (oa.got ? "拿到 #" + to_string(oa.id) : "未拿到")
         << " / task-B " << (ob.got ? "拿到 #" + to_string(ob.id) : "未拿到") << endl;
    printResult("同一条记录两个并发领取者恰有一个成功",
                winners == 1 && (!oa.got || !ob.got) && oa.id != ob.id);

    // ---------- 场景2：任务取消后释放，可被另一任务重新领用 ----------
    std::string winner = oa.got ? "task-A" : "task-B";
    db::DatabaseManager& winnerConn = oa.got ? *connA : *connB;
    {
        dao::SpeedDAO dao(winnerConn);
        bool released = dao.release(r1, winner, "检测任务取消，归还未消费记录");
        printResult("持有者取消任务并释放记录 #" + to_string(r1), released);
    }
    {
        dao::SpeedDAO dao(*connA);
        auto again = dao.claim(batchNo, "task-A", 120, "取消后重新领用");
        printResult("释放后记录 #" + to_string(r1) + " 可被重新领用",
                    again.has_value() && again->id == r1);
    }

    // ---------- 场景3：租约超时，系统回收后重新发放 ----------
    int r2 = 0;
    {
        dao::SpeedDAO dao;
        r2 = dao.insertWithBatch(makeSpeed(202.5f));
    }
    {
        dao::SpeedDAO slow(*connB);
        auto rec = slow.claim(batchNo, "slow-task", 1, "领用后处理超时"); // 1秒租约
        printResult("slow-task 领用记录 #" + to_string(r2) + "（租约1秒）", rec.has_value());
    }
    std::this_thread::sleep_for(std::chrono::seconds(2));
    {
        dao::SpeedDAO dao;
        int n = dao.reclaimExpiredLeases();
        printResult("超时未消费记录被回收重新释放(#" + to_string(r2) + ")", n >= 1);
        auto fast = dao.claim(batchNo, "fast-task", 120, "超时回收后重新领用");
        printResult("回收后记录 #" + to_string(r2) + " 重新发放给 fast-task",
                    fast.has_value() && fast->id == r2);
    }

    // ---------- 场景4：确认消费为终态，数据库连接重建也不倒退 ----------
    {
        dao::SpeedDAO dao(*connA);
        bool ok = dao.consume(r2, "fast-task", "检测结果已确认");
        printResult("fast-task 确认消费记录 #" + to_string(r2), ok);
    }
    // 模拟数据库连接重建（关闭后按原配置重连）
    connA->reconnect();
    {
        dao::SpeedDAO dao(*connA);
        auto rec = dao.findById(r2);
        bool stillConsumed = rec && rec->status == speed_status::CONSUMED && rec->flag == 1;
        printResult("重连后记录 #" + to_string(r2) + " 仍为 CONSUMED 终态", stillConsumed);

        bool releaseDenied = !dao.release(r2, "fast-task", "重连后尝试回退");
        bool otherDenied   = !dao.consume(r2, "intruder-task", "重连后他人尝试确认");
        printResult("终态记录拒绝释放/他人确认（不可倒退）", releaseDenied && otherDenied);
    }

    // ---------- 场景5：多条记录并发抢领，全局不重复发放 ----------
    {
        dao::SpeedDAO dao;
        for (int i = 0; i < 20; ++i) dao.insertWithBatch(makeSpeed(300.0f + i));
    }
    std::vector<int> claimedIds;
    std::mutex mtx;
    std::atomic<int> ready2{0};
    std::atomic<bool> go2{false};
    auto drainer = [&](std::string taskId, db::DatabaseManager& conn) {
        dao::SpeedDAO dao(conn);
        ready2.fetch_add(1);
        while (!go2.load()) std::this_thread::yield();
        while (true) {
            auto rec = dao.claim(batchNo, taskId, 120, "并发抢领");
            if (!rec) break;
            std::lock_guard<std::mutex> lk(mtx);
            claimedIds.push_back(rec->id);
        }
    };
    std::thread g1(drainer, "task-A", std::ref(*connA));
    std::thread g2(drainer, "task-B", std::ref(*connB));
    while (ready2.load() < 2) std::this_thread::yield();
    go2.store(true);
    g1.join(); g2.join();

    std::vector<int> sortedIds = claimedIds;
    std::sort(sortedIds.begin(), sortedIds.end());
    bool noDup = std::adjacent_find(sortedIds.begin(), sortedIds.end()) == sortedIds.end();
    cout << "  20 条记录被两个任务并发抢领，共发出 " << claimedIds.size() << " 次" << endl;
    printResult("并发抢领 20 条记录无重复发放", claimedIds.size() == 20 && noDup);

    // ---------- 追溯：运维查询每条记录历次领取/释放/消费原因 ----------
    cout << "\n  --- 领用状态历史追溯 ---" << endl;
    {
        dao::SpeedDAO dao;
        printHistory(dao, r1);
        printHistory(dao, r2);

        // 原有按日期查询方式仍然可用
        auto byDate = dao.findByDate(today);
        cout << "\n  原有按日期查询 findByDate('" << today << "') 命中 "
             << byDate.size() << " 条" << endl;
    }
}
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

        // 5.1 SPEED 一次性领用 / 可追溯流程演示
        //     （两个并发领取者 + 一次连接重建，验证不重复发放、终态不倒退、历史可查）
        demoSpeedTraceability(dbConfig);

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
