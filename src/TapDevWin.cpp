#include    "TapDev.hpp"
#include    "LogMgr.hpp"
#include    "TapLan.hpp"

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

static NetAdaptInfo tapInfo;
static const char* TAG = "[TapDev]";

static std::string getCurrentWorkDir()
{
    namespace fs = std::filesystem;
    try {
        return fs::current_path().string();
    } catch (const fs::filesystem_error& e) {
        return "";
    }
}

static bool initTapInfo(HKEY adaptKey, LPCSTR adaptIdx)
{
    LONG err;
    std::string errMsg;

    err = RegGetValueA(
        adaptKey,
        adaptIdx,
        "DeviceInstanceID",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo.devInstId,
        &tapInfo.devInstIdLen
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGF(TAG, "Failed to get DeviceInstanceID from %s.[%s]", adaptIdx, errMsg.c_str());
        return false;
    }
    LOGT(TAG, "DeviceInstanceID: [%s]", tapInfo.devInstId);

    err = RegGetValueA(
        adaptKey,
        adaptIdx,
        "NetCfgInstanceId",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo.netCfgInstId,
        &tapInfo.netInstIdLen
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGF(TAG, "Failed to get NetCfgInstanceId from %s.[%s]", adaptIdx, errMsg.c_str());
        return false;
    }
    LOGT(TAG, "NetCfgInstanceId: [%s]", tapInfo.netCfgInstId);

    std::stringstream connKeyPath;
    connKeyPath << NETWORK_CONNECTIONS_KEY << "\\" << tapInfo.netCfgInstId << "\\Connection";

    HKEY connKey;
    err = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        connKeyPath.str().c_str(),
        0,
        KEY_READ | KEY_SET_VALUE,
        &connKey
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGF(TAG, "Failed to open %s.[%s]", connKeyPath.str().c_str(), errMsg);
        RegCloseKey(connKey);
        return false;
    }

    err = RegGetValueA(
        connKey,
        nullptr,
        "Name",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo.name,
        &tapInfo.nameLen
    );
    RegCloseKey(connKey);
    if (err) {
        errMsg = getErrMsg(err);
        LOGE(TAG, "Failed to get Name from %s.[%s]", connKeyPath.str().c_str(), errMsg.c_str());
        return false;
    }

    if (0 != strcmp(TAP_NAME, tapInfo.name)) {
        char cmd[256];
        snprintf(cmd, REG_BUF_SIZE, "netsh interface set interface name=\"%s\" newname=\"%s\"", tapInfo.name, TAP_NAME);
        if (system(cmd)) {
            LOGE(TAG, "Failed to exec [%s].", cmd);
            return false;
        }

        strcpy((char*)tapInfo.name, TAP_NAME);
        tapInfo.nameLen = strlen(TAP_NAME) + 1;
    }

    return true;
}

static bool findExistedTap() {
    bool ret = false;
    LONG err;
    std::string errMsg;

    HKEY adaptKey;
    err = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        ADAPTER_KEY,
        0,
        KEY_READ,
        &adaptKey
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGE(TAG, "Failed to open %s.[%s]", ADAPTER_KEY, errMsg.c_str());
        return false;
    }

    for (DWORD idx = 0; ; ++idx) {
        CHAR adaptIdx[REG_BUF_SIZE];
        DWORD adaptIdxLen = REG_BUF_SIZE;
        err = RegEnumKeyExA(
            adaptKey,
            idx,
            adaptIdx,
            &adaptIdxLen,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        );
        if (err != ERROR_SUCCESS) {
            if (err != ERROR_NO_MORE_ITEMS) {
                errMsg = getErrMsg(err);
                LOGT(TAG, "Failed to enum %s.[%s]", NETWORK_CONNECTIONS_KEY, errMsg.c_str());
            }
            break;
        }

        CHAR owner[REG_BUF_SIZE];
        DWORD ownerLen = REG_BUF_SIZE;
        err = RegGetValueA(
            adaptKey,
            adaptIdx,
            "Owner",
            RRF_RT_REG_SZ,
            nullptr,
            &owner,
            &ownerLen
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGT(TAG, "Failed to get Owner from %s.[%s]", adaptIdx, errMsg.c_str());
            continue;
        } else if (0 != strcmp(TAP_NAME, owner)) {
            continue;
        }

        ret = initTapInfo(adaptKey, adaptIdx);
        break;
    }

    RegCloseKey(adaptKey);
    return ret;
}

static bool createNewTap() {
    bool ret = false;
    LONG err;
    std::string errMsg;
    DWORD64 startTimestamp; {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(&startTimestamp, &ft, 8);
    }
    char cmd[REG_BUF_SIZE];

    DWORD attributes = GetFileAttributesA(TAP_INSTALL);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        snprintf(cmd, REG_BUF_SIZE, "%s install OemVista.inf TAP0901", TAP_INSTALL);
        if (system(TAP_INSTALL " install OemVista.inf TAP0901")) {
            LOGE(TAG, "Failed to exec [%s].", cmd);
            return ret;
        }
    } else {
        LOGE(TAG, "Please place [%s] in [%s].", TAP_INSTALL, getCurrentWorkDir().c_str());
        return ret;
    }

    HKEY adaptKey;
    err = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        ADAPTER_KEY,
        0,
        KEY_READ,
        &adaptKey
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGE(TAG, "Failed to open %s.[%s]", ADAPTER_KEY, errMsg.c_str());
        return false;
    }

    for (DWORD idx = 0; ; ++idx) {
        NetAdaptInfo adapterInfo;
        CHAR adaptIdx[REG_BUF_SIZE];
        DWORD adaptIdxLen = REG_BUF_SIZE;
        err = RegEnumKeyExA(
            adaptKey,
            idx,
            adaptIdx,
            &adaptIdxLen,
            nullptr,
            nullptr,
            nullptr,
            nullptr
        );
        if (err != ERROR_SUCCESS) {
            if (err != ERROR_NO_MORE_ITEMS) {
                errMsg = getErrMsg(err);
                LOGE(TAG, "Failed to enum %s.[%s]", ADAPTER_KEY, errMsg.c_str());
            }
            break;
        }

        DWORD64 installTimestamp;
        DWORD installTimestampLen = sizeof(installTimestamp);
        err = RegGetValueA(
            adaptKey,
            adaptIdx,
            "NetworkInterfaceInstallTimestamp",
            RRF_RT_QWORD,
            nullptr,
            &installTimestamp,
            &installTimestampLen
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGT(TAG, "Failed to get NetworkInterfaceInstallTimestamp from %s.[%s]", adaptIdx, errMsg.c_str());
            continue;
        } else if (installTimestamp < startTimestamp) {
            continue;
        }

        CHAR providerName[REG_BUF_SIZE];
        DWORD providerNameLen = REG_BUF_SIZE;
        err = RegGetValueA(
            adaptKey,
            adaptIdx,
            "ProviderName",
            RRF_RT_REG_SZ,
            nullptr,
            &providerName,
            &providerNameLen
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGT(TAG, "Failed to get ProviderName from %s.[%s]", adaptIdx, errMsg.c_str());
            continue;
        } else if (strcmp("TAP-Windows Provider V9", (const char*)providerName) != 0) {
            continue;
        }

        Mac mac;
        mac.generateMac();
        uint64_t macNum = static_cast<uint64_t>(mac);
        macNum = _byteswap_uint64(macNum);
        macNum >>= 16;
        std::stringstream macSs;
        macSs << std::hex << std::uppercase << std::setfill('0') << std::setw(12) << macNum;
        err = RegSetKeyValueA(
            adaptKey,
            adaptIdx,
            "NetworkAddress",
            REG_SZ,
            macSs.str().c_str(),
            macSs.str().length() + 1
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set NetworkAddress to %s.[%s]", macSs.str().c_str(), errMsg.c_str());
        }

        std::string mtu_size = std::to_string(1418);
        err = RegSetKeyValueA(
            adaptKey,
            adaptIdx,
            "MTU",
            REG_SZ,
            mtu_size.c_str(),
            mtu_size.length() + 1
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set MTU to %s.[%s]", mtu_size.c_str(), errMsg.c_str());
        }

        std::string owner = TAP_NAME;
        err = RegSetKeyValueA(
            adaptKey,
            adaptIdx,
            "Owner",
            REG_SZ,
            owner.c_str(),
            owner.length() + 1
        );
        if (err != ERROR_SUCCESS) {
            errMsg = getErrMsg(err);
            LOGE(TAG, "Failed to set Owner to %s.[%s]", owner.c_str(), errMsg.c_str());
        }

        snprintf(cmd, REG_BUF_SIZE, "%s restart @%s", TAP_INSTALL, adapterInfo.devInstId);
        if (system(cmd)) {
            LOGF(TAG, "Failed to exec [%s].", cmd);
            break;
        }

        ret = initTapInfo(adaptKey, adaptIdx);
        break;
    }

    RegCloseKey(adaptKey);
    return ret;
}

TapDev::TapDev(): fdValid_(false), mac_{},
                    writeErrs_(0), readErrs_(0) {
    fdValid_ = open();
}

TapDev::~TapDev() {
    close();
    // use tapInfo.DeviceInstanceID to remove
    // if (system(TAP_INSTALL " remove TAP0901"))
    //     LOGE(TAG, "Removing tap device failed.");
}

bool TapDev::open() {
    std::string errMsg;

    if (!findExistedTap() && !createNewTap())
        return false;

    std::stringstream tapName;
    tapName << USERMODEDEVICEDIR << tapInfo.netCfgInstId << TAPSUFFIX;
    fd_ = CreateFileA(tapName.str().c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
    if (fd_ == INVALID_HANDLE_VALUE) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to open TAP device.[%s]", errMsg.c_str());
        return false;
    }

    if (!DeviceIoControl(fd_, TAP_IOCTL_SET_MEDIA_STATUS,
                         &tapInfo.mediaStatus, tapInfo.mediaStatusLen,
                         &tapInfo.mediaStatus, tapInfo.mediaStatusLen,
                         &tapInfo.mediaStatusLen, nullptr)) {
        errMsg = getErrMsg(GetLastError());
        LOGF(TAG, "Failed to set status to up.[%s]", errMsg.c_str());
        return false;
    }

    memset(mac_.addr, 0, 6);
    DWORD macLen = sizeof(mac_);
    if (!DeviceIoControl(fd_, TAP_IOCTL_GET_MAC,
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
    if (fd_ != INVALID_HANDLE_VALUE) {
        CloseHandle(fd_);
        fd_ = INVALID_HANDLE_VALUE;
    }

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
