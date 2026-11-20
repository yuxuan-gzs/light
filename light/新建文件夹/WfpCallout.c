// WfpCallout.c
#include "WfpCallout.h"

// 全局变量
PDEVICE_OBJECT g_DeviceObject = NULL;
UNICODE_STRING g_DeviceName, g_SymLinkName;

// 拦截统计
UINT64 g_TotalBlocked = 0;
UINT64 g_ActiveRules = 0;

// 黑名单 IP 列表
BLOCK_IP_ENTRY g_BlockedIPs[MAX_IP_LIST_SIZE];
UINT32 g_BlockedIPCount = 0;
KSPIN_LOCK g_BlockedIPLock;

// WFP 相关
HANDLE g_EngineHandle = NULL;
UINT64 g_CalloutId = 0;
UINT64 g_FilterId = 0;

// ============== 辅助函数 ==============

// 检查 IP 是否在黑名单中 (调用前需持有锁)
BOOLEAN IsIpBlockedLocked(UINT32 ip) {
    LARGE_INTEGER currentTime;
    KeQuerySystemTime(&currentTime);
    UINT64 now = currentTime.QuadPart / 10000; // 转换为毫秒

    for (UINT32 i = 0; i < g_BlockedIPCount; i++) {
        if (g_BlockedIPs[i].IpAddress == ip) {
            // 检查是否过期
            if (g_BlockedIPs[i].Timeout != 0 && now > g_BlockedIPs[i].Timeout) {
                // 过期，移除
                if (i < g_BlockedIPCount - 1) {
                    memmove(&g_BlockedIPs[i], &g_BlockedIPs[i + 1],
                        (g_BlockedIPCount - i - 1) * sizeof(BLOCK_IP_ENTRY));
                }
                g_BlockedIPCount--;
                g_ActiveRules--;
                return FALSE;
            }
            return TRUE;
        }
    }
    return FALSE;
}

// 添加 IP 到黑名单
NTSTATUS AddBlockedIP(UINT32 ip, UINT64 timeoutMs) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BlockedIPLock, &oldIrql);

    // 检查是否已存在
    for (UINT32 i = 0; i < g_BlockedIPCount; i++) {
        if (g_BlockedIPs[i].IpAddress == ip) {
            g_BlockedIPs[i].Timeout = timeoutMs;
            KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }

    if (g_BlockedIPCount >= MAX_IP_LIST_SIZE) {
        KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    LARGE_INTEGER currentTime;
    KeQuerySystemTime(&currentTime);

    g_BlockedIPs[g_BlockedIPCount].IpAddress = ip;
    g_BlockedIPs[g_BlockedIPCount].Timeout = (timeoutMs == 0) ? 0 :
        (currentTime.QuadPart / 10000 + timeoutMs);
    g_BlockedIPCount++;
    g_ActiveRules++;

    KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
    return STATUS_SUCCESS;
}

// 从黑名单移除 IP
NTSTATUS RemoveBlockedIP(UINT32 ip) {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BlockedIPLock, &oldIrql);

    for (UINT32 i = 0; i < g_BlockedIPCount; i++) {
        if (g_BlockedIPs[i].IpAddress == ip) {
            if (i < g_BlockedIPCount - 1) {
                memmove(&g_BlockedIPs[i], &g_BlockedIPs[i + 1],
                    (g_BlockedIPCount - i - 1) * sizeof(BLOCK_IP_ENTRY));
            }
            g_BlockedIPCount--;
            g_ActiveRules--;
            KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
            return STATUS_SUCCESS;
        }
    }

    KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
    return STATUS_NOT_FOUND;
}

// 清空所有黑名单
void ClearAllBlockedIPs() {
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BlockedIPLock, &oldIrql);
    g_BlockedIPCount = 0;
    g_ActiveRules = 0;
    KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);
}

// ============== WFP Callout 分类函数 ==============

VOID NTAPI Classify(
    const FWPS_INCOMING_VALUES* inFixedValues,
    const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
    void* layerData,
    const void* classifyContext,
    const FWPS_FILTER* filter,
    UINT64 flowContext,
    FWPS_CLASSIFY_OUT* classifyOut
) {
    UNREFERENCED_PARAMETER(inMetaValues);
    UNREFERENCED_PARAMETER(layerData);
    UNREFERENCED_PARAMETER(classifyContext);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(flowContext);

    // 获取目标 IP
    UINT32 remoteIp = inFixedValues->incomingValue[FWPS_FIELD_ALE_AUTH_CONNECT_V4_IP_REMOTE_ADDRESS].value.uint32;

    // 检查是否在黑名单中
    KIRQL oldIrql;
    KeAcquireSpinLock(&g_BlockedIPLock, &oldIrql);

    BOOLEAN blocked = IsIpBlockedLocked(remoteIp);

    KeReleaseSpinLock(&g_BlockedIPLock, oldIrql);

    if (blocked) {
        // 阻止连接
        classifyOut->actionType = FWP_ACTION_BLOCK;
        InterlockedIncrement64((LONG64*)&g_TotalBlocked);

        DbgPrint("[WfpCallout] BLOCKED: IP %d.%d.%d.%d\n",
            (remoteIp >> 24) & 0xFF, (remoteIp >> 16) & 0xFF,
            (remoteIp >> 8) & 0xFF, remoteIp & 0xFF);
    }
    else {
        // 放行
        classifyOut->actionType = FWP_ACTION_PERMIT;
    }
}

// ============== WFP 注册 ==============

NTSTATUS RegisterWfpCallout() {
    NTSTATUS status;

    // 打开 WFP 引擎
    FWPM_SESSION0 session = { 0 };
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;

    status = FwpmEngineOpen(
        NULL,
        RPC_C_AUTHN_WINNT,
        NULL,
        &session,
        &g_EngineHandle
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] FwpmEngineOpen failed: 0x%X\n", status);
        return status;
    }

    // 注册 Callout
    FWPS_CALLOUT0 callout = { 0 };
    callout.calloutKey = { 0x12345678, 0x1234, 0x1234,
                           {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0} };
    callout.flags = 0;
    callout.classifyFn = Classify;
    callout.notifyFn = NULL;
    callout.flowDeleteFn = NULL;

    status = FwpsCalloutRegister(
        (void*)g_DeviceObject,
        &callout,
        &g_CalloutId
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] FwpsCalloutRegister failed: 0x%X\n", status);
        FwpmEngineClose(g_EngineHandle);
        g_EngineHandle = NULL;
        return status;
    }

    // 添加过滤器
    FWPM_FILTER_CONDITION0 cond = { 0 };
    cond.fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    cond.matchType = FWP_MATCH_EQUAL;
    cond.conditionValue.type = FWP_UINT32;
    cond.conditionValue.uint32 = 0; // 匹配所有 IP

    FWPM_FILTER0 filter = { 0 };
    filter.layerKey = FWPM_LAYER_ALE_AUTH_CONNECT_V4;
    filter.displayData.name = L"Light WFP Callout";
    filter.displayData.description = L"Block IPs via WFP Callout";
    filter.filterCondition = &cond;
    filter.numFilterConditions = 1;
    filter.action.type = FWP_ACTION_CALLOUT_TERMINATING;
    filter.action.calloutKey = callout.calloutKey;
    filter.weight.type = FWP_EMPTY;

    status = FwpmFilterAdd(
        g_EngineHandle,
        &filter,
        NULL,
        &g_FilterId
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] FwpmFilterAdd failed: 0x%X\n", status);
        FwpsCalloutUnregisterById(g_CalloutId);
        FwpmEngineClose(g_EngineHandle);
        g_EngineHandle = NULL;
        return status;
    }

    DbgPrint("[WfpCallout] WFP Callout registered successfully\n");
    return STATUS_SUCCESS;
}

VOID UnregisterWfpCallout() {
    if (g_FilterId != 0) {
        FwpmFilterDeleteById(g_EngineHandle, g_FilterId);
        g_FilterId = 0;
    }

    if (g_CalloutId != 0) {
        FwpsCalloutUnregisterById(g_CalloutId);
        g_CalloutId = 0;
    }

    if (g_EngineHandle) {
        FwpmEngineClose(g_EngineHandle);
        g_EngineHandle = NULL;
    }

    DbgPrint("[WfpCallout] WFP Callout unregistered\n");
}

// ============== DeviceIoControl 处理 ==============

NTSTATUS HandleDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
    UNREFERENCED_PARAMETER(deviceObject);

    PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(irp);
    ULONG controlCode = irpStack->Parameters.DeviceIoControl.IoControlCode;
    PVOID inputBuffer = irp->AssociatedIrp.SystemBuffer;
    ULONG inputLength = irpStack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID outputBuffer = irp->AssociatedIrp.SystemBuffer;
    ULONG outputLength = irpStack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG bytesReturned = 0;

    NTSTATUS status = STATUS_SUCCESS;

    switch (controlCode) {
    case IOCTL_ADD_BLOCK_IP: {
        if (inputLength < sizeof(BLOCK_IP_ENTRY)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        BLOCK_IP_ENTRY* entry = (BLOCK_IP_ENTRY*)inputBuffer;
        status = AddBlockedIP(entry->IpAddress, entry->Timeout);
        break;
    }

    case IOCTL_REMOVE_BLOCK_IP: {
        if (inputLength < sizeof(UINT32)) {
            status = STATUS_INVALID_PARAMETER;
            break;
        }

        UINT32 ip = *(UINT32*)inputBuffer;
        status = RemoveBlockedIP(ip);
        break;
    }

    case IOCTL_CLEAR_ALL: {
        ClearAllBlockedIPs();
        break;
    }

    case IOCTL_GET_STATS: {
        if (outputLength < sizeof(STATS_DATA)) {
            status = STATUS_BUFFER_TOO_SMALL;
            break;
        }

        STATS_DATA* stats = (STATS_DATA*)outputBuffer;
        stats->TotalBlocked = g_TotalBlocked;
        stats->ActiveRules = g_ActiveRules;
        bytesReturned = sizeof(STATS_DATA);
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    irp->IoStatus.Status = status;
    irp->IoStatus.Information = bytesReturned;
    IoCompleteRequest(irp, IO_NO_INCREMENT);
    return status;
}

// ============== 驱动入口/卸载 ==============

NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING registryPath) {
    UNREFERENCED_PARAMETER(registryPath);

    NTSTATUS status;

    DbgPrint("[WfpCallout] DriverEntry\n");

    // 初始化自旋锁
    KeInitializeSpinLock(&g_BlockedIPLock);
    g_BlockedIPCount = 0;
    g_TotalBlocked = 0;
    g_ActiveRules = 0;

    // 创建设备
    RtlInitUnicodeString(&g_DeviceName, DEVICE_NAME);
    RtlInitUnicodeString(&g_SymLinkName, SYM_LINK_NAME);

    status = IoCreateDevice(
        driverObject,
        0,
        &g_DeviceName,
        FILE_DEVICE_UNKNOWN,
        0,
        FALSE,
        &g_DeviceObject
    );

    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] IoCreateDevice failed: 0x%X\n", status);
        return status;
    }

    status = IoCreateSymbolicLink(&g_SymLinkName, &g_DeviceName);
    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] IoCreateSymbolicLink failed: 0x%X\n", status);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    // 设置分发函数
    driverObject->MajorFunction[IRP_MJ_CREATE] =
        driverObject->MajorFunction[IRP_MJ_CLOSE] =
        [](PDEVICE_OBJECT, PIRP irp) { irp->IoStatus.Status = STATUS_SUCCESS; IoCompleteRequest(irp, IO_NO_INCREMENT); return STATUS_SUCCESS; };

    driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = HandleDeviceControl;
    driverObject->DriverUnload = [](PDRIVER_OBJECT) { DriverUnload(); };

    // 注册 WFP Callout
    status = RegisterWfpCallout();
    if (!NT_SUCCESS(status)) {
        DbgPrint("[WfpCallout] RegisterWfpCallout failed: 0x%X\n", status);
        IoDeleteSymbolicLink(&g_SymLinkName);
        IoDeleteDevice(g_DeviceObject);
        return status;
    }

    DbgPrint("[WfpCallout] Driver loaded successfully\n");
    return STATUS_SUCCESS;
}

VOID DriverUnload() {
    DbgPrint("[WfpCallout] DriverUnload\n");

    UnregisterWfpCallout();

    IoDeleteSymbolicLink(&g_SymLinkName);
    IoDeleteDevice(g_DeviceObject);

    DbgPrint("[WfpCallout] Driver unloaded\n");
}
