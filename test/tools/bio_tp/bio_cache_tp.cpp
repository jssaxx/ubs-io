/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2023. All rights reserved.
 */

#include "bio_cache_tp.h"

using namespace ock::bio;
#ifdef __aarch64__
static uint32_t MY_PID = 102;
void tp::CacheTp::Register() noexcept
{
    LVOS_TP_REG(CACHE_RECOVER_CACHE_FAIL, "cache recover cache err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SLICE_OPERATOR_4_FLOW_MEMORY, "slice operator 4 params flow memory err",
        tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SLICE_COPY_DISK2MEMORY_OK, "slice copy disk to memory ok", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SLICE_COPY_MEMORY2MEMORY_ERR, "slice copy memory to memory err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SLICE_OPERATOR_FLOW_MEMORY, "slice operator flow memory err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SLICE_OPERATOR_2_FLOW_MEMORY, "slice operator to flow memory err", tp::CommonTp::IntValueCallback);
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
    LVOS_TP_REG(DESTROY_WCACHE_FAIL, "destroy wcache err", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(WCACHE_DELETE_FLOWID_ERR, "wcache delete flowid err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(RCACHE_MANAGER_DELETE_ERR, "rcache manager delete err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(NO_PROCESS_CLEAR_OLD_CACHE, "no process clear old cache", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_FLUSH, "no process flush", tp::CommonTp::NoProcessCallback);

    LVOS_TP_REG(LIST_LIST_FAIL, "list list err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(LIST_MALLOC_RSP_FAIL, "list malloc rsp err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(LIST_MALLOC_RSP_FAIL_RESET, "reset list malloc rsp", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(PUT_ALLOC_SLICE_FAIL, "put alloc slice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(PUT_SLICELEN_ZERO_ALLOC_SLICE_FAIL, "put slice len 0 alloc slice err",
        tp::CommonTp::PointerValueCallback);
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
    LVOS_TP_REG(MIRROR_SERVER_TASK_FAIL_RESET_OUTER, "reset mirror task outer",
        tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(RCACHE_GC_THREAD_FAIL, "rcache gc thread err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(RCACHE_EVICT_THREAD_FAIL, "rcache evict thread err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CLI_AGENT_INIT_ERR, "cli agent init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CLI_SERVER_DIAGNOSE_HANDLER_ERR, "cli server handler err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CLI_SERVER_DIAGNOSE_INIT_ERR, "cli server init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MR_POOL_NULL_FAIL, "mr pool null err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(MR_POOL_NULL_FAIL_RESET, "reset mr pool null", tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(WCACHE_FLOW_DISK_FAIL, "wcache flow disk err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SYNCCALL_FAIL, "synccall err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SYNCCALL_CHANNEL_FAIL, "synccall channel err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(SYNCCALL_OPCODE_FAIL, "synccall opcode err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(PUT_SLICE_ZERO_ALLOC_FAIL, "put slice zero alloc err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(PUT_SLICE_NORMAL_ALLOC_FAIL, "put slice normal alloc err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(WRITE_SLICE_NULL_FAIL, "write slice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(HTRACE_INSTANCE_ERR, "htrace instance err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(HTRACE_INSTANCE_INNER_ERR, "htrace instance inner err", tp::CommonTp::PointerValueCallback);

    LVOS_TP_REG(RCACHE_MANAGER_INIT_FAIL, "rcache manager init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(RCACHE_MANAGER_INIT_FAIL_RESET, "rcache manager init err reset", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_MANAGER_INIT_FAIL, "wcache manager init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_MANAGER_INIT_FAIL_RESET, "wcache manager init err reset", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CACHE_OVERLOAD_INIT_OK, "cache over load init ok", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(CACHE_RECOVER_TYPE_FAIL, "cache recover type", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(CACHE_RECOVER_TYPE_INNER_FAIL, "cache recover inner type", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_CACHE_RECOVER, "no process cache recover", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(WCACHE_ALLOC_FAIL, "wcache alloc err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(WCACHE_INDEX_TABLE_FAIL, "wcache index table err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(WCACHE_GET_OK, "wcache get ok", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_NOT_EXIST, "wcache alloc err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_DESTROY_WCACHE, "no process wcache destroy", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(MIRROR_LIST_FAIL, "mirror list err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(UNDERFS_INIT_FAIL, "under fs init err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_DELETE, "mirror delete key err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_GET_ERR, "mirror delete key err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_GET_META_SLICE_FAIL, "wcache get meta slice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(UNDERFS_DELETE_ERR, "underfs delete err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_EXPIRED_CLEAR_OK, "wcahce expired clear err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_EXPIRED_CLEAR, "no process wcache expired clear err",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_STOP_EVICT, "no process rcache stop evict", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_EVICT_ERR, "rcache evict err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_TIER_TYPE_FAIL, "wcache tier type err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_TIER_ALLOC_FAIL, "wcache tier alloc err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(DISK_WCACHE_TIER_DESTROY_FAIL, "disk wcache tier destroy err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(RECOVER_CACHE_FLOWID_FAIL, "recover cache flowid err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_STATE_NOT_NORMAL, "wcache state not normal", tp::CommonTp::BoolValueCallback);

    LVOS_TP_REG(RCACHE_EVICT_PARAM_FAIL, "rcache gc evict err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_GC_PARAM_FAIL, "rcache gc param err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_EVICT, "no process rcache evict init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_GC, "no process rcache gc init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(MIRROR_FLOW_CREATE_WCACHE_FAIL, "mirror flow create wcache err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_FLOW_CREATE_WCACHE_FAIL_RESET, "reset mirror flow create wcache err",
        tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_FIND, "no process rcache find", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(RCACHE_ALLOC_OBJ_FAIL, "rcache alloc obj err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(RCACHE_INIT_OBJ_FAIL, "rcache init obj err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_CACHE_PROCESS, "no process cache process", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_CACHE_INIT, "no process cache init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_SERVER_START, "no process server start", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_ROLLBACK_SERVICE_START, "no process rollback service start",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_LOG_INSTANCE, "no process log instance", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_CONFIG, "no process config", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_UNDERFS_INIT, "no process underfs init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_INIT, "no process mirror server init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_CRB_INIT, "no process mirror server crb init",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_EXECUTOR, "no process executor init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_ROLLBACK_SERVICE_INIT, "no process service init", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_MIRROR_SERVER_TASK_START, "no process mirror server task start",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_PUT, "no process wcache put", tp::CommonTp::NoProcessCallback);

    LVOS_TP_REG(NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT, "no process wcache manager empty evict",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(WCACHE_HANDLE_BROCK_FLOWID_FAIL, "wcache handle brock flowid err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(HANDLE_CACHE_BROKE_OK, "handle cache broke ok", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_DESTROY_EVICT_THREAD, "no process destroy evict thread", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_FLUSH, "no process wcache flush", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(WCACHE_HANDLE_BROCK_FLUSH, "wcache handle brock flush", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR, "no process wcache expired clear",
        tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(WCACHE_HANDLE_BROCK_EXPIRED_CLEAR, "wcache handle bock expired clear", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(WCACHE_STATE_NORMAL, "wcache state normal", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_MANAGER_ERASE, "no process wcache manager erase", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_READ_CALLBACK, "no process wcache read callback", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(HANDLE_PROC_BROKEN_FAIL, "handle proc broken fail", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(NO_PROCESS_WCACHE_VALIDATE, "no process wcache validate", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(HANDLE_PROC_BROKE_OK, "handle proc broken ok", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR, "wcache handle proc broken expired clear",
        tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(WCACHE_HANDLE_PROC_BROCK_ROLE_ERR, "wcache handle proc broke role err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(NO_PROCESS_CLEAR_PROC_CACHE, "no process clear proc cache", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(CACHE_DELETE_RCACHE_MANAGER_ERR, "cache delete rcache manager err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(RCACHE_EVICT_OK, "rcache evict ok", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_RELEASE, "no process rcache release", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(NO_PROCESS_RCACHE_DESTROY_INDEX, "no process rcache destroy index", tp::CommonTp::NoProcessCallback);

    LVOS_TP_REG(RCACHE_MANAGER_GET_INSTANCE_FAIL, "rcache manager get instance err",
        tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(WCACHE_MANAGER_GET_INSTANCE_FAIL, "wcache manager get instance err",
        tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(CACHE_EXPIRED_ERR, "cache expired rcache err", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(ALLOC_TASK_POOL_FAIL_RESET_OUTER, "alloc task pool reset outer",
        tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(ALLOC_DISK_TASK_POOL_FAIL_RESET_OUTER, "alloc disk task pool reset outer",
        tp::CommonTp::PointerValueResetCallback);
    LVOS_TP_REG(NO_PROCESS_EXECUTOR_POOL_START, "no process executor pool start", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(MEXECSERVICE_START_FAIL, "executor service start fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(EXECUTOR_SERVICE_STOP_TASK_NULL, "executor service stop task fail", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(EXECUTOR_SERVICE_MSERVICE_FAIL, "executor service mservice err", tp::CommonTp::PointerValueCallback);
    LVOS_TP_REG(NO_PROCESS_EXECUTOR_SERVICE_JOIN, "no process executor service join", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(EXECUTOR_SERVICE_MSERVICE_FAIL_RESET, "executor service mservice fail reset",
        tp::CommonTp::PointerValueResetCallback);


    LVOS_TP_REG(MIRROR_SERVER_GET_SLICE_FAIL, "mirror server get slice err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_PUT_FAIL, "mirror server handle put err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_GET_FAIL, "mirror server handle get err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_DELETE_FAIL, "mirror server handle delete err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_LOAD_FAIL, "mirror server handle load err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_STAT_FAIL, "mirror server handle stat err", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(MIRROR_SERVER_HDL_LIST_FAIL, "mirror server handle list err", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(WCACHE_GET_MEM_SLICE_FAIL, "wcache get mem slice fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_GET_DISK_SLICE_FAIL, "wcache get disk slice fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_GET_EVICT_OFFSET_FAIL, "wcache get evict offset fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_CHECK_RCACHE_LEVEL_FAIL, "wcache check rcache level fail", tp::CommonTp::BoolValueCallback);
    LVOS_TP_REG(WCACHE_FLUSH_FAIL, "wcache flush fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(WCACHE_EXPIRE_FAIL, "wcache flush fail", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(RCACHE_GET_MEM_SLICE_FAIL, "rcache get mem slice fail", tp::CommonTp::IntValueCallback);
    LVOS_TP_REG(RCACHE_GET_EVICT_IO_FAIL, "rcache get evict io fail", tp::CommonTp::LongValueCallback);
    LVOS_TP_REG(RCACHE_GET_DISK_SLICE_FAIL, "rcache get disk slice fail", tp::CommonTp::IntValueCallback);

    LVOS_TP_REG(BDM_RW_IO_FAIL, "bdm io err", tp::CommonTp::NoProcessCallback);
    LVOS_TP_REG(BDM_ALLOC_BLOCK_FAIL, "bdm alloc block fail", tp::CommonTp::IntValueCallback);
}

void tp::CacheTp::Deregister() noexcept
{
    LVOS_TP_UNREG(ALLOC_TASK_POOL_FAIL_RESET_OUTER);
    LVOS_TP_UNREG(ALLOC_DISK_TASK_POOL_FAIL_RESET_OUTER);
    LVOS_TP_UNREG(NO_PROCESS_EXECUTOR_POOL_START);
    LVOS_TP_UNREG(MEXECSERVICE_START_FAIL);
    LVOS_TP_UNREG(EXECUTOR_SERVICE_STOP_TASK_NULL);
    LVOS_TP_UNREG(EXECUTOR_SERVICE_MSERVICE_FAIL);
    LVOS_TP_UNREG(NO_PROCESS_EXECUTOR_SERVICE_JOIN);
    LVOS_TP_UNREG(EXECUTOR_SERVICE_MSERVICE_FAIL_RESET);
    LVOS_TP_UNREG(CACHE_EXPIRED_ERR);
    LVOS_TP_UNREG(RCACHE_MANAGER_GET_INSTANCE_FAIL);
    LVOS_TP_UNREG(WCACHE_MANAGER_GET_INSTANCE_FAIL);
    LVOS_TP_UNREG(CACHE_DELETE_RCACHE_MANAGER_ERR);
    LVOS_TP_UNREG(CACHE_RECOVER_CACHE_FAIL);
    LVOS_TP_UNREG(SLICE_OPERATOR_4_FLOW_MEMORY);
    LVOS_TP_UNREG(SLICE_COPY_DISK2MEMORY_OK);
    LVOS_TP_UNREG(SLICE_COPY_MEMORY2MEMORY_ERR);
    LVOS_TP_UNREG(SLICE_OPERATOR_FLOW_MEMORY);
    LVOS_TP_UNREG(SLICE_OPERATOR_2_FLOW_MEMORY);
    LVOS_TP_UNREG(NO_PROCESS_CLEAR_PROC_CACHE);
    LVOS_TP_UNREG(HANDLE_PROC_BROKEN_FAIL);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_VALIDATE);
    LVOS_TP_UNREG(HANDLE_PROC_BROKE_OK);
    LVOS_TP_UNREG(WCACHE_HANDLE_PROC_BROCK_EXPIRED_CLEAR);
    LVOS_TP_UNREG(WCACHE_HANDLE_PROC_BROCK_ROLE_ERR);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_READ_CALLBACK);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_MANAGER_ERASE);
    LVOS_TP_UNREG(WCACHE_STATE_NORMAL);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_MANAGER_EMPTY_EVICT);
    LVOS_TP_UNREG(WCACHE_HANDLE_BROCK_FLOWID_FAIL);
    LVOS_TP_UNREG(HANDLE_CACHE_BROKE_OK);
    LVOS_TP_UNREG(NO_PROCESS_DESTROY_EVICT_THREAD);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_FLUSH);
    LVOS_TP_UNREG(WCACHE_HANDLE_BROCK_FLUSH);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_EXPIRED_CLEAR);
    LVOS_TP_UNREG(WCACHE_HANDLE_BROCK_EXPIRED_CLEAR);
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
    LVOS_TP_UNREG(DESTROY_WCACHE_FAIL);
    LVOS_TP_UNREG(MR_POOL_NULL_FAIL);
    LVOS_TP_UNREG(MR_POOL_NULL_FAIL_RESET);
    LVOS_TP_UNREG(WCACHE_FLOW_DISK_FAIL);
    LVOS_TP_UNREG(SYNCCALL_FAIL);
    LVOS_TP_UNREG(SYNCCALL_CHANNEL_FAIL);
    LVOS_TP_UNREG(SYNCCALL_OPCODE_FAIL);
    LVOS_TP_UNREG(PUT_SLICE_ZERO_ALLOC_FAIL);
    LVOS_TP_UNREG(PUT_SLICE_NORMAL_ALLOC_FAIL);
    LVOS_TP_UNREG(WRITE_SLICE_NULL_FAIL);
    LVOS_TP_UNREG(WCACHE_READ_CALLBACK_FAIL);
    LVOS_TP_UNREG(HTRACE_INSTANCE_ERR);
    LVOS_TP_UNREG(HTRACE_INSTANCE_INNER_ERR);
    LVOS_TP_UNREG(RCACHE_MANAGER_INIT_FAIL);
    LVOS_TP_UNREG(RCACHE_MANAGER_INIT_FAIL_RESET);
    LVOS_TP_UNREG(WCACHE_MANAGER_INIT_FAIL);
    LVOS_TP_UNREG(WCACHE_MANAGER_INIT_FAIL_RESET);
    LVOS_TP_UNREG(CACHE_OVERLOAD_INIT_OK);
    LVOS_TP_UNREG(CACHE_RECOVER_TYPE_FAIL);
    LVOS_TP_UNREG(CACHE_RECOVER_TYPE_INNER_FAIL);
    LVOS_TP_UNREG(NO_PROCESS_CACHE_RECOVER);
    LVOS_TP_UNREG(WCACHE_ALLOC_FAIL);
    LVOS_TP_UNREG(WCACHE_ALLOC_FAIL);
    LVOS_TP_UNREG(WCACHE_INDEX_TABLE_FAIL);
    LVOS_TP_UNREG(WCACHE_GET_OK);
    LVOS_TP_UNREG(WCACHE_NOT_EXIST);
    LVOS_TP_UNREG(NO_PROCESS_DESTROY_WCACHE);
    LVOS_TP_UNREG(MIRROR_LIST_FAIL);
    LVOS_TP_UNREG(UNDERFS_INIT_FAIL);
    LVOS_TP_UNREG(NO_PROCESS_MIRROR_DELETE);
    LVOS_TP_UNREG(RCACHE_GET_ERR);
    LVOS_TP_UNREG(WCACHE_DELETE_FLOWID_ERR);
    LVOS_TP_UNREG(RCACHE_MANAGER_DELETE_ERR);
    LVOS_TP_UNREG(WCACHE_GET_META_SLICE_FAIL);
    LVOS_TP_UNREG(UNDERFS_DELETE_ERR);
    LVOS_TP_UNREG(NO_PROCESS_CLEAR_OLD_CACHE);
    LVOS_TP_UNREG(NO_PROCESS_FLUSH);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_MANAGER_EXPIRED_CLEAR);
    LVOS_TP_UNREG(WCACHE_EXPIRED_CLEAR_OK);
    LVOS_TP_UNREG(NO_PROCESS_RCACHE_EVICT);
    LVOS_TP_UNREG(RCACHE_EVICT_ERR);
    LVOS_TP_UNREG(NO_PROCESS_RCACHE_STOP_EVICT);
    LVOS_TP_UNREG(WCACHE_TIER_TYPE_FAIL);
    LVOS_TP_UNREG(WCACHE_TIER_ALLOC_FAIL);
    LVOS_TP_UNREG(DISK_WCACHE_TIER_DESTROY_FAIL);
    LVOS_TP_UNREG(RECOVER_CACHE_FLOWID_FAIL);
    LVOS_TP_UNREG(WCACHE_STATE_NOT_NORMAL);
    LVOS_TP_UNREG(NO_PROCESS_WCACHE_PUT);
    LVOS_TP_UNREG(RCACHE_EVICT_OK);
    LVOS_TP_UNREG(NO_PROCESS_RCACHE_RELEASE);
    LVOS_TP_UNREG(NO_PROCESS_RCACHE_DESTROY_INDEX);

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

    LVOS_TP_UNREG(MIRROR_SERVER_GET_SLICE_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_PUT_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_GET_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_DELETE_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_LOAD_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_STAT_FAIL);
    LVOS_TP_UNREG(MIRROR_SERVER_HDL_LIST_FAIL);
    LVOS_TP_UNREG(WCACHE_GET_MEM_SLICE_FAIL);
    LVOS_TP_UNREG(WCACHE_GET_DISK_SLICE_FAIL);
    LVOS_TP_UNREG(WCACHE_GET_EVICT_OFFSET_FAIL);
    LVOS_TP_UNREG(WCACHE_CHECK_RCACHE_LEVEL_FAIL);
    LVOS_TP_UNREG(WCACHE_FLUSH_FAIL);
    LVOS_TP_UNREG(WCACHE_EXPIRE_FAIL);
    LVOS_TP_UNREG(RCACHE_GET_MEM_SLICE_FAIL);
    LVOS_TP_UNREG(RCACHE_GET_EVICT_IO_FAIL);
    LVOS_TP_UNREG(RCACHE_GET_DISK_SLICE_FAIL);
    LVOS_TP_UNREG(BDM_RW_IO_FAIL);
    LVOS_TP_UNREG(BDM_ALLOC_BLOCK_FAIL);
}
#else
void tp::CacheTp::Register() noexcept {}

void tp::CacheTp::Deregister() noexcept {}
#endif
