#include "trace_mark.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <condition_variable>
#include <chrono>
#include <ctime>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ock {
namespace tracemark {
namespace {

constexpr uint32_t MAX_SERVICE_NUM = 64;
constexpr uint32_t MAX_INNER_ID_NUM = 1024;
constexpr uint64_t NS_PER_US = 1000;
constexpr uint16_t DEFAULT_IOPS_INTERVAL = 90;
constexpr int NAME_WIDTH = 50;
constexpr int DIGIT_WIDTH = 15;
constexpr int NUMBER_PRECISION = 3;
constexpr int TIME_WIDTH = 2;
constexpr mode_t DIR_MODE = 0750;
constexpr mode_t FILE_MODE = S_IRUSR | S_IWUSR | S_IRGRP;

uint32_t ServiceId(int32_t traceId)
{
    return (static_cast<uint32_t>(traceId) >> TRACE_MARK_SERVICE_SHIFT) & 0xFFFFU;
}

uint32_t InnerId(int32_t traceId)
{
    return static_cast<uint32_t>(traceId) & 0xFFFFU;
}

std::string CurrentTime()
{
    time_t rawTime = 0;
    time(&rawTime);
    struct tm timeInfo {};
    if (localtime_r(&rawTime, &timeInfo) == nullptr) {
        return "";
    }

    std::stringstream ss;
    ss << std::setfill('0') << std::setw(TIME_WIDTH) << std::right << timeInfo.tm_hour << ":"
       << std::setfill('0') << std::setw(TIME_WIDTH) << std::right << timeInfo.tm_min << ":"
       << std::setfill('0') << std::setw(TIME_WIDTH) << std::right << timeInfo.tm_sec;
    return ss.str();
}

std::string HeaderString()
{
    std::stringstream ss;
    ss << "TIME"
       << "\t\t" << std::left << std::setw(NAME_WIDTH) << "NAME"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "BEGIN"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "GOOD_END"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "BAD_END"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "ON_FLY"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "IOPS"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "MIN(us)"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "MAX(us)"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "AVG(us)"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "TPX(us)"
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << "TOTAL(us)";
    return ss.str();
}

void UpdateMin(std::atomic<uint64_t> &target, uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (value < current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void UpdateMax(std::atomic<uint64_t> &target, uint64_t value)
{
    uint64_t current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

std::string FormatTraceLine(const std::string &name, uint64_t begin, uint64_t goodEnd, uint64_t badEnd,
    uint64_t minLatency, uint64_t maxLatency, uint64_t totalLatency, double p99LatencyUs, uint16_t interval)
{
    const uint64_t finished = goodEnd + badEnd;
    const uint64_t onFly = begin > finished ? begin - finished : 0;
    const double intervalSeconds = interval == 0 ? DEFAULT_IOPS_INTERVAL : interval;

    std::ostringstream os;
    os.flags(std::ios::fixed);
    os.precision(NUMBER_PRECISION);
    os << std::left << std::setw(NAME_WIDTH) << name << "\t" << std::left << std::setw(DIGIT_WIDTH) << begin
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << goodEnd
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << badEnd
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << onFly
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << (static_cast<double>(begin) / intervalSeconds)
       << "\t" << std::left << std::setw(DIGIT_WIDTH)
       << (minLatency == UINT64_MAX ? 0.0 : static_cast<double>(minLatency) / NS_PER_US)
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << static_cast<double>(maxLatency) / NS_PER_US
       << "\t" << std::left << std::setw(DIGIT_WIDTH)
       << (goodEnd == 0 ? 0.0 : static_cast<double>(totalLatency) / static_cast<double>(goodEnd) / NS_PER_US)
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << p99LatencyUs
       << "\t" << std::left << std::setw(DIGIT_WIDTH) << static_cast<double>(totalLatency) / NS_PER_US;
    return os.str();
}

bool CreateParentDirs(const std::string &filePath)
{
    size_t slash = filePath.find_last_of('/');
    if (slash == std::string::npos) {
        return true;
    }

    std::string dir = filePath.substr(0, slash);
    if (dir.empty()) {
        dir = "/";
    }

    std::string current = (!dir.empty() && dir[0] == '/') ? "/" : "";
    size_t start = (!dir.empty() && dir[0] == '/') ? 1 : 0;
    while (start < dir.size()) {
        size_t end = dir.find('/', start);
        std::string part = dir.substr(start, end == std::string::npos ? std::string::npos : end - start);
        start = end == std::string::npos ? dir.size() : end + 1;
        if (part.empty()) {
            continue;
        }
        if (!current.empty() && current.back() != '/') {
            current += "/";
        }
        current += part;
        if (access(current.c_str(), F_OK) != 0 && mkdir(current.c_str(), DIR_MODE) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

struct TracePoint {
    void Begin(const char *traceName)
    {
        SetName(traceName);
        begin.fetch_add(1, std::memory_order_relaxed);
        intervalBegin.fetch_add(1, std::memory_order_relaxed);
    }

    void End(uint64_t latencyNs, int32_t retCode)
    {
        if (retCode != 0) {
            badEnd.fetch_add(1, std::memory_order_relaxed);
            intervalBadEnd.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        UpdateMin(minLatency, latencyNs);
        UpdateMax(maxLatency, latencyNs);
        UpdateMin(intervalMinLatency, latencyNs);
        UpdateMax(intervalMaxLatency, latencyNs);
        totalLatency.fetch_add(latencyNs, std::memory_order_relaxed);
        intervalTotalLatency.fetch_add(latencyNs, std::memory_order_relaxed);
        goodEnd.fetch_add(1, std::memory_order_relaxed);
        intervalGoodEnd.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(samplesMutex);
            samples.emplace_back(latencyNs);
            intervalSamples.emplace_back(latencyNs);
        }
    }

    void Clear()
    {
        begin.store(0, std::memory_order_relaxed);
        goodEnd.store(0, std::memory_order_relaxed);
        badEnd.store(0, std::memory_order_relaxed);
        totalLatency.store(0, std::memory_order_relaxed);
        minLatency.store(UINT64_MAX, std::memory_order_relaxed);
        maxLatency.store(0, std::memory_order_relaxed);
        ResetInterval();
        std::lock_guard<std::mutex> lock(samplesMutex);
        samples.clear();
        intervalSamples.clear();
    }

    bool IsActive() const
    {
        return active.load(std::memory_order_acquire);
    }

    std::string TotalLine(uint16_t interval) const
    {
        return FormatTraceLine(GetName(), begin.load(std::memory_order_relaxed),
            goodEnd.load(std::memory_order_relaxed), badEnd.load(std::memory_order_relaxed),
            minLatency.load(std::memory_order_relaxed), maxLatency.load(std::memory_order_relaxed),
            totalLatency.load(std::memory_order_relaxed), PercentileUs(false), interval);
    }

    std::string IntervalLine(uint16_t interval)
    {
        const uint64_t beginValue = intervalBegin.exchange(0, std::memory_order_relaxed);
        const uint64_t goodValue = intervalGoodEnd.exchange(0, std::memory_order_relaxed);
        const uint64_t badValue = intervalBadEnd.exchange(0, std::memory_order_relaxed);
        const uint64_t totalValue = intervalTotalLatency.exchange(0, std::memory_order_relaxed);
        const uint64_t minValue = intervalMinLatency.exchange(UINT64_MAX, std::memory_order_relaxed);
        const uint64_t maxValue = intervalMaxLatency.exchange(0, std::memory_order_relaxed);
        return FormatTraceLine(GetName(), beginValue, goodValue, badValue, minValue, maxValue, totalValue,
            PercentileUs(true), interval);
    }

private:
    void SetName(const char *traceName)
    {
        if (active.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(nameMutex);
        if (!active.load(std::memory_order_relaxed)) {
            name = traceName == nullptr ? "" : traceName;
            active.store(true, std::memory_order_release);
        }
    }

    std::string GetName() const
    {
        std::lock_guard<std::mutex> lock(nameMutex);
        return name;
    }

    void ResetInterval()
    {
        intervalBegin.store(0, std::memory_order_relaxed);
        intervalGoodEnd.store(0, std::memory_order_relaxed);
        intervalBadEnd.store(0, std::memory_order_relaxed);
        intervalTotalLatency.store(0, std::memory_order_relaxed);
        intervalMinLatency.store(UINT64_MAX, std::memory_order_relaxed);
        intervalMaxLatency.store(0, std::memory_order_relaxed);
    }

    double PercentileUs(bool intervalOnly) const
    {
        std::vector<uint64_t> copied;
        {
            std::lock_guard<std::mutex> lock(samplesMutex);
            copied = intervalOnly ? intervalSamples : samples;
            if (intervalOnly) {
                intervalSamples.clear();
            }
        }
        if (copied.empty()) {
            return 0.0;
        }

        std::sort(copied.begin(), copied.end());
        size_t index = static_cast<size_t>(std::ceil(0.99 * copied.size()));
        if (index == 0) {
            index = 1;
        }
        if (index > copied.size()) {
            index = copied.size();
        }
        return static_cast<double>(copied[index - 1]) / NS_PER_US;
    }

private:
    mutable std::mutex nameMutex;
    std::string name;
    std::atomic<bool> active{ false };
    std::atomic<uint64_t> begin{ 0 };
    std::atomic<uint64_t> goodEnd{ 0 };
    std::atomic<uint64_t> badEnd{ 0 };
    std::atomic<uint64_t> totalLatency{ 0 };
    std::atomic<uint64_t> minLatency{ UINT64_MAX };
    std::atomic<uint64_t> maxLatency{ 0 };
    std::atomic<uint64_t> intervalBegin{ 0 };
    std::atomic<uint64_t> intervalGoodEnd{ 0 };
    std::atomic<uint64_t> intervalBadEnd{ 0 };
    std::atomic<uint64_t> intervalTotalLatency{ 0 };
    std::atomic<uint64_t> intervalMinLatency{ UINT64_MAX };
    std::atomic<uint64_t> intervalMaxLatency{ 0 };
    mutable std::mutex samplesMutex;
    mutable std::vector<uint64_t> samples;
    mutable std::vector<uint64_t> intervalSamples;
};

class TraceStore {
public:
    static TraceStore &Instance()
    {
        static TraceStore store;
        return store;
    }

    void Init()
    {
        EnsurePoints();
    }

    TracePoint *Point(int32_t traceId)
    {
        EnsurePoints();
        const uint32_t serviceId = ServiceId(traceId);
        const uint32_t innerId = InnerId(traceId);
        if (serviceId >= MAX_SERVICE_NUM || innerId >= MAX_INNER_ID_NUM) {
            return nullptr;
        }
        return &points[serviceId * MAX_INNER_ID_NUM + innerId];
    }

    void ConfigureDump(const std::string &filePath, uint16_t interval)
    {
        StopDump();
        if (filePath.empty() || interval == 0) {
            return;
        }
        if (!CreateParentDirs(filePath)) {
            return;
        }
        int fd = open(filePath.c_str(), O_CREAT | O_TRUNC, FILE_MODE);
        if (fd < 0) {
            return;
        }
        close(fd);
        {
            std::lock_guard<std::mutex> lock(dumpMutex);
            dumpFile = filePath;
            dumpInterval = interval;
            dumpRunning = true;
        }
        dumpThread = std::thread(&TraceStore::DumpLoop, this);
    }

    std::string TraceInfo()
    {
        return BuildTraceInfo(false);
    }

    void Clear()
    {
        EnsurePoints();
        for (uint32_t i = 0; i < MAX_SERVICE_NUM * MAX_INNER_ID_NUM; ++i) {
            if (points[i].IsActive()) {
                points[i].Clear();
            }
        }
    }

    inline void SetEnabled(bool isEnable)
    {
        enabled.store(isEnable);
    }

    inline bool Enabled() const
    {
        return enabled.load();
    }

    ~TraceStore()
    {
        StopDump();
        delete[] points;
    }

private:
    TraceStore() = default;
    TraceStore(const TraceStore &) = delete;
    TraceStore &operator = (const TraceStore &) = delete;

    void EnsurePoints()
    {
        if (points != nullptr) {
            return;
        }
        std::lock_guard<std::mutex> lock(initMutex);
        if (points == nullptr) {
            points = new TracePoint[MAX_SERVICE_NUM * MAX_INNER_ID_NUM];
        }
    }

    std::string BuildTraceInfo(bool intervalOnly)
    {
        EnsurePoints();
        const uint16_t interval = intervalOnly ? DumpInterval() : DEFAULT_IOPS_INTERVAL;
        std::stringstream ss;
        ss << HeaderString() << std::endl;
        uint32_t lineCount = 0;
        for (uint32_t i = 0; i < MAX_SERVICE_NUM * MAX_INNER_ID_NUM; ++i) {
            if (!points[i].IsActive()) {
                continue;
            }
            ss << CurrentTime() << "\t";
            ss << (intervalOnly ? points[i].IntervalLine(interval) : points[i].TotalLine(interval)) << std::endl;
            ++lineCount;
        }
        if (lineCount > 0) {
            ss << std::endl;
        }
        return ss.str();
    }

    uint16_t DumpInterval() const
    {
        std::lock_guard<std::mutex> lock(dumpMutex);
        return dumpInterval == 0 ? DEFAULT_IOPS_INTERVAL : dumpInterval;
    }

    void DumpLoop()
    {
        std::unique_lock<std::mutex> lock(dumpMutex);
        while (dumpRunning) {
            const uint16_t interval = dumpInterval;
            dumpCond.wait_for(lock, std::chrono::seconds(interval));
            if (!dumpRunning) {
                break;
            }
            const std::string filePath = dumpFile;
            lock.unlock();
            AppendDump(filePath, BuildTraceInfo(true));
            lock.lock();
        }
    }

    void AppendDump(const std::string &filePath, const std::string &content)
    {
        if (filePath.empty() || content.empty()) {
            return;
        }
        std::ofstream out(filePath, std::ios::out | std::ios::app);
        if (!out.is_open()) {
            return;
        }
        out << content;
    }

    void StopDump()
    {
        {
            std::lock_guard<std::mutex> lock(dumpMutex);
            if (!dumpRunning) {
                return;
            }
            dumpRunning = false;
            dumpCond.notify_all();
        }
        if (dumpThread.joinable()) {
            dumpThread.join();
        }
    }

private:
    std::mutex initMutex;
    TracePoint *points{ nullptr };
    std::atomic<bool> enabled{false};
    mutable std::mutex dumpMutex;
    std::condition_variable dumpCond;
    std::thread dumpThread;
    std::string dumpFile;
    uint16_t dumpInterval{ DEFAULT_IOPS_INTERVAL };
    bool dumpRunning{ false };
};

} // namespace

void TraceMark::Init()
{
    TraceStore::Instance().Init();
}

void TraceMark::SetDumpFile(std::string &dumpFile, uint16_t interval)
{
    TraceStore::Instance().ConfigureDump(dumpFile, interval);
}

std::string TraceMark::GetTraceInfo()
{
    return TraceStore::Instance().TraceInfo();
}

void TraceMark::ClearTrace()
{
    TraceStore::Instance().Clear();
}

void TraceMark::SetEnable(bool isEnable)
{
    TraceStore::Instance().SetEnabled(isEnable);
}

bool TraceMark::IsEnable()
{
    return TraceStore::Instance().Enabled();
}

uint64_t TraceMark::NowNs()
{
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

void TraceMark::MarkBegin(int32_t traceId, const char *traceName)
{
    TracePoint *point = TraceStore::Instance().Point(traceId);
    if (point == nullptr) {
        return;
    }
    point->Begin(traceName);
}

void TraceMark::MarkEnd(int32_t traceId, uint64_t latencyNs, int32_t retCode)
{
    TracePoint *point = TraceStore::Instance().Point(traceId);
    if (point == nullptr) {
        return;
    }
    point->End(latencyNs, retCode);
}

}
}
