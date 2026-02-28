#include    "TapDev.hpp"
#include    "LogMgr.hpp"
#include    "TapLan.hpp"

#define     cfgData                                 TapLan::config_
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Signatures\Unmanaged
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Profiles
#define     ADAPTER_KEY                             "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define     NETWORK_CONNECTIONS_KEY                 "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define     TAP_INSTALL                             ".\\tapinstall.exe"
#define     USERMODEDEVICEDIR                       "\\\\.\\Global\\"
#define     TAPSUFFIX                               ".tap"
#define     TAP_CONTROL_CODE(request, method)       CTL_CODE(FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)
#define     TAP_IOCTL_GET_MAC                       TAP_CONTROL_CODE(1, METHOD_BUFFERED)
#define     TAP_IOCTL_SET_MEDIA_STATUS              TAP_CONTROL_CODE(6, METHOD_BUFFERED)

WinAdapterInfo tapInfo;
OVERLAPPED overlapRead{}, overlapWrite{};
TapFd tapFd = nullptr;
static const char* TAG = "[TapDev]";

static std::string getCurrentWorkDir() {
    namespace fs = std::filesystem;
    try {
        return fs::current_path().string();
    } catch (const fs::filesystem_error& e) {
        return "";
    }
}

static bool findExistedTap() {
    bool ret = false;
    LONG err;
    std::string errMsg;

    HKEY openKey0;
    err = RegOpenKeyExA(HKEY_LOCAL_MACHINE, NETWORK_CONNECTIONS_KEY, 0, KEY_READ, &openKey0);
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGT(TAG, "Failed to open %s.[%s]", NETWORK_CONNECTIONS_KEY, errMsg.c_str());
        return ret;
    }

    for (DWORD idx = 0; ; ++idx) {
        WinAdapterInfo adapterInfo;

        err = RegEnumKeyExA(openKey0, idx, adapterInfo.netCfgInstId, &adapterInfo.netInstIdLen, nullptr, nullptr, nullptr, nullptr);
        if (err != ERROR_SUCCESS) {
            if (err != ERROR_NO_MORE_ITEMS) {
                errMsg = getErrMsg(err);
                LOGT(TAG, "Failed to enum %s.[%s]", NETWORK_CONNECTIONS_KEY, errMsg.c_str());
            }
            break;
        }

        std::stringstream regPath;
        regPath << NETWORK_CONNECTIONS_KEY << "\\" << adapterInfo.netCfgInstId << "\\Connection";

        HKEY openKey1;
        err = RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath.str().c_str(), 0, KEY_READ, &openKey1);
        if (err != ERROR_SUCCESS) {
            if (err != ERROR_FILE_NOT_FOUND) {
                errMsg = getErrMsg(err);
                LOGT(TAG, "Failed to open %s.[%s]", regPath.str().c_str(), errMsg.c_str());
            }
            continue;
        }

        // FIXME: use "MyProgramName" instead of "Name" to check will be better
        err = RegGetValueA(openKey1, nullptr, "Name", RRF_RT_REG_SZ, nullptr, adapterInfo.name, &adapterInfo.nameLen);
        RegCloseKey(openKey1);
        if (err) {
            LOGT(TAG, "Failed to get Name from %s.[%s]", regPath.str().c_str(), errMsg.c_str());
            continue;
        }

        if (0 != strcmp(TAP_NAME, (const char*)adapterInfo.name))
            continue;
        LOGT(TAG, "NetCfgInstanceId: [%s]", adapterInfo.netCfgInstId);

        memcpy(&tapInfo, &adapterInfo, sizeof(WinAdapterInfo));
        ret = true;
        break;
    }

    RegCloseKey(openKey0);
    return ret;
}

static bool createNewTap(Mac mac) {
    bool ret = false;
    LONG err;
    std::string errMsg;
    DWORD64 startTimestamp; {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(&startTimestamp, &ft, 8);
    }
    char cmd[BUFFER_SIZE];

    DWORD attributes = GetFileAttributesA(TAP_INSTALL);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(cmd, BUFFER_SIZE, "%s install OemVista.inf TAP0901", TAP_INSTALL);
        if (system(TAP_INSTALL " install OemVista.inf TAP0901")) {
            LOGE(TAG, "Failed to exec [%s].", cmd);
            return ret;
        }
    } else {
        LOGE(TAG, "Please place [%s] in [%s].", TAP_INSTALL, getCurrentWorkDir().c_str());
        return ret;
    }

    HKEY openKey0, openKey1;
    err = RegOpenKeyExA(HKEY_LOCAL_MACHINE, ADAPTER_KEY, 0, KEY_READ, &openKey0);
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGE(TAG, "Failed to open %s.[%s]", ADAPTER_KEY, errMsg.c_str());
        return false;
    }

    for (DWORD idx = 0; ; ++idx) {
        WinAdapterInfo adapterInfo;
        CHAR driverId[BUFFER_SIZE];
        DWORD driverIdLen = BUFFER_SIZE;
        err = RegEnumKeyExA(openKey0, idx, driverId, &driverIdLen, nullptr, nullptr, nullptr, nullptr);
        if (err != ERROR_SUCCESS) {
            if (err != ERROR_NO_MORE_ITEMS) {
                errMsg = getErrMsg(err);
                LOGE(TAG, "Failed to enum %s.[%s]", ADAPTER_KEY, errMsg.c_str());
            }
            break;
        }

        DWORD64 installTimestamp;
        DWORD installTimestampLen = sizeof(installTimestamp);
        err = RegGetValueA(openKey0, driverId, "NetworkInterfaceInstallTimestamp", RRF_RT_QWORD, nullptr, &installTimestamp, &installTimestampLen);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGT(TAG, "Failed to get NetworkInterfaceInstallTimestamp from %s.[%s]", driverId, errMsg.c_str());
            continue;
        } else if (installTimestamp < startTimestamp) {
            continue;
        }

        CHAR providerName[BUFFER_SIZE];
        DWORD providerNameLen = BUFFER_SIZE;
        err = RegGetValueA(openKey0, driverId, "ProviderName", RRF_RT_REG_SZ, nullptr, &providerName, &providerNameLen);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGT(TAG, "Failed to get ProviderName from %s.[%s]", driverId, errMsg.c_str());
            continue;
        } else if (strcmp("TAP-Windows Provider V9", (const char*)providerName) != 0) {
            continue;
        }
        LOGT(TAG, "DriverId: [%s]", driverId);

        err = RegGetValueA(openKey0, driverId, "DeviceInstanceID", RRF_RT_REG_SZ, nullptr, adapterInfo.devInstId, &adapterInfo.devInstIdLen);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGF(TAG, "Failed to get DeviceInstanceID from %s.[%s]", driverId, errMsg.c_str());
            break;
        }

        err = RegGetValueA(openKey0, driverId, "NetCfgInstanceId", RRF_RT_REG_SZ, nullptr, adapterInfo.netCfgInstId, &adapterInfo.netInstIdLen);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGF(TAG, "Failed to get NetCfgInstanceId from %s.[%s]", driverId, errMsg.c_str());
            break;
        }

        uint64_t macNum = static_cast<uint64_t>(mac);
        macNum = _byteswap_uint64(macNum);
        macNum >>= 16;
        std::stringstream macSs;
        macSs << std::hex << std::uppercase << std::setfill('0') << std::setw(12) << macNum;
        err = RegSetKeyValueA(openKey0, driverId, "NetworkAddress", REG_SZ, macSs.str().c_str(), macSs.str().length() + 1);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set NetworkAddress to %s.[%s]", macSs.str().c_str(), errMsg.c_str());
        }

        std::string mtu_size = std::to_string(1418);
        err = RegSetKeyValueA(openKey0, driverId, "MTU", REG_SZ, mtu_size.c_str(), mtu_size.length() + 1);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set MTU to %s.[%s]", mtu_size.c_str(), errMsg.c_str());
        }

        std::string myProgName = TAP_NAME;
        err = RegSetKeyValueA(openKey0, driverId, "MyProgramName", REG_SZ, myProgName.c_str(), myProgName.length() + 1);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set MyProgramName to %s.[%s]", myProgName.c_str(), errMsg.c_str());
        }

        snprintf(cmd, BUFFER_SIZE, "%s restart @%s", TAP_INSTALL, adapterInfo.devInstId);
        if (system(cmd)) {
            LOGF(TAG, "Failed to exec [%s].", cmd);
            break;
        }

        std::stringstream regPath;
        regPath << NETWORK_CONNECTIONS_KEY << "\\" << adapterInfo.netCfgInstId << "\\Connection";
        LOGT(TAG, "NetCfgInstanceId: [%s]", adapterInfo.netCfgInstId);

        err = RegOpenKeyExA(HKEY_LOCAL_MACHINE, regPath.str().c_str(), 0, KEY_READ | KEY_SET_VALUE, &openKey1);
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGF(TAG, "Failed to open %s.[%s]", regPath.str().c_str(), errMsg);
            RegCloseKey(openKey0);
            return false;
        }
        err = RegGetValueA(openKey1, nullptr, "Name", RRF_RT_REG_SZ, nullptr, adapterInfo.name, &adapterInfo.nameLen);
        RegCloseKey(openKey1);
        if (err) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to get Name from %s.[%s]", regPath.str().c_str(), errMsg.c_str());
            break;
        }
        snprintf(cmd, BUFFER_SIZE, "netsh interface set interface name=\"%s\" newname=\"%s\"", adapterInfo.name, TAP_NAME);
        if (system(cmd)) {
            LOGE(TAG, "Failed to exec [%s].", cmd);
            break;
        }
        strcpy((char*)adapterInfo.name, TAP_NAME);
        adapterInfo.nameLen = strlen(TAP_NAME) + 1;

        memcpy(&tapInfo, &adapterInfo, sizeof(WinAdapterInfo));
        ret = true;
        break;
    }

    RegCloseKey(openKey1);
    RegCloseKey(openKey0);
    return ret;
}

TapDev::TapDev(): fdValid_(false), mac_{},
                    writeErrs_(0), readErrs_(0) {
    generateMac();
    fdValid_ = open();
    overlapRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    overlapWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
}

TapDev::~TapDev() {
    close();
}

void TapDev::generateMac() {
    mac_.addr[0] = 0x02;
    mac_.addr[1] = 0x34;
    mac_.addr[2] = 0x60;

    auto now = std::chrono::high_resolution_clock::now();
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    uint32_t seed = static_cast<uint32_t>(micros ^ (getpid() << 16));
    mac_.addr[3] = (seed >> 16) & 0xFF;
    mac_.addr[4] = (seed >> 8) & 0xFF;
    mac_.addr[5] = seed & 0xFF;
}

bool TapDev::open() {
    std::string errMsg;

    if (!findExistedTap() && !createNewTap(mac_))
        return false;

    std::stringstream tapName;
    tapName << USERMODEDEVICEDIR << tapInfo.netCfgInstId << TAPSUFFIX;
    tapFd = CreateFileA(tapName.str().c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
    if (tapFd == INVALID_HANDLE_VALUE) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to open TAP device.[%s]", errMsg.c_str());
        return false;
    }

    if (!DeviceIoControl(tapFd, TAP_IOCTL_SET_MEDIA_STATUS,
                        &tapInfo.mediaStatus, tapInfo.mediaStatusLen,
                        &tapInfo.mediaStatus, tapInfo.mediaStatusLen,
                        &tapInfo.mediaStatusLen, nullptr)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to set status to up.[%s]", errMsg.c_str());
        return false;
    }

    memset(mac_.addr, 0, 6);
    DWORD macLen = sizeof(mac_);
    if (!DeviceIoControl(tapFd, TAP_IOCTL_GET_MAC,
                        mac_.addr, 6,
                        mac_.addr, 6,
                        &macLen, nullptr)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to get MAC address.[%s]", errMsg.c_str());
        return false;
    }

    return true;
}

bool TapDev::close() {
    CloseHandle(tapFd);
    CloseHandle(overlapRead.hEvent);
    CloseHandle(overlapWrite.hEvent);
    // if (system(TAP_INSTALL " remove TAP0901"))
    //     LOGE(TAG, "Removing tap device failed.");

    return true;
}

void TapDev::getMacAddr(Mac& mac) {
    memcpy(mac.addr, mac_.addr, sizeof(mac_));
}

bool TapDev::setIPv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen)
{
    std::stringstream cidr;
    cidr << inet_ntoa(*ipv4Addr) << "/" << +netIdLen;

    std::stringstream cmd;
    cmd << "netsh interface ip set address \"" << TAP_NAME << "\" static " << cidr.str();
    if (system(cmd.str().c_str())) {
        LOGE(TAG, "Failed to exec [%s].", cmd.str().c_str());
        return false;
    }
    LOGI(TAG, "%s IP address has been set to %s.", TAP_NAME, cidr.str().c_str());

    return true;
}

ssize_t TapDev::write(const void* buf, size_t bufLen) {
    static DWORD writeBytes;
    if (WriteFile(tapFd, buf, bufLen, &writeBytes, &overlapWrite)) {
        ResetEvent(overlapWrite.hEvent);
        return writeBytes;
    }

    DWORD err = GetLastError();
    if (err == ERROR_IO_PENDING) {
        GetOverlappedResult(tapFd, &overlapWrite, &writeBytes, TRUE);
        ResetEvent(overlapWrite.hEvent);
        if (writeBytes < bufLen) {
            LOGE(TAG, "writeBytes[%ld] is less than expected[%lu].", writeBytes, bufLen);
            ++writeErrs_;
        }
    } else {
        LOGE(TAG, "Failed to write to tap device.[%s]", getErrMsg(err).c_str());
        ++writeErrs_;
        writeBytes = -1;
    }

    return writeBytes;
}

ssize_t TapDev::read(void* buf, size_t bufLen, int timeout) {
    static DWORD readBytes;
    static bool waitFlag = false;
    if (!waitFlag && ReadFile(tapFd, buf, bufLen, &readBytes, &overlapRead)) {
        ResetEvent(overlapRead.hEvent);
        return readBytes;
    }

    if (!waitFlag) {
        waitFlag = true;
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            waitFlag = false;
            LOGE(TAG, "Failed to read from tap device.[%s]", getErrMsg(err).c_str());
            ++readErrs_;
            return -1;
        }
    }

    if (WAIT_OBJECT_0 == WaitForSingleObject(overlapRead.hEvent, timeout)) {
        waitFlag = false;
        GetOverlappedResult(tapFd, &overlapRead, &readBytes, FALSE);
        ResetEvent(overlapRead.hEvent);
        return readBytes;
    }

    return 0;
}
