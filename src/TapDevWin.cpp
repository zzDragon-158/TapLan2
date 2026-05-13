#include    "TapDev.hpp"
#include    "LogMgr.hpp"
#include    "TapLan.hpp"

// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Signatures\Unmanaged
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Profiles
#define     TAP_CONTROL_CODE(request, method)       CTL_CODE(FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)
#define     TAP_IOCTL_GET_MAC                       TAP_CONTROL_CODE(1, METHOD_BUFFERED)
#define     TAP_IOCTL_SET_MEDIA_STATUS              TAP_CONTROL_CODE(6, METHOD_BUFFERED)

constexpr LPCSTR ADAPTER_KEY = R"(SYSTEM\CurrentControlSet\Control\Class\{4D36E972-E325-11CE-BFC1-08002BE10318})";
constexpr LPCSTR NETWORK_CONNECTIONS_KEY = R"(SYSTEM\CurrentControlSet\Control\Network\{4D36E972-E325-11CE-BFC1-08002BE10318})";
constexpr LPCSTR TAP_INSTALL = R"(.\tapinstall.exe)";
constexpr LPCSTR USERMODEDEVICEDIR = R"(\\.\Global\)";
constexpr LPCSTR TAPSUFFIX = ".tap";

TapDev::NetAdaptInfo::NetAdaptInfo() noexcept
    : netCfgInstId{}
    , netInstIdLen(REG_BUF_SIZE)
    , devInstId{}
    , devInstIdLen(REG_BUF_SIZE)
    , name{}
    , nameLen(REG_BUF_SIZE)
    , mediaStatus(TRUE)
    , mediaStatusLen(sizeof(mediaStatusLen))
{
    ;
}

static std::string getCurrentWorkDir()
{
    namespace fs = std::filesystem;
    try {
        return fs::current_path().string();
    } catch (const fs::filesystem_error& e) {
        return "";
    }
}

TapDev::TapDev()
    : fd_(INVALID_TAPFD)
    , mac_{}
    , writeBytes_(0)
    , writeErrs_(0)
    , readBytes_(0)
    , readErrs_(0)
    , tapInfo_()
{
    if (!open()) {
        close();
    }
}

TapDev::~TapDev()
{
    close();
    // use tapInfo_.DeviceInstanceID to remove
    // if (system(TAP_INSTALL " remove TAP0901"))
    //     LOGE("Removing tap device failed.");
}

bool TapDev::open()
{
    WINBOOL ok;
    std::string errMsg;

    if (!findExistingTap() && !createNewTap())
        return false;

    std::stringstream tapName;
    tapName << USERMODEDEVICEDIR << tapInfo_.netCfgInstId << TAPSUFFIX;
    fd_ = CreateFileA(tapName.str().c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
    if (!isFdValid()) {
        errMsg = getErrMsg(GetLastError());
        LOGF("Failed to open TAP device.[{}]", errMsg);
        return false;
    }

    ok = DeviceIoControl(
        fd_,
        TAP_IOCTL_SET_MEDIA_STATUS,
        &tapInfo_.mediaStatus,
        tapInfo_.mediaStatusLen,
        &tapInfo_.mediaStatus,
        tapInfo_.mediaStatusLen,
        &tapInfo_.mediaStatusLen,
        nullptr
    );
    if (!ok) {
        errMsg = getErrMsg(GetLastError());
        LOGF("Failed to set status to up.[{}]", errMsg);
        return false;
    }

    mac_ = {};
    DWORD macLen = sizeof(mac_);
    ok = DeviceIoControl(
        fd_,
        TAP_IOCTL_GET_MAC,
        mac_.addr,
        6,
        mac_.addr,
        6,
        &macLen,
        nullptr
    );
    if (!ok) {
        errMsg = getErrMsg(GetLastError());
        LOGF("Failed to get MAC address.[{}]", errMsg);
        return false;
    }

    return true;
}

bool TapDev::close()
{
    if (isFdValid()) {
        CloseHandle(fd_);
        fd_ = INVALID_TAPFD;
    }

    return true;
}

bool TapDev::setIPv4Addr(const in_addr& ipv4Addr, uint8_t netIdLen)
{
    std::string cmd = std::format(
        R"(netsh interface ip set address "{}" static "{}/{}")",
        TAP_NAME,
        ipv4Addr,
        netIdLen
    );

    if (system(cmd.c_str())) {
        LOGE("Failed to exec [{}].", cmd);
        return false;
    }
    LOGI("{} IP address has been set to {}.", TAP_NAME, ipv4Addr);

    return true;
}

bool TapDev::initTapInfo(HKEY adaptKey, LPCSTR adaptIdx)
{
    LONG err;
    std::string errMsg;

    err = RegGetValueA(
        adaptKey,
        adaptIdx,
        "DeviceInstanceID",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo_.devInstId,
        &tapInfo_.devInstIdLen
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGF("Failed to get DeviceInstanceID from {}.[{}]", adaptIdx, errMsg);
        return false;
    }
    LOGT("DeviceInstanceID: [{}]", tapInfo_.devInstId);

    err = RegGetValueA(
        adaptKey,
        adaptIdx,
        "NetCfgInstanceId",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo_.netCfgInstId,
        &tapInfo_.netInstIdLen
    );
    if (err != ERROR_SUCCESS) {
        errMsg = getErrMsg(err);
        LOGF("Failed to get NetCfgInstanceId from {}.[{}]", adaptIdx, errMsg);
        return false;
    }
    LOGT("NetCfgInstanceId: [{}]", tapInfo_.netCfgInstId);

    std::stringstream connKeyPath;
    connKeyPath << NETWORK_CONNECTIONS_KEY << "\\" << tapInfo_.netCfgInstId << "\\Connection";

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
        LOGF("Failed to open {}.[{}]", connKeyPath.str(), errMsg);
        RegCloseKey(connKey);
        return false;
    }

    err = RegGetValueA(
        connKey,
        nullptr,
        "Name",
        RRF_RT_REG_SZ,
        nullptr,
        tapInfo_.name,
        &tapInfo_.nameLen
    );
    RegCloseKey(connKey);
    if (err) {
        errMsg = getErrMsg(err);
        LOGE("Failed to get Name from {}.[{}]", connKeyPath.str(), errMsg);
        return false;
    }

    if (0 != strcmp(TAP_NAME, tapInfo_.name)) {
        std::string cmd = std::format(
            R"(netsh interface set interface name="{}" newname="{}")",
            tapInfo_.name,
            TAP_NAME
        );
        if (system(cmd.c_str())) {
            LOGE("Failed to exec [{}].", cmd);
            return false;
        }

        strcpy((char*)tapInfo_.name, TAP_NAME);
        tapInfo_.nameLen = strlen(TAP_NAME) + 1;
    }

    return true;
}

bool TapDev::findExistingTap()
{
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
        LOGE("Failed to open {}.[{}]", ADAPTER_KEY, errMsg);
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
                LOGT("Failed to enum {}.[{}]", NETWORK_CONNECTIONS_KEY, errMsg);
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
            LOGT("Failed to get Owner from {}.[{}]", adaptIdx, errMsg);
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

bool TapDev::createNewTap()
{
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
        std::string cmd = std::format(
            R"({} install OemVista.inf TAP0901)",
            TAP_INSTALL
        );
        if (system(cmd.c_str())) {
            LOGE("Failed to exec [{}].", cmd);
            return ret;
        }
    } else {
        LOGE("Please place [{}] in [{}].", TAP_INSTALL, getCurrentWorkDir());
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
        LOGE("Failed to open {}.[{}]", ADAPTER_KEY, errMsg);
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
                LOGE("Failed to enum {}.[{}]", ADAPTER_KEY, errMsg);
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
            LOGT("Failed to get NetworkInterfaceInstallTimestamp from {}.[{}]", adaptIdx, errMsg);
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
            LOGT("Failed to get ProviderName from {}.[{}]", adaptIdx, errMsg);
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
            LOGE("Failed to set NetworkAddress to {}.[{}]", macSs.str(), errMsg);
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
            LOGE("Failed to set MTU to {}.[{}]", mtu_size, errMsg);
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
            LOGE("Failed to set Owner to {}.[{}]", owner, errMsg);
        }

        std::string cmd = std::format(
            R"({} restart @{})",
            TAP_INSTALL,
            adapterInfo.devInstId
        );
        if (system(cmd.c_str())) {
            LOGF("Failed to exec [{}].", cmd);
            break;
        }

        ret = initTapInfo(adaptKey, adaptIdx);
        break;
    }

    RegCloseKey(adaptKey);
    return ret;
}
