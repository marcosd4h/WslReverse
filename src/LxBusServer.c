#include "Functions.h"
#include "WslSession.h"
#include "WinInternal.h" // PEB and some defined expression
#include "LxBus.h" // For IOCTLs values
#include <stdio.h>

#ifndef STATUS_PRIVILEGE_NOT_HELD
#define STATUS_PRIVILEGE_NOT_HELD ((NTSTATUS)0xC0000061L)
#endif

#define LXBUS_SERVER_NAME "minibus"
#define MESSAGE_TO_SEND "Hello from LxBus Server\n\n"

typedef struct _PipePair {
    HANDLE Read;
    HANDLE Write;
} PipePair;

HRESULT LxBusServer(PWslSession* wslSession,
                    GUID* DistroID)
{
    HRESULT hRes;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatusBlock;
    LARGE_INTEGER ByteOffset = { 0 };
    char Buffer[100];
    HANDLE EventHandle = NULL, ServerHandle = NULL;

    // Create a event to sync read/write
    Status = ZwCreateEvent(&EventHandle,
                           EVENT_ALL_ACCESS,
                           NULL,
                           SynchronizationEvent,
                           TRUE);


    //
    // 1# Register a LxBus server
    //
    hRes = (*wslSession)->RegisterLxBusServer(wslSession,
                                              DistroID,
                                              LXBUS_SERVER_NAME,
                                              &ServerHandle);
    if (FAILED(hRes))
    {
        Log(hRes, L"RegisterLxBusServer");
        if (hRes == E_ACCESSDENIED)
            wprintf(L"Run this program as administrator...\n");
        return hRes;
    }

    // Wait for connection from LxBus client infinitely
    LXBUS_IPC_SERVER_WAIT_FOR_CONNECTION_MSG WaitMsg = { 0 };
    WaitMsg.Timeout = INFINITE;
    Status = ZwDeviceIoControlFile(ServerHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_SERVER_WAIT_FOR_CONNECTION,
                                   &WaitMsg, sizeof(WaitMsg),
                                   &WaitMsg, sizeof(WaitMsg));

    if (NT_SUCCESS(Status))
        wprintf(L"[*] LxBus ServerHandle: %ld ClientHandle: %ld\n",
                ToULong(ServerHandle), WaitMsg.ClientHandle);


    //
    // 2# Read message from LxBus client
    //
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwReadFile(ToHandle(WaitMsg.ClientHandle),
                        EventHandle,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        Buffer,
                        sizeof(Buffer),
                        &ByteOffset,
                        NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);

    printf("\nMessage from client:\n%s", Buffer);


    //
    // 3# Write message to LxBus client
    //
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         MESSAGE_TO_SEND,
                         sizeof(MESSAGE_TO_SEND),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);


    //
    // 4# Marshal write end of pipe
    //
    PipePair pipePairA = { NULL };
    CreatePipe(&pipePairA.Read, &pipePairA.Write, NULL, 0);

    LXBUS_IPC_MESSAGE_MARSHAL_HANDLE_DATA HandleMsgA = { 0 };
    HandleMsgA.Handle = ToULong(pipePairA.Write);
    HandleMsgA.Type = LxOutputPipeType;

    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_MARSHAL_HANDLE,
                                   &HandleMsgA, sizeof(HandleMsgA),
                                   &HandleMsgA, sizeof(HandleMsgA));
    if (NT_SUCCESS(Status))
        wprintf(L"HandleMsgA.HandleIdCount: %lld\n", HandleMsgA.HandleIdCount);

    // Write the HandleIdCount so that LxBus client can unmarshal
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &HandleMsgA.HandleIdCount,
                         sizeof(HandleMsgA.HandleIdCount),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);

    // Read message from pipe handle
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Status = ZwReadFile(pipePairA.Read,
                        EventHandle,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        Buffer,
                        sizeof(Buffer),
                        &ByteOffset,
                        NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);

    printf("\nMessage from pipe:\n%s", Buffer);


    //
    // 5# Marshal read end of pipe
    //
    PipePair pipePairB = { NULL };
    CreatePipe(&pipePairB.Read, &pipePairB.Write, NULL, 0);

    LXBUS_IPC_MESSAGE_MARSHAL_HANDLE_DATA HandleMsgB = { 0 };
    HandleMsgB.Handle = ToULong(pipePairB.Read);
    HandleMsgB.Type = LxInputPipeType;

    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_MARSHAL_HANDLE,
                                   &HandleMsgB, sizeof(HandleMsgB),
                                   &HandleMsgB, sizeof(HandleMsgB));
    if (NT_SUCCESS(Status))
        wprintf(L"HandleMsgB.HandleIdCount: %lld\n", HandleMsgB.HandleIdCount);

    // Write the HandleIdCount so that LxBus client can unmarshal
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &HandleMsgB.HandleIdCount,
                         sizeof(HandleMsgB.HandleIdCount),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);

    // Write message to pipe handle
    Status = ZwWriteFile(pipePairB.Write,
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         MESSAGE_TO_SEND,
                         sizeof(MESSAGE_TO_SEND),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);


    //
    // 6# Unmarshal standard I/O file descriptors
    //
    LXBUS_IPC_MESSAGE_MARSHAL_VFSFILE_MSG VfsMsg = { 0 };
    for (int i = 0; i < TOTAL_IO_HANDLES; i++)
    {
        // Read the marshalled fd
        Status = ZwReadFile(ToHandle(WaitMsg.ClientHandle),
                            EventHandle,
                            NULL,
                            NULL,
                            &IoStatusBlock,
                            &VfsMsg.HandleIdCount,
                            sizeof(VfsMsg.HandleIdCount),
                            &ByteOffset,
                            NULL);
        if (Status == STATUS_PENDING)
            Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            wprintf(L"VfsMsg.HandleIdCount: %lld\n", VfsMsg.HandleIdCount);

        // Unmarshal it
        Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                       NULL,
                                       NULL,
                                       NULL,
                                       &IoStatusBlock,
                                       IOCTL_LXBUS_IPC_CONNECTION_UNMARSHAL_VFS_FILE,
                                       &VfsMsg, sizeof(VfsMsg),
                                       &VfsMsg, sizeof(VfsMsg));
        if (NT_SUCCESS(Status))
            wprintf(L"VfsMsg.Handle: %ld\n", ToULong(VfsMsg.Handle));
    }


    //
    // 7# Unmarshal pid from client side
    //
    LXBUS_IPC_MESSAGE_MARSHAL_PROCESS_MSG ProcessMsg = { 0 };

    // Read ProcessIdCount from client side
    Status = ZwReadFile(ToHandle(WaitMsg.ClientHandle),
                        EventHandle,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        &ProcessMsg.ProcessIdCount,
                        sizeof(ProcessMsg.ProcessIdCount),
                        &ByteOffset,
                        NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);
    if (NT_SUCCESS(Status))
        wprintf(L"ProcessMsg.ProcessIdCount: %lld\n", ProcessMsg.ProcessIdCount);

    // Unmarshal it
    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_UNMARSHAL_PROCESS,
                                   &ProcessMsg, sizeof(ProcessMsg),
                                   &ProcessMsg, sizeof(ProcessMsg));
    if (NT_SUCCESS(Status))
        wprintf(L"ProcessMsg.ProcessHandle: %ld\n", ToULong(ProcessMsg.ProcessHandle));


    //
    // 8# Create a session leader from any process handle
    //
    HANDLE ProcessHandle = NULL;
    Status = ZwDuplicateObject(ZwCurrentProcess(),
                               ZwCurrentProcess(),
                               ZwCurrentProcess(),
                               &ProcessHandle,
                               0,
                               OBJ_INHERIT,
                               DUPLICATE_SAME_ACCESS);

    LXBUS_IPC_MESSAGE_MARSHAL_CONSOLE_MSG ConsoleMsg = { 0 };
    ConsoleMsg.Handle = ToULong(ProcessHandle);

    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_MARSHAL_CONSOLE,
                                   &ConsoleMsg, sizeof(ConsoleMsg),
                                   &ConsoleMsg, sizeof(ConsoleMsg));
    if (NT_SUCCESS(Status))
        wprintf(L"ConsoleMsg.ConsoleIdCount: %lld\n", ConsoleMsg.ConsoleIdCount);

    // Write ConsoleIdCount so that LxBus client can unmarshal it
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &ConsoleMsg.ConsoleIdCount,
                         sizeof(ConsoleMsg.ConsoleIdCount),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);


    //
    // 9# Create unnamed LxBus server
    //
    LXBUS_IPC_CONNECTION_CREATE_UNNAMED_SERVER_MSG UnnamedServerMsg = { 0 };

    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_CREATE_UNNAMED_SERVER,
                                   NULL, 0,
                                   &UnnamedServerMsg, sizeof(UnnamedServerMsg));
    if (NT_SUCCESS(Status))
        wprintf(L"UnnamedServerMsg.ServerPortIdCount: %lld\n", UnnamedServerMsg.ServerPortIdCount);

    // Write ServerPortIdCount so that LxBus client can unmarshal it
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &UnnamedServerMsg.ServerPortIdCount,
                         sizeof(UnnamedServerMsg.ServerPortIdCount),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);
    // To be continued ...


    //
    // 10# Marshal fork token
    //
    ULONG Privilege = 3;
    PULONG_PTR ReturnedState = NULL;
    Status = RtlAcquirePrivilege(&Privilege, 1, 0, &ReturnedState);

    if (!NT_SUCCESS(Status))
    {
        wprintf(L"RtlAcquirePrivilege Status: 0x%08X\n", Status);
        if (Status == STATUS_PRIVILEGE_NOT_HELD)
            wprintf(L"Enable \"Replace a process level token\" privilege in Group Policy...\n");
    }

    LXBUS_IPC_CONNECTION_MARSHAL_FORK_TOKEN_MSG TokenMsg = { 0 };
    TokenMsg.Handle = ToULong(*ReturnedState);

    Status = ZwDeviceIoControlFile(ToHandle(WaitMsg.ClientHandle),
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_LXBUS_IPC_CONNECTION_MARSHAL_FORK_TOKEN,
                                   &TokenMsg, sizeof(TokenMsg),
                                   &TokenMsg, sizeof(TokenMsg));
    if (NT_SUCCESS(Status))
        wprintf(L"TokenMsg.TokenIdCount: %lld\n", TokenMsg.TokenIdCount);

    // Write TokenIdCount so that LxBus client can unmarshal it
    Status = ZwWriteFile(ToHandle(WaitMsg.ClientHandle),
                         EventHandle,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         &TokenMsg.TokenIdCount,
                         sizeof(TokenMsg.TokenIdCount),
                         &ByteOffset,
                         NULL);
    if (Status == STATUS_PENDING)
        Status = ZwWaitForSingleObject(EventHandle, FALSE, NULL);


    Sleep(1000);

    // Cleanup
    ZwClose(ProcessHandle);
    RtlReleasePrivilege(ReturnedState);
    ZwClose(pipePairA.Read);
    ZwClose(pipePairA.Write);
    ZwClose(pipePairB.Read);
    ZwClose(pipePairB.Write);
    ZwClose(EventHandle);
    ZwClose(ServerHandle);
    ZwClose(ToHandle(WaitMsg.ClientHandle));
    return Status;
}
