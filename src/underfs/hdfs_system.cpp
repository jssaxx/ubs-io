/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2024. All rights reserved.
 */

#include "hdfs_system.h"
#include <cstring>
#include "bio_err.h"
#include "bio_log.h"
#include "bio_config_instance.h"
#include "bio_tracepoint_helper.h"
#include "ceptor_env.h"
#include "underfs_config.h"

using namespace ock::interceptor;

namespace ock {
namespace bio {
constexpr uint32_t UFS_KEY_MAX_SIZE = 256;
constexpr char FILE = 'F';
constexpr char DIRECTORY = 'D';
constexpr int32_t HDFS_ERROR = -1;
const size_t MAX_FILENAME_LENGTH = 255;

inline static bool KeyValid(const char *key)
{
    if (UNLIKELY(key == nullptr || strlen(key) == 0 || strlen(key) >= UFS_KEY_MAX_SIZE)) {
        return false;
    }
    return true;
}

BResult HdfsSystem::Init()
{
    if (mInited) {
        return BIO_OK;
    }

    if (LoadHdfsLibrary() != BIO_OK) {
        LOG_ERROR("Failed to load hdfs library.");
        return BIO_UFS_IOERR;
    }
    if (LoadHdfsConfig() != BIO_OK) {
        LOG_ERROR("Failed to load hdfs config.");
        return BIO_UFS_IOERR;
    }
    LVOS_TP_START(UNDERFS_HDFS_CONNECT_FAIL, &mHdfsFs, nullptr);
    hdfsBuilder *builder = newBuilderOp();
    builderSetNameNodeOp(builder, mNameNodeIp.c_str());
    builderSetNameNodePortOp(builder, mNameNodePort);
    mHdfsFs = builderConnectOp(builder);
    LVOS_TP_END;
    if (!mHdfsFs) {
        LOG_ERROR("Failed to connect to hdfs, ip:" << mNameNodeIp << ", port:" << mNameNodePort << ".");
        return BIO_UFS_IOERR;
    }

    int ret = BIO_OK;
    if (existsOp(mHdfsFs, mHdfsWorkingPath.c_str()) != BIO_OK) {
        ret = createDirOp(mHdfsFs, mHdfsWorkingPath.c_str());
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to create working directory, directory:" << mHdfsWorkingPath << ".");
            disconnectOp(mHdfsFs);
            return BIO_UFS_IOERR;
        }
    }

    ret = setWorkingDirOp(mHdfsFs, mHdfsWorkingPath.c_str());
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to set working directory, directory:" << mHdfsWorkingPath << ".");
        disconnectOp(mHdfsFs);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("UnderFS initialize succeed, ip:" << mNameNodeIp << ", port:" << mNameNodePort << ", workingPath:" <<
        mHdfsWorkingPath << ".");
    mInited = true;
    return BIO_OK;
}

void HdfsSystem::Stop()
{
    if (mHdfsFs) {
        disconnectOp(mHdfsFs);
    }
    mInited = false;
}

BResult ParsePath(const char *path, char *parentDir, size_t parentDirSize)
{
    const char *lastSlash = strrchr(path, '/');
    if (lastSlash != nullptr) {
        size_t parentLen = lastSlash - path;
        BResult ret = strncpy_s(parentDir, parentDirSize, path, parentLen);
        if (ret != BIO_OK) {
            LOG_ERROR("strncpy_s faild, ret:"<< ret << ".");
            return BIO_UFS_IOERR;
        }
        parentDir[parentLen] = '\0';
    } else {
        strcpy_s(parentDir, parentDirSize, "");
    }
    return BIO_OK;
}

BResult HdfsSystem::CreateDirectory(const std::string path)
{
    if ((path.empty() || existsOp(mHdfsFs, path.c_str()) == BIO_OK)) {
        return BIO_OK;
    }

    std::string subPath;
    for (auto &c : path) { // c represents each character in the path string
        subPath += c;
        if (c == '/') {
            auto ret = createDirOp(mHdfsFs, subPath.c_str());
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to create directory, directory:" << subPath << ".");
                return BIO_UFS_IOERR;
            }
        }
    }

    auto ret = createDirOp(mHdfsFs, subPath.c_str());
    if (ret != BIO_OK) {
        LOG_ERROR("Failed to create directory, directory:" << subPath << ".");
        return BIO_UFS_IOERR;
    }

    return BIO_OK;
}

BResult HdfsSystem::Put(const char *key, const char *value, const size_t len)
{
    if (UNLIKELY(!KeyValid(key) || value == nullptr)) {
        LOG_ERROR("Invalid put parameter, key or value pointers is nullptr, key:" << key << ", length:" << len << ".");
        return BIO_UFS_IOERR;
    }
    if (len == 0) {
        LOG_ERROR("Invalid put parameter, length is 0, key:" << key << ", length:" << len << ".");
        return BIO_UFS_IOERR;
    }

    char parentDir[UFS_KEY_MAX_SIZE];
    if (ParsePath(key, parentDir, sizeof(parentDir)) != BIO_OK) {
        LOG_ERROR("parse path failed, path:" << key << ".");
        return BIO_UFS_IOERR;
    }
    if (strlen(parentDir) != 0 && (existsOp(mHdfsFs, parentDir) != BIO_OK)) {
        int ret = CreateDirectory(parentDir);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to create directory, directory:" << parentDir << ".");
            return BIO_UFS_IOERR;
        }
    }

    hdfsStreamBuilder *builder;
    LVOS_TP_START(UNDERFS_SET_BUILDER_NULL, &builder, nullptr);
    builder = streamBuilderAllocOp(mHdfsFs, key, O_WRONLY | O_CREAT);
    LVOS_TP_END;
    if (!builder) {
        LOG_ERROR("Failed to allocate hdfs file builder, file:" << key << ".");
        return BIO_UFS_IOERR;
    }

    hdfsFile file = streamBuilderBuildOp(builder);
    if (!file) {
        LOG_ERROR("Failed to open file, file:" << key << ".");
        return BIO_UFS_IOERR;
    }

    int32_t ret = writeOp(mHdfsFs, file, value, len);
    if (ret == HDFS_ERROR) {
        LOG_ERROR("Failed to write file, file:" << key << ".");
        closeFileOp(mHdfsFs, file);
        return BIO_UFS_IOERR;
    }

    if (flushOp(mHdfsFs, file) != BIO_OK) {
        LOG_WARN("Failed to flush data to hdfs"
            << ".");
    }

    LOG_INFO("Put data success, key:" << key << ", len:" << len << ".");
    closeFileOp(mHdfsFs, file);
    return BIO_OK;
}

BResult HdfsSystem::Get(const char *key, char *value, const size_t len, const uint64_t off)
{
    if (UNLIKELY(!KeyValid(key) || value == nullptr)) {
        LOG_ERROR("Invalid get parameter, key or value pointers is nullptr, key:" << key << ", length:" << len <<
            ", offset:" << off << ".");
        return BIO_UFS_IOERR;
    }
    if (len == 0) {
        LOG_ERROR("Invalid get parameter, length is 0, key:" << key << ", length:" << len << ", offset:" << off << ".");
        return BIO_UFS_IOERR;
    }

    hdfsStreamBuilder *builder;
    LVOS_TP_START(UNDERFS_SET_BUILDER_NULL, &builder, nullptr);
    builder = streamBuilderAllocOp(mHdfsFs, key, O_RDONLY);
    LVOS_TP_END;
    if (!builder) {
        LOG_ERROR("Failed to allocate hdfs file builder, file:" << key << ".");
        return BIO_UFS_IOERR;
    }

    hdfsFile file = streamBuilderBuildOp(builder);
    if (!file) {
        LOG_ERROR("Failed to open file, file:" << key << ".");
        return BIO_NOT_EXISTS;
    }

    int ret = preadOp(mHdfsFs, file, off, value, len);
    if (ret == HDFS_ERROR) {
        LOG_ERROR("Failed to read file, file:" << key << ", length:" << len << ", offset:" << off << ".");
        closeFileOp(mHdfsFs, file);
        return BIO_UFS_IOERR;
    }

    LOG_INFO("Get data success, key:" << key << ", length:" << len << ", offset:" << off << ".");
    closeFileOp(mHdfsFs, file);
    return BIO_OK;
}

bool HdfsSystem::IsEmptyDirectorie(const std::string &path)
{
    int numEntries = 0;
    hdfsFileInfo *files = listDirectoryOp(mHdfsFs, path.c_str(), &numEntries);
    if (numEntries == 0) {
        return true;
    }
    freeFileInfoOp(files, numEntries);
    return false;
}

std::string GetParentDirectory(const std::string path)
{
    if (path.empty()) {
        return "";
    }

    size_t lastSlashPos = path.find_last_of("/");
    size_t startPos = 0;
    if (lastSlashPos == std::string::npos) {
        return "";
    } else {
        return path.substr(startPos, lastSlashPos);
    }
}

void HdfsSystem::DeleteEmptyDirImpl(const std::string &path)
{
    constexpr int isRecursive = 1;
    auto ret = deleteOp(mHdfsFs, path.c_str(), isRecursive);
    if (ret != BIO_OK) {
        LOG_WARN("Failed to delete empty directorie: " << path << ".");
    }
}

void HdfsSystem::DeleteEmptyDirectories(const std::string &path)
{
    if (path.empty() || (existsOp(mHdfsFs, path.c_str()) != BIO_OK)) {
        return;
    }
    if (IsEmptyDirectorie(path)) {
        DeleteEmptyDirImpl(path);
        DeleteEmptyDirectories(GetParentDirectory(path));
    } else {
        return;
    }
}

BResult HdfsSystem::Delete(const char *key)
{
    if (UNLIKELY(!KeyValid(key))) {
        LOG_ERROR("Invalid key, key:" << key << ".");
        return BIO_UFS_IOERR;
    }

    constexpr int isRecursive = 1;
    int ret = deleteOp(mHdfsFs, key, isRecursive);
    if (ret != BIO_OK) {
        if (existsOp(mHdfsFs, key) != BIO_OK) {
            LOG_WARN("Failed to check file, not exist, " << key << ".");
            return BIO_NOT_EXISTS;
        } else {
            LOG_ERROR("Failed to delete file, file:" << key << ".");
            return BIO_UFS_IOERR;
        }
    }

    DeleteEmptyDirectories(GetParentDirectory(key));
    LOG_INFO("Delete data success, key:" << key << ".");
    return BIO_OK;
}

BResult HdfsSystem::Stat(const char *key, ObjStat &objStat)
{
    if (UNLIKELY(!KeyValid(key))) {
        LOG_ERROR("Invalid key, key:" << key << ".");
        return BIO_UFS_IOERR;
    }

    hdfsFileInfo *fileInfo = getPathInfoOp(mHdfsFs, key);
    if (!fileInfo) {
        LOG_WARN("Failed to check file, not exist, " << key << ".");
        return BIO_NOT_EXISTS;
    }

    if (fileInfo->mSize < 0) {
        LOG_ERROR("invalid file size: " << fileInfo->mSize << ".");
        return BIO_NOT_EXISTS;
    }
    objStat.size = static_cast<uint64_t>(fileInfo->mSize);
    objStat.time = fileInfo->mLastMod;

    constexpr int fileInfoCount = 1;
    freeFileInfoOp(fileInfo, fileInfoCount);
    LOG_INFO("Stat success, key:" << key << ".");
    return BIO_OK;
}

std::string GetFileNameFromHdfsPath(const std::string path)
{
    if (path.empty()) {
        LOG_ERROR("Invalid path, path:" << path << ".");
        return path;
    }

    size_t pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

BResult HdfsSystem::ListDirectoryRecursive(const char *path,
    std::unordered_map<std::string, HdfsSystem::ObjStat> &objStat)
{
    if (UNLIKELY(!KeyValid(path))) {
        LOG_ERROR("Invalid path, path:" << path << ".");
        return BIO_UFS_IOERR;
    }

    std::string curPath = path;
    if (curPath.back() == '/') {
        curPath.pop_back();
    }

    int numEntries = 0;
    errno = BIO_OK;
    hdfsFileInfo *files = listDirectoryOp(mHdfsFs, curPath.c_str(), &numEntries);
    if (!files && (errno != BIO_OK)) {
        LOG_ERROR("Failed to list directory, directory:" << curPath << ".");
        return BIO_UFS_IOERR;
    }

    if (!files) {
        return BIO_OK;
    }

    std::string fileName{};
    BResult ret = BIO_OK;
    for (int i = 0; i < numEntries; ++i) {
        fileName = curPath + '/' + GetFileNameFromHdfsPath(files[i].mName);
        if (files[i].mKind == FILE) {
            objStat.insert({ fileName, { static_cast<uint64_t>(files[i].mSize), files[i].mLastMod } });
        } else {
            ret = ListDirectoryRecursive(fileName.c_str(), objStat);
            if (ret != BIO_OK) {
                LOG_ERROR("Failed to list directory, directory:" << fileName << ".");
                break;
            }
        }
    }

    freeFileInfoOp(files, numEntries);
    return ret;
}

BResult HdfsSystem::ListFilesInDirectory(const char *prefix,
    std::unordered_map<std::string, HdfsSystem::ObjStat> &objStat)
{
    if (UNLIKELY(!KeyValid(prefix))) {
        LOG_ERROR("Invalid path, path:" << prefix << ".");
        return BIO_UFS_IOERR;
    }

    int numEntries = 0;
    errno = BIO_OK;
    hdfsFileInfo *files = listDirectoryOp(mHdfsFs, mHdfsWorkingPath.c_str(), &numEntries);
    if (!files && (errno != BIO_OK)) {
        LOG_ERROR("Failed to list directory, directory:" << mHdfsWorkingPath << ".");
        return BIO_UFS_IOERR;
    }

    if (!files) {
        return BIO_OK;
    }

    size_t prefixLength = std::strlen(prefix);
    for (int i = 0; i < numEntries; ++i) {
        std::string fileName = GetFileNameFromHdfsPath(files[i].mName);
        constexpr size_t startPosition = 0;
        if ((files[i].mKind == DIRECTORY) || (fileName.size() < prefixLength) ||
            (fileName.compare(startPosition, prefixLength, prefix) != 0)) {
            continue;
        }
        objStat.insert({ fileName, { static_cast<uint64_t>(files[i].mSize), files[i].mLastMod } });
    }

    freeFileInfoOp(files, numEntries);
    return BIO_OK;
}

BResult HdfsSystem::List(const char *prefix, std::unordered_map<std::string, ObjStat> &objStat)
{
    BResult ret = BIO_OK;
    if (UNLIKELY(!KeyValid(prefix))) {
        LOG_ERROR("Invalid prefix, prefix:" << prefix << ".");
        return BIO_UFS_IOERR;
    }

    hdfsFileInfo *fileInfo = getPathInfoOp(mHdfsFs, prefix);
    if (fileInfo && fileInfo->mKind == DIRECTORY) {
        ret = ListDirectoryRecursive(prefix, objStat);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to list directory, directory:" << prefix << ".");
        }
        constexpr int fileInfoCount = 1;
        freeFileInfoOp(fileInfo, fileInfoCount);
    } else {
        ret = ListFilesInDirectory(prefix, objStat);
        if (ret != BIO_OK) {
            LOG_ERROR("Failed to list files, prefix:" << prefix << ".");
        }
    }

    LOG_INFO("List success, prefix:" << prefix << ".");
    return ret;
}

void *HdfsSystem::LoadFunction(const char *name)
{
    void *ptr = nullptr;
    ptr = dlsym(handler, name);
    if (ptr == nullptr) {
        LOG_ERROR("Failed to load function " << name << ".");
        return nullptr;
    }
    return ptr;
}

BResult HdfsSystem::InitOperations()
{
    if ((createDirOp = reinterpret_cast<CreateDirFuncPtr>(LoadFunction("hdfsCreateDirectory"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((setWorkingDirOp = reinterpret_cast<SetWorkingDirFuncPtr>(LoadFunction("hdfsSetWorkingDirectory"))) ==
        nullptr) {
        return BIO_INNER_ERR;
    }
    if ((disconnectOp = reinterpret_cast<DisconnectFuncPtr>(LoadFunction("hdfsDisconnect"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((streamBuilderAllocOp = reinterpret_cast<StreamBuilderAllocFuncPtr>(LoadFunction("hdfsStreamBuilderAlloc"))) ==
        nullptr) {
        return BIO_INNER_ERR;
    }
    if ((streamBuilderBuildOp = reinterpret_cast<StreamBuilderBuildFuncPtr>(LoadFunction("hdfsStreamBuilderBuild"))) ==
        nullptr) {
        return BIO_INNER_ERR;
    }
    if ((writeOp = reinterpret_cast<WriteFuncPtr>(LoadFunction("hdfsWrite"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((flushOp = reinterpret_cast<FlushFuncPtr>(LoadFunction("hdfsHFlush"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((closeFileOp = reinterpret_cast<CloseFileFuncPtr>(LoadFunction("hdfsCloseFile"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((preadOp = reinterpret_cast<PreadFuncPtr>(LoadFunction("hdfsPread"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((deleteOp = reinterpret_cast<DeleteFuncPtr>(LoadFunction("hdfsDelete"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((existsOp = reinterpret_cast<ExistsFuncPtr>(LoadFunction("hdfsExists"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((getPathInfoOp = reinterpret_cast<GetPathInfoFuncPtr>(LoadFunction("hdfsGetPathInfo"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((freeFileInfoOp = reinterpret_cast<FreeFileInfoFuncPtr>(LoadFunction("hdfsFreeFileInfo"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((listDirectoryOp = reinterpret_cast<ListDirectoryFuncPtr>(LoadFunction("hdfsListDirectory"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((newBuilderOp = reinterpret_cast<NewBuilderFuncPtr>(LoadFunction("hdfsNewBuilder"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((builderSetNameNodeOp = reinterpret_cast<BuilderSetNameNodeFuncPtr>(LoadFunction("hdfsBuilderSetNameNode"))) ==
        nullptr) {
        return BIO_INNER_ERR;
    }
    if ((builderSetNameNodePortOp =
             reinterpret_cast<BuilderSetNameNodePortFuncPtr>(LoadFunction("hdfsBuilderSetNameNodePort"))) == nullptr) {
        return BIO_INNER_ERR;
    }
    if ((builderConnectOp = reinterpret_cast<BuilderConnectFuncPtr>(LoadFunction("hdfsBuilderConnect"))) == nullptr) {
        return BIO_INNER_ERR;
    }

    return BIO_OK;
}

BResult HdfsSystem::LoadHdfsLibrary()
{
#ifdef DEBUG_UT
    return BIO_OK;
#endif
    
    std::string hadoopHome = env::GetEnv("HADOOP_HOME", "");
    if (hadoopHome.empty()) {
        LOG_ERROR("Failed to check HADOOP_HOME.");
        return BIO_UFS_IOERR;
    }

    std::string soFileName = hadoopHome + "/lib/native/libhdfs.so";
    char *canonicalPath = realpath(soFileName.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        LOG_ERROR("Failed to open library, not exist, " << soFileName << ".");
        return BIO_NOT_EXISTS;
    }

    handler = dlopen(canonicalPath, RTLD_NOW);
    free(canonicalPath);
    canonicalPath = nullptr;
    if (handler == nullptr) {
        LOG_ERROR("Failed to open library: " << soFileName << " , dlopen error: " << dlerror() << ".");
        return BIO_UFS_IOERR;
    }

    if (InitOperations() != BIO_OK) {
        LOG_ERROR("Failed to init operations.");
        return BIO_UFS_IOERR;
    }

    return BIO_OK;
}

std::pair<std::string, std::string> ParseIpPort(const std::string &host)
{
    std::pair<std::string, std::string> result;
    size_t pos = host.find(':');
    if (pos != std::string::npos) {
        result.first = host.substr(0, pos);
        result.second = host.substr(pos + 1);
        return result;
    } else {
        return {};
    }
}

bool IsValidPort(std::string &port)
{
    // port should be between 0~65535
    const std::regex pattern(R"(^(6553[0-5]|65[0-4][0-9]{2}|6[0-4][0-9]{3}|[1-5][0-9]{4}|[1-9][0-9]{0,3}|0)$)");
    return std::regex_match(port, pattern);
}

bool IsValidIP(const std::string &ip)
{
    // check IPv4
    const std::regex pattern(R"(^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.)"
        R"((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.)"
        R"((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.)"
        R"((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$)");

    return ((std::regex_match(ip, pattern)) || (ip == "default")); // default: use ip in core-site.xml
}

bool IsValidWorkingPath(std::string &path)
{
    const std::regex pattern("^/(?:[a-zA-Z0-9_]+(?:/[a-zA-Z0-9_]+)*)$");
    return ((!path.empty()) && (path.size() <= MAX_FILENAME_LENGTH) && (std::regex_match(path, pattern)));
}

bool IsValidHdfsConfig(std::pair<std::string, std::string> &ipPort, std::string &workingPath)
{
    if (!IsValidIP(ipPort.first)) {
        LOG_ERROR("invalid namenode ip:" << ipPort.first << ".");
        return false;
    }
    if (!IsValidPort(ipPort.second)) {
        LOG_ERROR("invalid namenode port:" << ipPort.second << ", " << "it should be between 0~65535.");
        return false;
    }
    if (!IsValidWorkingPath(workingPath)) {
        LOG_ERROR("invalid working path.");
        return false;
    }
    return true;
}

BResult HdfsSystem::LoadHdfsConfig()
{
    BioConfig::UnderFsConfig config = UnderFsConfig::Instance()->GetUnderFsConfig();
    BioConfig::HdfsConfig hdfsConfig = { config.hdfsConfig.nameNode, config.hdfsConfig.workingPath };
    std::pair<std::string, std::string> ipPort = ParseIpPort(hdfsConfig.nameNode);
    if (!IsValidHdfsConfig(ipPort, hdfsConfig.workingPath)) {
        LOG_ERROR("invalid hdfs config.");
        return BIO_UFS_IOERR;
    }
    mNameNodeIp = ipPort.first;
    try {
        mNameNodePort = static_cast<uint16_t>(stoi(ipPort.second));
    } catch (...) {
        LOG_ERROR("Failed to convert the type, port: " << ipPort.second << ".");
        return BIO_UFS_IOERR;
    }
    mHdfsWorkingPath = hdfsConfig.workingPath;
    return BIO_OK;
}
}
}