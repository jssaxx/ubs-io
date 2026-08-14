/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
 *
 * ubs-io is licensed under the Mulan PSL v2.
 */

#ifndef UBSIO_KV_UT_STUB_CONTROL_H
#define UBSIO_KV_UT_STUB_CONTROL_H

#include <cstddef>
#include <cstdint>

extern "C" {

void FakeBioReset();
void FakeBioSetResult(const char *name, int result);
int FakeBioGetCallCount(const char *name);
void FakeBioSetStatSize(uint32_t size);
void FakeBioSetBatchItemResult(int result);
void FakeBioSetResourceDiskCount(uint16_t count);
void FakeBioSetScanCount(uint32_t count);
void FakeBioSetDiskInfo(const char *path, uint64_t offset, uint64_t length, int result);
uint32_t FakeBioGetStandaloneDevice();

void FakeAclReset();
void FakeAclSetResult(const char *name, int result);
int FakeAclGetCallCount(const char *name);
void FakeAclSetCurrentDevice(int32_t device);
int32_t FakeAclGetCurrentDevice();

void FakeNdsReset();
void FakeNdsSetResult(const char *name, long long result);
int FakeNdsGetCallCount(const char *name);

}

#endif
