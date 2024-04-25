/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#include "bio_cache_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::CacheTp::Register() noexcept
{
    LVOS_TP_REG(CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL, "cache recover flowmanager get all object err",
        tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_READ_CALLBACK_FAIL, "wcache read callback err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIN_LOG_LEVEL_FAIL, "min log lever err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIN_LOG_LEVEL_FAIL_RESET, "reset min log level", tp::CommonTp::IntValueResetCallback);
    LVOS_TP_REG(LOG_ROTATION_FILE_SIZE_FAIL, "log rotation file size err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(LOG_ROTATION_FILE_SIZE_FAIL_RESET, "reset log rotation file size", tp::CommonTp::IntValueResetCallback);
    LVOS_TP_REG(LOG_ROTATION_FILE_COUNT_FAIL, "log rotation file count err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(LOG_ROTATION_FILE_COUNT_FAIL_RESET, "reset log rotation file count",
        tp::CommonTp::IntValueResetCallback);
    LVOS_TP_REG(LOG_INIT_FAIL, "log init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(TRACE_FILE_OPPEN_FAIL, "trace file open err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SERVICE_START_FAIL, "service start err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CONFIG_INIT_FAIL, "config init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(ALLOC_TASK_POOL_FAIL, "alloc task pool err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(ALLOC_TASK_POOL_FAIL_RESET, "reset alloc task pool", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(DLOPEN_SERVERSO_FAIL, "dlopen server.so err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(DLOPEN_SERVERSO_FAIL_RESET, "reset dlopen server.so handler", tp::CommonTp::PointerValueResetCallback);

    LVOS_TP_REG(LIST_LIST_FAIL, "list list err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(LIST_MALLOC_RSP_FAIL, "list malloc rsp err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(LIST_MALLOC_RSP_FAIL_RESET, "reset list malloc rsp", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(PUT_ALLOC_SLICE_FAIL, "put alloc slice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL, "put slice len 0 alloc slice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(WCACHE_FLOW_OFFSET_FAIL, "wcache flow offset err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_FLOW_OFFSET_FAIL_RESET, "reset wcache flow offset", tp::CommonTp::IntValueResetCallback);
    LVOS_TP_REG(WCACHE_HOLD_WAIT_FAIL, "wcache hold wait err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CONFIG_INSTANCE_FAIL, "server start config err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CONFIG_INSTANCE_FAIL_RESET, "reset server start config", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(TRACE_CREATE_DIR_FAIL, "trace create dir err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(TRACE_PATH_REAL_FAIL, "trace path real err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(TRACE_PATH_REAL_FAIL_RESET, "reset trace path real", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(UNDERFS_OPEN_DIR_FAIL, "underfs open dir err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(UNDERFS_MKDIR_FAIL, "underfs make dir err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_INIT_FAIL, "mirror server init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_TASK_FAIL, "mirror task err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_TASK_FAIL_RESET, "reset mirror task", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(MIRROR_SERVER_JOB_FAIL, "mirror job err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_JOB_FAIL_RESET, "reset mirror job", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(QUEUE_INIT_FAIL, "queue init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(EXECUTOR_THREAD_FAIL, "executor thread err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_TASK_FAIL_RESET_OUTER, "reset mirror task outer", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(RCACHE_GC_THREAD_FAIL, "rcache gc thread err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(RCACHE_EVICT_THREAD_FAIL, "rcache evict thread err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CLI_AGENT_INIT_ERR, "cli agent init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CLI_SERVER_DIAGNOSE_HANDLER_ERR, "cli server handler err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CLI_SERVER_DIAGNOSE_INIT_ERR, "cli server init err", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(RCACHE_EVICT_PARAM_FAIL, "rcache gc evict err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_GC_PARAM_FAIL, "rcache gc param err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_EVICT, "no process rcache evict init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_GC, "no process rcache gc init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(MIRROR_FLOW_CREATE_WCACHE_FAIL, "mirror flow create wcache err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET, "reset mirror flow create wcache err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_FIND, "no process rcache find", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_ALLOC_OBJ_FAIL, "rcache alloc obj err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(RCACHE_INIT_OBJ_FAIL, "rcache init obj err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_CACHE_PROCESS, "no process cache process", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_CACHE_INIT, "no process cache init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_SERVER_START, "no process server start", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_ROLLBACK_SERVICE_START, "no process rollback service start", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_LOG_INSTANCE, "no process log instance", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_CONFIG, "no process config", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_UNDERFS_INIT, "no process underfs init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_INIT, "no process mirror server init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_CRB_INIT, "no process mirror server crb init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_EXECUTOR, "no process executor init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_ROLLBACK_SERVICE_INIT, "no process service init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_TASK_START, "no process mirror server task start", tp::CommonTp::NoProcessCallback);

}

void tp::CacheTp::Deregister() noexcept
{
    LVOS_TP_UNREG(CACHE_RECOVER_FM_GET_ALL_OBJECT_FAIL);
    LVOS_TP_UNREG(WCACHE_READCALL_BACK_FAIL);
    LVOS_TP_UNREG(MIN_LOG_LEVEL_FAIL);
    LVOS_TP_UNREG(MIN_LOG_LEVEL_FAIL_RESET);
    LVOS_TP_UNREG(LOG_ROTATION_FILE_SIZE_FAIL);
    LVOS_TP_UNREG(LOG_ROTATION_FILE_SIZE_FAIL_RESET);
    LVOS_TP_UNREG(LOG_ROTATION_FILE_COUNT_FAIL);
    LVOS_TP_UNREG(LOG_ROTATION_FILE_COUNT_FAIL_RESET);
    LVOS_TP_UNREG(LOG_INIT_FAIL);
    LVOS_TP_UNREG(TRACE_FILE_OPPEN_FAIL);
    LVOS_TP_UNREG(SERVICE_START_FAIL);
    LVOS_TP_UNREG(CONFIG_INIT_FAIL);
    LVOS_TP_UNREG(ALLOC_TASK_POOL_FAIL);
    LVOS_TP_UNREG(ALLOC_TASK_POOL_FAIL_RESET);
    LVOS_TP_UNREG(DLOPEN_SERVERSO_FAIL);
    LVOS_TP_UNREG(DLOPEN_SERVERSO_FAIL_RESET);
    LVOS_TP_UNREG(LIST_LIST_FAIL);
    LVOS_TP_UNREG(LIST_MALLOC_RSP_FAIL);
    LVOS_TP_UNREG(LIST_MALLOC_RSP_FAIL_RESET);
    LVOS_TP_UNREG(PUT_ALLOC_SLICE_FAIL);
    LVOS_TP_UNREG(PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL);
    LVOS_TP_UNREG(WCACHE_FLOW_OFFSET_FAIL);
    LVOS_TP_UNREG(WCACHE_FLOW_OFFSET_FAIL_RESET);
    LVOS_TP_UNREG(WCACHE_HOLD_WAIT_FAIL);
    LVOS_TP_UNREG(CONFIG_INSTANCE_FAIL);
    LVOS_TP_UNREG(CONFIG_INSTANCE_FAIL_RESET);
    LVOS_TP_UNREG(TRACE_CREATE_DIR_FAIL);
    LVOS_TP_UNREG(TRACE_PATH_REAL_FAIL);
    LVOS_TP_UNREG(TRACE_PATH_REAL_FAIL_RESET);
    LVOS_TP_UNREG(UNDERFS_OPEN_DIR_FAIL);
    LVOS_TP_UNREG(UNDERFS_MKDIR_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_INIT_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_TASK_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_TASK_FAIL_RESET);
    LVOS_TP_UNREG(MIRROR_SERVER_JOB_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_JOB_FAIL_RESET);
    LVOS_TP_UNREG(EXECUTOR_THREAD_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_TASK_FAIL_RESET_OUTER);
    LVOS_TP_UNREG(NO_PROCESS_MIRROR_SERVER_TASK_START);
    LVOS_TP_UNREG(CLI_AGENT_INIT_ERR);
    LVOS_TP_UNREG(CLI_SERVER_DIAGNOSE_HANDLER_ERR);
    LVOS_TP_UNREG(CLI_SERVER_DIAGNOSE_INIT_ERR);
    LVOS_TP_UNREG(MIRROR_FLOW_CREATE_WCACHE_FAIL);
    LVOS_TP_UNREG(MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET);
    LVOS_TP_UNREG(NO_PROCESS_RCACHE_FIND);
    LVOS_TP_UNREG(RCACHE_ALLOC_OBJ_FAIL);
    LVOS_TP_UNREG(RCACHE_INIT_OBJ_FAIL);

    LVOS_TP_UNREG(NO_PROCESS_SERVER_START);
    LVOS_TP_UNREG(NO_PROCESS_ROLLBACK_SERVICE_START);
    LVOS_TP_UNREG(NO_PROCESS_LOG_INSTANCE);
    LVOS_TP_UNREG(NO_PROCESS_CONFIG);
    LVOS_TP_UNREG(NO_PROCESS_UNDERFS_INIT);
    LVOS_TP_UNREG(NO_PROCESS_MIRROR_SERVER_INIT);
    LVOS_TP_UNREG(NO_PROCESS_MIRROR_SERVER_CRB_INIT);
    LVOS_TP_UNREG(NO_PROCESS_EXECUTOR);
    LVOS_TP_UNREG(NO_PROCESS_ROLLBACK_SERVICE_INIT);
    LVOS_TP_UNREG(NO_PROCESS_CACHE_PROCESS);
    LVOS_TP_UNREG(NO_PROCESS_CACHE_INIT);
}
#else
void tp::CacheTp::Register() noexcept {}

void tp::CacheTp::Deregister() noexcept {}
#endif