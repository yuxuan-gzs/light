// WfpCallout.h
#pragma once

#include <ntddk.h>
#include <fwpsk.h>
#include <fwpmk.h>
#include <netioapi.h>
#include <wdm.h>

#define DEVICE_NAME L"\\Device\\WfpCallout"
#define SYM_LINK_NAME L"\\DosDevices\\WfpCallout"

#define IOCTL_ADD_BLOCK_IP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_REMOVE_BLOCK_IP CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_CLEAR_ALL CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_GET_STATS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define MAX_IP_LIST_SIZE 1024

typedef struct _BLOCK_IP_ENTRY {
    UINT32 IpAddress;      // 网络字节序
    UINT64 Timeout;        // 过期时间 (毫秒)，0 = 永久
} BLOCK_IP_ENTRY;

typedef struct _BLOCK_IP_LIST {
    UINT32 Count;
    BLOCK_IP_ENTRY Entries[MAX_IP_LIST_SIZE];
} BLOCK_IP_LIST;

typedef struct _STATS_DATA {
    UINT64 TotalBlocked;
    UINT64 ActiveRules;
} STATS_DATA;