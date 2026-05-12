/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *      http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef HDAGGER_DAGGER_FILE_H
#define HDAGGER_DAGGER_FILE_H

#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

namespace ock {
namespace mms {
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
    int fd = open(diskPath.c_str(), (O_RDWR | O_SYNC));
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
}
}
#endif // HDAGGER_DAGGER_FILE_H

