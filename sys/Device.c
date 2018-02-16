/*
MIT License

Copyright (c) 2016 Benjamin "Nefarius" H�glinger

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/


#include "driver.h"
#include "device.tmh"

#ifdef ALLOC_PRAGMA
#pragma alloc_text (PAGE, HidGuardianCreateDevice)
#pragma alloc_text (PAGE, HidGuardianEvtDeviceContextCleanup)
#pragma alloc_text (PAGE, EvtWdfCreateRequestsQueueIoDefault)
#pragma alloc_text (PAGE, EvtFileCleanup)
#endif


NTSTATUS
HidGuardianCreateDevice(
    _Inout_ PWDFDEVICE_INIT DeviceInit
)
{
    WDF_OBJECT_ATTRIBUTES   deviceAttributes;
    PDEVICE_CONTEXT         deviceContext;
    WDFDEVICE               device;
    NTSTATUS                status;
    WDF_FILEOBJECT_CONFIG   deviceConfig;
    WDFMEMORY               memory;
    WDF_IO_QUEUE_CONFIG     queueConfig;
    WDFMEMORY               classNameMemory;
    PCWSTR                  className;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    WdfFdoInitSetFilter(DeviceInit);

    WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);
    deviceAttributes.SynchronizationScope = WdfSynchronizationScopeNone;
    WDF_FILEOBJECT_CONFIG_INIT(&deviceConfig, 
        WDF_NO_EVENT_CALLBACK, 
        WDF_NO_EVENT_CALLBACK, 
        EvtFileCleanup
    );

    WdfDeviceInitSetFileObjectConfig(
        DeviceInit,
        &deviceConfig,
        &deviceAttributes
    );

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, DEVICE_CONTEXT);

    //
    // We will just register for cleanup notification because we have to
    // delete the control-device when the last instance of the device goes
    // away. If we don't delete, the driver wouldn't get unloaded automatically
    // by the PNP subsystem.
    //
    deviceAttributes.EvtCleanupCallback = HidGuardianEvtDeviceContextCleanup;

    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);

    if (NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_VERBOSE,
            TRACE_DEVICE,
            "Current device handle: %p", device);

        //
        // Get a pointer to the device context structure that we just associated
        // with the device object. We define this structure in the device.h
        // header file. DeviceGetContext is an inline function generated by
        // using the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro in device.h.
        // This function will do the type checking and return the device context.
        // If you pass a wrong object handle it will return NULL and assert if
        // run under framework verifier mode.
        //
        deviceContext = DeviceGetContext(device);

        //
        // Linked list for sticky PIDs
        // 
        deviceContext->StickyPidList = PID_LIST_CREATE();
        if (deviceContext->StickyPidList == NULL) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "PID_LIST_CREATE failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        //
        // Always allow SYSTEM PID 4
        // 
        PID_LIST_PUSH(&deviceContext->StickyPidList, SYSTEM_PID, TRUE);

        WDF_OBJECT_ATTRIBUTES_INIT(&deviceAttributes);
        deviceAttributes.ParentObject = device;

        //
        // Query for current device's Hardware ID
        // 
        status = WdfDeviceAllocAndQueryProperty(device,
            DevicePropertyHardwareID,
            NonPagedPool,
            &deviceAttributes,
            &memory
        );

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfDeviceAllocAndQueryProperty failed with status %!STATUS!", status);
            return status;
        }

        //
        // Get Hardware ID string
        // 
        deviceContext->HardwareIDsMemory = memory;
        deviceContext->HardwareIDs = WdfMemoryGetBuffer(memory, &deviceContext->HardwareIDsLength);

        //
        // Query for current device's ClassName
        // 
        status = WdfDeviceAllocAndQueryProperty(device,
            DevicePropertyClassName,
            NonPagedPool,
            &deviceAttributes,
            &classNameMemory
        );

        if (NT_SUCCESS(status)) {
            className = WdfMemoryGetBuffer(classNameMemory, NULL);

            TraceEvents(TRACE_LEVEL_INFORMATION,
                TRACE_DEVICE,
                "Current device class: %ls", className);
        }

        //
        // Initialize the I/O Package and any Queues
        //
        status = HidGuardianQueueInitialize(device);

        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "HidGuardianQueueInitialize failed with status %!STATUS!", status);
            return status;
        }

#pragma region Create PendingAuthQueue I/O Queue

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

        status = WdfIoQueueCreate(device,
            &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &deviceContext->PendingAuthQueue
        );
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfIoQueueCreate (PendingAuthQueue) failed with %!STATUS!", status);
            return status;
        }

#pragma endregion

#pragma region Create CreateRequestsQueue I/O Queue

        WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchSequential);
        queueConfig.EvtIoDefault = EvtWdfCreateRequestsQueueIoDefault;

        status = WdfIoQueueCreate(device,
            &queueConfig,
            WDF_NO_OBJECT_ATTRIBUTES,
            &deviceContext->CreateRequestsQueue
        );
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfIoQueueCreate (CreateRequestsQueue) failed with %!STATUS!", status);
            return status;
        }

        status = WdfDeviceConfigureRequestDispatching(device, 
            deviceContext->CreateRequestsQueue, 
            WdfRequestTypeCreate
        );
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfDeviceConfigureRequestDispatching failed with %!STATUS!", status);
            return status;
        }

#pragma endregion

        //
        // Add this device to the FilterDevice collection.
        //
        WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);
        //
        // WdfCollectionAdd takes a reference on the item object and removes
        // it when you call WdfCollectionRemove.
        //
        status = WdfCollectionAdd(FilterDeviceCollection, device);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "WdfCollectionAdd failed with status %!STATUS!", status);
        }
        WdfWaitLockRelease(FilterDeviceCollectionLock);

        //
        // Create a control device
        //
        status = HidGuardianCreateControlDevice(device);
        if (!NT_SUCCESS(status)) {
            TraceEvents(TRACE_LEVEL_ERROR,
                TRACE_DEVICE,
                "HidGuardianCreateControlDevice failed with status %!STATUS!", status);

            return status;
        }

        //
        // Check if this device should get intercepted
        // 
        status = AmIAffected(deviceContext);

        TraceEvents(TRACE_LEVEL_INFORMATION,
            TRACE_DEVICE,
            "AmIAffected status %!STATUS!", status);

        //
        // Fetch default action to take what to do if service isn't available
        // from registry key.
        // 
        GetDefaultAction(deviceContext);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");

    return status;
}

#pragma warning(push)
#pragma warning(disable:28118) // this callback will run at IRQL=PASSIVE_LEVEL
_Use_decl_annotations_
VOID
HidGuardianEvtDeviceContextCleanup(
    WDFOBJECT Device
)
/*++

Routine Description:

EvtDeviceRemove event callback must perform any operations that are
necessary before the specified device is removed. The framework calls
the driver's EvtDeviceRemove callback when the PnP manager sends
an IRP_MN_REMOVE_DEVICE request to the driver stack.

Arguments:

Device - Handle to a framework device object.

Return Value:

WDF status code

--*/
{
    ULONG               count;
    PDEVICE_CONTEXT     pDeviceCtx;

    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    pDeviceCtx = DeviceGetContext(Device);

    PID_LIST_DESTROY(&pDeviceCtx->StickyPidList);

    WdfWaitLockAcquire(FilterDeviceCollectionLock, NULL);

    count = WdfCollectionGetCount(FilterDeviceCollection);

    if (count == 1)
    {
        //
        // We are the last instance. So let us delete the control-device
        // so that driver can unload when the FilterDevice is deleted.
        // We absolutely have to do the deletion of control device with
        // the collection lock acquired because we implicitly use this
        // lock to protect ControlDevice global variable. We need to make
        // sure another thread doesn't attempt to create while we are
        // deleting the device.
        //
        HidGuardianDeleteControlDevice((WDFDEVICE)Device);
    }

    WdfCollectionRemove(FilterDeviceCollection, Device);

    WdfWaitLockRelease(FilterDeviceCollectionLock);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}
#pragma warning(pop) // enable 28118 again

_Use_decl_annotations_
VOID
EvtWdfCreateRequestsQueueIoDefault(
    WDFQUEUE  Queue,
    WDFREQUEST  Request
)
{
    NTSTATUS                            status;
    WDFDEVICE                           device;
    PCONTROL_DEVICE_CONTEXT             pControlCtx;
    WDFREQUEST                          invertedCall;
    size_t                              bufferLength = 0;
    PHIDGUARDIAN_GET_CREATE_REQUEST     pGetCreateRequest;
    ULONG                               index;
    DWORD                               pid;
    PDEVICE_CONTEXT                     pDeviceCtx;
    WDF_OBJECT_ATTRIBUTES               requestAttribs;
    PCREATE_REQUEST_CONTEXT             pRequestCtx = NULL;
    ULONG                               hwidBufferLength;
    BOOLEAN                             ret;
    WDF_REQUEST_SEND_OPTIONS            options;
    BOOLEAN                             allowed;
    LONGLONG                            lockTimeout = WDF_REL_TIMEOUT_IN_US(10);


    PAGED_CODE();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");

    device = WdfIoQueueGetDevice(Queue);
    pControlCtx = ControlDeviceGetContext(ControlDevice);
    pDeviceCtx = DeviceGetContext(device);
    pid = CURRENT_PROCESS_ID();

    TraceEvents(TRACE_LEVEL_VERBOSE,
        TRACE_DEVICE,
        ">> Current PID: %d",
        pid);

    //
    // Check PID against internal list to speed up validation
    // 
    if (PID_LIST_CONTAINS(&pDeviceCtx->StickyPidList, pid, &allowed)) {
        TraceEvents(TRACE_LEVEL_INFORMATION,
            TRACE_DEVICE,
            "Request belongs to sticky PID %d, processing",
            pid);

        if (allowed) {
            //
            // Sticky PID allowed, forward request instantly
            // 
            goto allowAccess;
        }
        else {
            //
            // Sticky PID denied, fail request instantly
            // 
            goto blockAccess;
        }
    }

    //
    // Skip checks if no decision-maker is present
    // 
    if (!pControlCtx->IsServicePresent) {
        goto defaultAction;
    }

    //
    // Get inverted call to communicate with the user-mode application
    // 
    status = WdfIoQueueRetrieveNextRequest(pControlCtx->InvertedCallQueue, &invertedCall);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_WARNING,
            TRACE_DEVICE,
            "WdfIoQueueRetrieveNextRequest failed with status %!STATUS!", status);

        goto defaultAction;
    }

    //
    // Get buffer of request
    // 
    status = WdfRequestRetrieveOutputBuffer(
        invertedCall,
        sizeof(HIDGUARDIAN_GET_CREATE_REQUEST),
        (void*)&pGetCreateRequest,
        &bufferLength);

    //
    // Validate output buffer
    // 
    if (!NT_SUCCESS(status) || bufferLength != pGetCreateRequest->Size)
    {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "Packet size mismatch: %d != %d", (ULONG)bufferLength, pGetCreateRequest->Size);

        WdfRequestCompleteWithInformation(invertedCall, status, bufferLength);

        //
        // There request data buffer is malformed, skip
        // 
        goto defaultAction;
    }

#pragma region HOLDING LOCK

    status = WdfWaitLockAcquire(FilterDeviceCollectionLock, &lockTimeout);

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "Couldn't acquire device collection lock in time");

        goto defaultAction;
    }

    //
    // Search for our device in the collection to get index
    // 
    for (
        index = 0;
        index < WdfCollectionGetCount(FilterDeviceCollection);
        index++
        )
    {
        //
        // Assign request and device details to inverted call
        // 
        if (WdfCollectionGetItem(FilterDeviceCollection, index) == device)
        {
            TraceEvents(TRACE_LEVEL_INFORMATION,
                TRACE_DEVICE,
                "Request ID: %d",
                pGetCreateRequest->RequestId);

            pGetCreateRequest->DeviceIndex = index;

            TraceEvents(TRACE_LEVEL_INFORMATION,
                TRACE_DEVICE,
                "Found our device at index %d",
                pGetCreateRequest->DeviceIndex);

            pGetCreateRequest->ProcessId = pid;

            TraceEvents(TRACE_LEVEL_INFORMATION,
                TRACE_DEVICE,
                "PID associated to this request: %d",
                pGetCreateRequest->ProcessId);

            hwidBufferLength = pGetCreateRequest->Size - sizeof(HIDGUARDIAN_GET_CREATE_REQUEST);

            TraceEvents(TRACE_LEVEL_VERBOSE,
                TRACE_DEVICE,
                "Size for string: %d", hwidBufferLength);

            if (hwidBufferLength >= pDeviceCtx->HardwareIDsLength)
            {
                RtlCopyMemory(
                    pGetCreateRequest->HardwareIds,
                    pDeviceCtx->HardwareIDs,
                    pDeviceCtx->HardwareIDsLength
                );
            }

            break;
        }
    }

    WdfWaitLockRelease(FilterDeviceCollectionLock);

#pragma endregion

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&requestAttribs, CREATE_REQUEST_CONTEXT);
    requestAttribs.ParentObject = Request;

    //
    // Add custom context to request so we can validate it later
    // 
    status = WdfObjectAllocateContext(
        Request,
        &requestAttribs,
        (PVOID)&pRequestCtx
    );

    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfObjectAllocateContext failed with status %!STATUS!", status);

        WdfRequestCompleteWithInformation(invertedCall, status, bufferLength);

        goto defaultAction;
    }

    //
    // Pass identification information to request context
    // 
    pRequestCtx->ProcessId = pGetCreateRequest->ProcessId;
    pRequestCtx->RequestId = pGetCreateRequest->RequestId;

    //
    // Information has been passed to user-land, queue this request for 
    // later confirmation (or block) action.
    // 
    status = WdfRequestForwardToIoQueue(Request, pDeviceCtx->PendingAuthQueue);
    if (!NT_SUCCESS(status)) {
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfRequestForwardToIoQueue failed with status %!STATUS!", status);

        WdfRequestCompleteWithInformation(invertedCall, status, bufferLength);

        goto defaultAction;
    }

    //
    // Complete inverted call. Now it's up to the user-mode service
    // to decide what to do and invoke another IRP
    // 
    WdfRequestCompleteWithInformation(invertedCall, STATUS_SUCCESS, bufferLength);

    //
    // Request is queued and pending, we're done here
    // 

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit (access pending)");

    return;

defaultAction:

    if (pDeviceCtx->AllowByDefault) {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Default action requested: allow");
        goto allowAccess;
    }
    else {
        TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "Default action requested: deny");
        goto blockAccess;
    }

allowAccess:

    WdfRequestFormatRequestUsingCurrentType(Request);

    //
    // Send request down the stack
    // 
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,
        WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);

    ret = WdfRequestSend(Request, WdfDeviceGetIoTarget(device), &options);

    if (ret == FALSE) {
        status = WdfRequestGetStatus(Request);
        TraceEvents(TRACE_LEVEL_ERROR,
            TRACE_DEVICE,
            "WdfRequestSend failed: %!STATUS!", status);
        WdfRequestComplete(Request, status);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit (access granted)");

    return;

blockAccess:

    //
    // If forwarding fails, fall back to blocking access
    // 
    WdfRequestComplete(Request, STATUS_ACCESS_DENIED);

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit (access blocked)");
}

_Use_decl_annotations_
VOID
EvtFileCleanup(
    WDFFILEOBJECT  FileObject
)
{
    WDFDEVICE                   device;
    PDEVICE_CONTEXT             pDeviceCtx;
    ULONG                       pid;
    PCONTROL_DEVICE_CONTEXT     pControlCtx;

    PAGED_CODE();

    device = WdfFileObjectGetDevice(FileObject);
    pDeviceCtx = DeviceGetContext(device);
    pControlCtx = ControlDeviceGetContext(ControlDevice);
    pid = CURRENT_PROCESS_ID();

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry (PID: %d)", pid);

    if (!pControlCtx->IsServicePresent && PID_LIST_REMOVE_BY_PID(&pDeviceCtx->StickyPidList, pid)) {
        TraceEvents(TRACE_LEVEL_INFORMATION,
            TRACE_DEVICE,
            "Our guardian service is gone, removed sticky PID: %d", pid);
    }

    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit");
}
