/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023-2023. All rights reserved.
 */

#ifndef HDAGGER_DAGGER_FILE_H
#define HDAGGER_DAGGER_FILE_H

#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>
#include <bio_log.h>

namespace ock {
namespace bio {
class FileUtil {
public:
    /*
     * @brief Check if file or dir exists
     */
    static bool Exist(const std::string &path);

    /*
     * @brief Check if the file or dir readable
     */
    static bool Readable(const std::string &path);

    /*
     * @brief Check if the file or dir writable
     */
    static bool Writable(const std::string &path);

    /*
     * @brief Check if the file or dir readable and writable
     */
    static bool ReadAndWritable(const std::string &path);

    /*
     * @brief Create dir
     */
    static bool MakeDir(const std::string &path, uint32_t mode);

    /*
     * @brief Create dir recursively if parent doesn't exist
     */
    static bool MakeDirRecursive(const std::string &path, uint32_t mode);

    /*
     * @brief Remove the dir without sub dirs
     */
    static bool Remove(const std::string &path, bool canonicalPath = true);

    /*
     * @brief Remove the dir recursively, its sub dir will be removed
     */
    static bool RemoveDirRecursive(const std::string &path);

    /*
     * @brief Get the realpath for security consideration
     */
    static bool CanonicalPath(std::string &path);

    static int64_t GetDiskCapacity(std::string &diskPath);

    static bool AppendConfigToLine(std::vector<std::string>& lines, const std::string& key,
                                   const std::string& newConfig);

    static int64_t FindTargetLine(const std::vector<std::string>& lines, const std::string& key);

    static bool WriteFile(const std::string& filename, const std::vector<std::string>& lines);

    static bool ReadFile(const std::string& filename, std::vector<std::string>& lines);

    static bool BackUpFile(const std::string& srcPath, const std::string& destPath);

    static bool RenameFile(const std::string& oldPath, const std::string& newPath);

    static bool RemoveFile(const std::string& filePath);
};

inline bool FileUtil::Exist(const std::string &path)
{
    return access(reinterpret_cast<const char *>(path.c_str()), 0) != -1;
}

inline bool FileUtil::Readable(const std::string &path)
{
    return access(reinterpret_cast<const char *>(path.c_str()), F_OK | R_OK) != -1;
}

inline bool FileUtil::Writable(const std::string &path)
{
    return access(reinterpret_cast<const char *>(path.c_str()), F_OK | W_OK) != -1;
}

inline bool FileUtil::ReadAndWritable(const std::string &path)
{
    return access(reinterpret_cast<const char *>(path.c_str()), F_OK | R_OK | W_OK) != -1;
}

inline bool FileUtil::MakeDir(const std::string &path, uint32_t mode)
{
    if (path.empty()) {
        return false;
    }

    if (Exist(path)) {
        return true;
    }

    return ::mkdir(path.c_str(), mode) == 0;
}

inline bool FileUtil::MakeDirRecursive(const std::string &path, uint32_t mode)
{
    if (path.empty()) {
        return false;
    }

    if (Exist(path)) {
        return true;
    }

    char *chPath = const_cast<char *>(path.c_str());
    char *p = strchr(chPath + 1, '/');
    for (; p != nullptr; (p = strchr(p + 1, '/'))) {
        *p = '\0';
        if (mkdir(chPath, mode) == -1) {
            if (errno != EEXIST) {
                *p = '/';
                return false;
            }
        }
        *p = '/';
    }

    return ::mkdir(chPath, mode) == 0;
}

inline bool FileUtil::Remove(const std::string &path, bool canonicalPath)
{
    if (path.empty() || path.size() > 4096L) {
        return false;
    }

    std::string realPath = path;
    if (canonicalPath && !CanonicalPath(realPath)) {
        return false;
    }

    return ::remove(realPath.c_str()) == 0;
}

inline bool FileUtil::RemoveDirRecursive(const std::string &path)
{
    if (path.empty() || path.size() > 4096L) {
        return false;
    }

    std::string realPath = path;
    if (!CanonicalPath(realPath)) {
        return false;
    }

    DIR *dir = opendir(realPath.c_str());
    if (dir == nullptr) {
        return false;
    }

    struct dirent *entry = nullptr;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        struct stat statBuf {};
        std::string absPath = realPath + "/" + entry->d_name;
        if (!stat(absPath.c_str(), &statBuf) && S_ISDIR(statBuf.st_mode)) {
            RemoveDirRecursive(absPath);
        }

        ::remove(absPath.c_str());
    }

    ::closedir(dir);

    ::remove(realPath.c_str());
    return true;
}

inline bool FileUtil::CanonicalPath(std::string &path)
{
    if (path.empty() || path.size() > 4096L) {
        return false;
    }

    /* It will allocate memory to store path */
    char *realPath = realpath(path.c_str(), nullptr);
    if (realPath == nullptr) {
        return false;
    }

    path = realPath;
    free(realPath);
    return true;
}

inline int64_t FileUtil::GetDiskCapacity(std::string &diskPath)
{
    char *canonicalPath = realpath(diskPath.c_str(), nullptr);
    if (canonicalPath == nullptr) {
        return 0;
    }
    // get the capacity of this device
    int fd = open(canonicalPath, (O_RDWR | O_SYNC));
    free(canonicalPath);
    canonicalPath = nullptr;
    if (fd < 0) {
        return 0;
    }
    off_t off = lseek(fd, 0, SEEK_END);
    if (off < 0) {
        close(fd);
        return 0;
    }
    close(fd);
    return off;
}

inline bool FileUtil::WriteFile(const std::string& filename, const std::vector<std::string>& lines)
{
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& line : lines) {
        file << line << std::endl;
    }
    file.close();
    return true;
}

inline int64_t FileUtil::FindTargetLine(const std::vector<std::string>& lines, const std::string& key)
{
    for (size_t i = 0; i < lines.size(); ++i) {
        if (lines[i].find(key) != std::string::npos) {
            return i;
        }
    }
    return -1; // Not found
}

inline bool FileUtil::AppendConfigToLine(std::vector<std::string>& lines, const std::string& key,
                                         const std::string& newConfig)
{
    int index = FindTargetLine(lines, key);
    if (index != -1) {
        lines[index] += newConfig;
        return true;
    }
    return false;
}

inline bool FileUtil::ReadFile(const std::string& filename, std::vector<std::string>& lines)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    return true;
}

inline bool FileUtil::BackUpFile(const std::string& srcPath, const std::string& destPath)
{
    // 打开源文件
    std::ifstream src(srcPath, std::ios::binary);
    if (!src) {
        LOG_ERROR("BackUpFile: failed to open source file: '" << srcPath << "'");
        return false;
    }

    // 打开目标文件
    std::ofstream dest(destPath, std::ios::binary);
    if (!dest) {
        LOG_ERROR("BackUpFile: failed to open destination file: '" << destPath << "'");
        return false;
    }

    // 文件赋值
    dest << src.rdbuf();
    if (!dest) {
        LOG_ERROR("BackUpFile: error writing data from '" << srcPath << "' to '" << destPath << "'");
        return false;
    }

    return true;
}

inline bool FileUtil::RenameFile(const std::string& oldPath, const std::string& newPath)
{
    if (oldPath == newPath) {
        LOG_DEBUG("RenameFile: oldPath and newPath are identical, nothing to do.");
        return true;
    }

    if (std::rename(oldPath.c_str(), newPath.c_str()) != 0) {
        int err = errno;
        LOG_ERROR("RenameFile: failed to rename from '" << oldPath << "' to '"
                  << newPath << "', errno=" << err << " (" << std::strerror(err) << ")");
        return false;
    }

    return true;
}

inline bool FileUtil::RemoveFile(const std::string& filePath)
{
    return std::remove(filePath.c_str()) == 0;
}
}
}
#endif // HDAGGER_DAGGER_FILE_H
