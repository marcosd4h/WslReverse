#include "LxssDevice.h"
#include "WslSession.h"
#include "Functions.h"
#include "CreateLxProcess.h"
#include "LxBusServer.h"
#include "wgetopt.h"
#include <stdio.h>

int main(void)
{
    int wargc;
    PWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);

    if (wargc < 2)
    {
        wprintf(L"Try 'WslReverse.exe --help' for more information.\n");
        return 0;
    }

    // Declare variables
    int c;
    HRESULT hRes = 0;
    ULONG Version, DefaultUid, Flags, EnvironmentCount;
    GUID DistroId = { 0 }, DefaultDistroId = { 0 };
    wchar_t GuidString[GUID_STRING];
    HANDLE hTarFile = NULL;
    PWslSession* wslSession = NULL;

    // Option table
    const wchar_t* OptionString = L"b:d:e:Gg:hi:lr:S:s:t:u:x";

    const struct option OptionTable[] = {
        { L"bus",           required_argument,   0,  'b' },
        { L"get-id",        required_argument,   0,  'd' },
        { L"export",        required_argument,   0,  'e' },
        { L"get-default",   no_argument,         0,  'G' },
        { L"get-config",    required_argument,   0,  'g' },
        { L"help",          no_argument,         0,  'h' },
        { L"install",       required_argument,   0,  'i' },
        { L"list",          no_argument,         0,  'l' },
        { L"run",           required_argument,   0,  'r' },
        { L"set-default",   required_argument,   0,  'S' },
        { L"set-config",    required_argument,   0,  's' },
        { L"terminate",     required_argument,   0,  't' },
        { L"uninstall",     required_argument,   0,  'u' },
        { L"xpert",         no_argument,         0,  'x' },
        { 0,                no_argument,         0,   0  },
    };

    hRes = CoInitializeEx(0, COINIT_MULTITHREADED);
    hRes = CoInitializeSecurity(0, -1, 0, 0, RPC_C_AUTHN_LEVEL_DEFAULT, SecurityDelegation, 0, EOAC_STATIC_CLOAKING, 0);
    hRes = CoCreateInstance(&CLSID_LxssUserSession, 0, CLSCTX_LOCAL_SERVER, &IID_ILxssUserSession, (PVOID*)&wslSession);
    Log(hRes, L"CoCreateInstance");
    if (!wslSession)
        return 0;

    // Option parsing
    while ((c = wgetopt_long(wargc, wargv, OptionString, OptionTable, 0)) != -1)
    {
        switch (c)
        {
        case 0:
        {
            wprintf(L"Try 'WslReverse.exe --help' for more information.\n");
            Usage();
            break;
        }
        case 'b':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->CreateInstance(wslSession, &DistroId, 0);
            Log(hRes, L"CreateInstance");
            if (hRes < 0)
                return hRes;
            hRes = LxBusServer(wslSession, &DistroId);
            break;
        }
        case 'd':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DefaultDistroId);
            PrintGuid(&DefaultDistroId, GuidString);
            Log(hRes, L"GetDistributionId");
            wprintf(L"%ls: %ls\n", optarg, GuidString);
            break;
        }
        case 'e':
        {
            WCHAR TarFilePath[MAX_PATH];
            wprintf(L"Enter filename or full path of exported tar file (without quote): ");
            wscanf_s(L"%ls", TarFilePath, MAX_PATH);

            hTarFile = CreateFileW(
                TarFilePath,
                GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_DELETE,
                NULL,
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);

            if (GetFileType(hTarFile) == FILE_TYPE_PIPE)
            {
                // hTarFile = GetStdHandle(STD_OUTPUT_HANDLE)
                hRes = (*wslSession)->ExportDistributionFromPipe(wslSession, &DistroId, hTarFile);
                Log(hRes, L"ExportDistributionFromPipe");
            }
            else
            {
                hRes = (*wslSession)->ExportDistribution(wslSession, &DistroId, hTarFile);
                Log(hRes, L"ExportDistribution");
            }

            CloseHandle(hTarFile);
            break;
        }
        case 'G':
        {
            hRes = (*wslSession)->GetDefaultDistribution(wslSession, &DistroId);
            Log(hRes, L"GetDefaultDistribution");
            PrintGuid(&DistroId, GuidString);
            wprintf(L"%ls\n", GuidString);
            break;
        }
        case 'g':
        {
            PWSTR DistributionName, BasePath;
            PSTR KernelCommandLine;
            PSTR* DefaultEnvironment = NULL;

            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->GetDistributionConfiguration(
                wslSession, &DistroId, &DistributionName, &Version, &BasePath,
                &KernelCommandLine, &DefaultUid, &EnvironmentCount, &DefaultEnvironment, &Flags
            );
            Log(hRes, L"GetDistributionConfiguration");
            printf(
                "\n Distribution Name: %ls\n Version: %lu\n BasePath: %ls\n KernelCommandLine: %s\n"
                " DefaultUID: %lu\n EnvironmentCount: %lu\n DefaultEnvironment: %s\n Flags: %lu\n"
                , DistributionName, Version, BasePath, KernelCommandLine,
                DefaultUid, EnvironmentCount, *DefaultEnvironment, Flags
            );
            break;
        }
        case 'h':
        {
            Usage();
            break;
        }
        case 'i':
        {
            wchar_t TarFilePath[MAX_PATH], BasePath[MAX_PATH];
            wprintf(L"Enter full path of .tar.gz file (without quote): ");
            wscanf_s(L"%ls", TarFilePath, MAX_PATH);

            wprintf(L"Enter folder path where to install (without quote): ");
            wscanf_s(L"%ls", BasePath, MAX_PATH);

            hRes = CoCreateGuid(&DistroId);

            hTarFile = CreateFileW(
                TarFilePath,
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_DELETE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL);

            if (GetFileType(hTarFile) == FILE_TYPE_PIPE)
            {
                // hTarFile = GetStdHandle(STD_INPUT_HANDLE)
                hRes = (*wslSession)->RegisterDistributionFromPipe(
                    wslSession, optarg, ToBeInstall, hTarFile, BasePath, &DistroId);
                Log(hRes, L"RegisterDistributionFromPipe");
            }
            else
            {
                hRes = (*wslSession)->RegisterDistribution(
                    wslSession, optarg, ToBeInstall, hTarFile, BasePath, &DistroId);
                Log(hRes, L"RegisterDistribution");
            }

            CloseHandle(hTarFile);

            if(hRes == S_OK)
                wprintf(L"Installed\n");
            break;
        }
        case 'l':
        {
            GUID* DistroIdList = NULL;
            ULONG DistroCount = 0;
            hRes = (*wslSession)->GetDefaultDistribution(wslSession, &DefaultDistroId);
            hRes = (*wslSession)->EnumerateDistributions(wslSession, FALSE, &DistroCount, &DistroIdList);

            if (DistroCount)
            {
                ULONG i = 0;
                PWSTR DistributionName, BasePath;
                PSTR KernelCommandLine;
                PSTR* DefaultEnvironment = NULL;

                wprintf(L"\nWSL Distributions:\n");
                do
                {
                    DistroId = DistroIdList[i];
                    if (DistroId.Data1 == DefaultDistroId.Data1 &&
                        (DistroId.Data4 - DefaultDistroId.Data4))
                    {
                        hRes = (*wslSession)->GetDistributionConfiguration(
                            wslSession, &DistroId, &DistributionName, &Version, &BasePath,
                            &KernelCommandLine, &DefaultUid, &EnvironmentCount, &DefaultEnvironment, &Flags);
                        PrintGuid(&DistroId, GuidString);
                        wprintf(L"%ls : %ls (Default)\n", GuidString, DistributionName);
                    }
                    else
                    {
                        hRes = (*wslSession)->GetDistributionConfiguration(
                            wslSession, &DistroId, &DistributionName, &Version, &BasePath,
                            &KernelCommandLine, &DefaultUid, &EnvironmentCount, &DefaultEnvironment, &Flags);
                        PrintGuid(&DistroId, GuidString);
                        wprintf(L"%ls : %ls\n", GuidString, DistributionName);
                    }
                    ++i;
                } while (i < DistroCount);
            }
            else
            {
                wprintf(L"No Distribution installed.\n");
            }
            break;
        }
        case 'r':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->CreateInstance(wslSession, &DistroId, 0);
            Log(hRes, L"CreateInstance");
            if (hRes < 0)
                return hRes;
            hRes = CreateLxProcess(wslSession, &DistroId);
            break;
        }
        case 'S':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->SetDefaultDistribution(wslSession, &DistroId);
            Log(hRes, L"SetDefaultDistribution");
            break;
        }
        case 's':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            PCSTR KernelCommandLine = "BOOT_IMAGE=/kernel init=/init ro";
            PCSTR DefaultEnvironment[4] = { 
                "HOSTTYPE=x86_64",
                "LANG=en_US.UTF-8",
                "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:/usr/games:/usr/local/games",
                "TERM=xterm-256color"
            };
            hRes = (*wslSession)->ConfigureDistribution(wslSession, &DistroId, KernelCommandLine,
                RootUser, ARRAY_SIZE(DefaultEnvironment), DefaultEnvironment, WSL_DISTRIBUTION_FLAGS_DEFAULT);
            Log(hRes, L"ConfigureDistribution");
            break;
        }
        case 't':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->TerminateDistribution(wslSession, &DistroId);
            Log(hRes, L"TerminateDistribution");
            break;
        }
        case 'u':
        {
            hRes = (*wslSession)->GetDistributionId(wslSession, optarg, Installed, &DistroId);
            Log(hRes, L"GetDistributionId");
            hRes = (*wslSession)->UnregisterDistribution(wslSession, &DistroId);
            Log(hRes, L"UnregisterDistribution");
            break;
        }
        case 'x':
        {
            LxssDevice();
            break;
        }
        default:
            wprintf(L"Try 'WslReverse.exe --help' for more information.\n");
        }
    }

    (*wslSession)->Release(wslSession);
    return 0;
}
