#pragma once


// {0C320FF7-BD9B-42B6-BDAF-49FEB9C91649}
DEFINE_GUID(GUID_DEVINTERFACE_HIDGUARDIAN,
    0xc320ff7, 0xbd9b, 0x42b6, 0xbd, 0xaf, 0x49, 0xfe, 0xb9, 0xc9, 0x16, 0x49);

#define CONTROL_DEVICE_PATH         L"\\\\.\\HidGuardian"
#define NTDEVICE_NAME_STRING        L"\\Device\\HidGuardian"
#define SYMBOLIC_NAME_STRING        L"\\DosDevices\\HidGuardian"

#define IOCTL_INDEX                 0x901
#define FILE_DEVICE_HIDGUARDIAN     32768U

//
// Used for inverted calls to get request information
// 
#define IOCTL_HIDGUARDIAN_GET_CREATE_REQUEST        CTL_CODE(FILE_DEVICE_HIDGUARDIAN,   \
                                                                    IOCTL_INDEX + 0x00, \
                                                                    METHOD_BUFFERED,    \
                                                                    FILE_READ_ACCESS | FILE_WRITE_ACCESS)

//
// Used to instruct driver to allow or deny request
// 
#define IOCTL_HIDGUARDIAN_SET_CREATE_REQUEST        CTL_CODE(FILE_DEVICE_HIDGUARDIAN,   \
                                                                    IOCTL_INDEX + 0x01, \
                                                                    METHOD_BUFFERED,    \
                                                                    FILE_WRITE_ACCESS)

#define IOCTL_HIDGUARDIAN_REGISTER_CERBERUS         CTL_CODE(FILE_DEVICE_HIDGUARDIAN,   \
                                                                    IOCTL_INDEX + 0x02, \
                                                                    METHOD_BUFFERED,    \
                                                                    FILE_ANY_ACCESS)


#include <pshpack1.h>

#pragma warning(push)
#pragma warning(disable:4200) // disable warnings for structures with zero length arrays.
typedef struct _HIDGUARDIAN_GET_CREATE_REQUEST
{
    //
    // Size of packet
    // 
    IN ULONG Size;

    //
    // Arbitrary value to match request and response
    // 
    IN ULONG RequestId;

    //
    // ID of the process this request is related to
    // 
    OUT ULONG ProcessId;

    //
    // Buffer containing Hardware ID string
    // 
    OUT WCHAR HardwareIds[];

} HIDGUARDIAN_GET_CREATE_REQUEST, *PHIDGUARDIAN_GET_CREATE_REQUEST;
#pragma warning(pop)

typedef struct _HIDGUARDIAN_SET_CREATE_REQUEST
{
    //
    // Arbitrary value to match request and response
    // 
    IN ULONG RequestId;

    //
    // TRUE if WdfRequestTypeCreate is allowed, FALSE otherwise
    // 
    IN BOOLEAN IsAllowed;

    //
    // If TRUE, the decision will be cached in the driver for the current PID
    // 
    IN BOOLEAN IsSticky;

} HIDGUARDIAN_SET_CREATE_REQUEST, *PHIDGUARDIAN_SET_CREATE_REQUEST;

#include <poppack.h>
