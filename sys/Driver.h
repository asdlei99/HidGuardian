/*
* Windows kernel-mode driver for controlling access to various input devices.
*
* MIT License
*
* Copyright (c) 2016-2019 Nefarius Software Solutions e.U.
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/


#define INITGUID

#include <ntddk.h>
#include <wdf.h>

#include "HidGuardian.h"
#include "PidList.h"
#include "Sideband.h"
#include "device.h"
#include "queue.h"
#include "Guardian.h"
#include "trace.h"

#define DRIVERNAME "HidGuardian: "

WDFCOLLECTION   FilterDeviceCollection;
WDFWAITLOCK     FilterDeviceCollectionLock;
WDFDEVICE       ControlDevice;

EXTERN_C_START

//
// WDFDRIVER Events
//

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD HidGuardianEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP HidGuardianEvtDriverContextCleanup;

EXTERN_C_END
