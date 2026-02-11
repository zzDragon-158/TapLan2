#include    "TapDev.hpp"
#include    "LogMgr.hpp"

// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Signatures\Unmanaged
// HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\NetworkList\Profiles
#define     ADAPTER_KEY                             "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define     NETWORK_CONNECTIONS_KEY                 "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"
#define     TAP_INSTALL                             ".\\tapinstall.exe"
#define     USERMODEDEVICEDIR                       "\\\\.\\Global\\"
#define     TAPSUFFIX                               ".tap"
#define     BUFFER_SIZE                             1024
#define     TAP_CONTROL_CODE(request, method)       CTL_CODE(FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)
#define     TAP_IOCTL_GET_MAC                       TAP_CONTROL_CODE(1, METHOD_BUFFERED)
#define     TAP_IOCTL_SET_MEDIA_STATUS              TAP_CONTROL_CODE(6, METHOD_BUFFERED)

struct WinAdapterInfo {
    HANDLE handle;
    CHAR adapterId[BUFFER_SIZE];
    DWORD adapterIdLen;
    CHAR adapterName[BUFFER_SIZE];
    DWORD adapterNameLen;
    CHAR adapterMac[6];
    DWORD adapterMacLen;
    DWORD mediaStatus;
    DWORD mediaStatusLen;
    OVERLAPPED overlapRead, overlapWrite;

    WinAdapterInfo():   handle(nullptr), adapterIdLen(BUFFER_SIZE), adapterNameLen(BUFFER_SIZE), adapterMacLen(6),
                        mediaStatus(TRUE), mediaStatusLen(sizeof(mediaStatusLen)) {
        memset(adapterId, 0, sizeof(adapterId));
        memset(adapterName, 0, sizeof(adapterName));
        memset(adapterMac, 0, sizeof(adapterMac));
        memset(&overlapRead, 0, sizeof(overlapRead));
        memset(&overlapWrite, 0, sizeof(overlapWrite));
        overlapRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        overlapWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    }
};

static WinAdapterInfo tapLanTapDevice;
static const char* TAG = "[TapDev]";

static bool findExistedTap() {
    bool ret = false;

    HKEY openKey0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, NETWORK_CONNECTIONS_KEY, 0, KEY_READ, &openKey0)) {
        LOGE(TAG, "Openning %s failed.", NETWORK_CONNECTIONS_KEY);
        return ret;
    }

    for (int i = 0; ; ++i) {
        WinAdapterInfo adapter;
        if (RegEnumKeyExA(openKey0, i, adapter.adapterId, &adapter.adapterIdLen, nullptr, nullptr, nullptr, nullptr))
            break;

        HKEY openKey1;
        std::ostringstream regpath;
        regpath << NETWORK_CONNECTIONS_KEY << "\\" << adapter.adapterId << "\\Connection";
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regpath.str().c_str(), 0, KEY_READ, &openKey1))
            continue;

        int err = RegQueryValueExA(openKey1, "Name", nullptr, nullptr, (LPBYTE)adapter.adapterName, &adapter.adapterNameLen);
        if (err) {
            RegCloseKey(openKey1);
            continue;
        }
        RegCloseKey(openKey1);

        if (0 != strcmp(TAP_NAME, (const char*)adapter.adapterName))
            continue;

        memcpy(&tapLanTapDevice, &adapter, sizeof(WinAdapterInfo));
        ret = true;
        break;
    }

    RegCloseKey(openKey0);
    return ret;
}

static bool createNewTap() {
    bool ret = false;
    DWORD64 startTimestamp; {
        FILETIME ft;
        GetSystemTimeAsFileTime(&ft);
        memcpy(&startTimestamp, &ft, 8);
    }

    DWORD attributes = GetFileAttributesA(TAP_INSTALL);
    if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        if (system(TAP_INSTALL " install OemVista.inf TAP0901")) {
            LOGE(TAG, "Creating tap device failed.");
            return ret;
        }
    } else {
        LOGE(TAG, "%s does not exist. Please place this program in the current directory.", TAP_INSTALL);
        return ret;
    }

    HKEY openKey0;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ADAPTER_KEY, 0, KEY_READ, &openKey0)) {
        LOGE(TAG, "Openning %s failed.", ADAPTER_KEY);
        return false;
    }

    for (int i = 0; ; ++i) {
        WinAdapterInfo adapter;
        CHAR driveId[BUFFER_SIZE];
        DWORD driveIdLen = BUFFER_SIZE;
        if (RegEnumKeyExA(openKey0, i, driveId, &driveIdLen, nullptr, nullptr, nullptr, nullptr))
            break;

        HKEY openKey1;
        std::ostringstream regpath;
        regpath << ADAPTER_KEY << "\\" << driveId;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regpath.str().c_str(), 0, KEY_READ, &openKey1))
            continue;

        LONG err;
        DWORD64 installTimestamp;
        DWORD installTimestampLen = sizeof(installTimestamp);
        err = RegQueryValueExA(openKey1, "NetworkInterfaceInstallTimestamp", nullptr, nullptr, (UCHAR*)&installTimestamp, &installTimestampLen);
        if (err || installTimestamp < startTimestamp) {
            RegCloseKey(openKey1);
            continue;
        }

        CHAR providerName[BUFFER_SIZE];
        DWORD providerNameLen = BUFFER_SIZE;
        err = RegQueryValueExA(openKey1, "ProviderName", nullptr, nullptr, (UCHAR*)&providerName, &providerNameLen);
        if (err || strcmp("TAP-Windows Provider V9", (const char*)providerName)) {
            RegCloseKey(openKey1);
            continue;
        }
        err = RegQueryValueExA(openKey1, "NetCfgInstanceId", nullptr, nullptr, (UCHAR*)adapter.adapterId, &adapter.adapterIdLen);
        if (err) {
            RegCloseKey(openKey1);
            continue;
        }
        RegCloseKey(openKey1);

        regpath.str("");
        regpath << NETWORK_CONNECTIONS_KEY << "\\" << adapter.adapterId << "\\Connection";
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, regpath.str().c_str(), 0, KEY_READ, &openKey1)) {
            LOGE(TAG, "Openning %s failed.", regpath.str().c_str());
            break;
        }
        err = RegQueryValueExA(openKey1, "Name", nullptr, nullptr, (LPBYTE)adapter.adapterName, &adapter.adapterNameLen);
        if (err) {
            RegCloseKey(openKey1);
            LOGE(TAG, "Getting tap device name failed.");
            break;
        }
        RegCloseKey(openKey1);

        std::ostringstream cmd;
        cmd << "netsh interface set interface name=\"" << adapter.adapterName << "\" newname=\"" << TAP_NAME << "\"";
        if (system(cmd.str().c_str())) {
            LOGE(TAG, "Rename tap device failed.");
            break;
        }
        cmd.str("");
        cmd << "netsh interface ipv4 set subinterface \"" << TAP_NAME << "\" mtu=1418 store=persistent";
        if (system(cmd.str().c_str())) {
            LOGE(TAG, "set tap device mtu to 1418 failed.");
            break;
        }

        strcpy(adapter.adapterName, TAP_NAME);
        adapter.adapterNameLen = strlen(TAP_NAME) + 1;
        memcpy(&tapLanTapDevice, &adapter, sizeof(WinAdapterInfo));
        ret = true;
        break;
    }

    RegCloseKey(openKey0);
    return ret;
}

TapDev::TapDev(): fdValid_(true), mac_{},
                    writeErrs_(0), readErrs_(0) {
    // nothing to do
}

TapDev::~TapDev() {
    close();
}

bool TapDev::open() {
    if (!findExistedTap() && !createNewTap())
        return false;

    std::ostringstream tapName;
    tapName << USERMODEDEVICEDIR << tapLanTapDevice.adapterId << TAPSUFFIX;
    tapLanTapDevice.handle = CreateFileA(tapName.str().c_str(), GENERIC_WRITE | GENERIC_READ, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);
    if (tapLanTapDevice.handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (!DeviceIoControl(tapLanTapDevice.handle, TAP_IOCTL_SET_MEDIA_STATUS,
        &tapLanTapDevice.mediaStatus, tapLanTapDevice.mediaStatusLen,
        &tapLanTapDevice.mediaStatus, tapLanTapDevice.mediaStatusLen, &tapLanTapDevice.mediaStatusLen, nullptr)) {
        LOGE(TAG, "DeviceIoControl(TAP_IOCTL_SET_MEDIA_STATUS) failed.");
        return false;
    }

    if (!DeviceIoControl(tapLanTapDevice.handle, TAP_IOCTL_GET_MAC,
        tapLanTapDevice.adapterMac, tapLanTapDevice.adapterMacLen,
        tapLanTapDevice.adapterMac, tapLanTapDevice.adapterMacLen, &tapLanTapDevice.adapterMacLen, nullptr)) {
        LOGE(TAG, "DeviceIoControl(TAP_IOCTL_GET_MAC) failed.");
        return false;
    }
    std::memcpy(mac_.addr, tapLanTapDevice.adapterMac, 6);

    return true;
}

bool TapDev::close() {
    CloseHandle(tapLanTapDevice.handle);
    // if (system(TAP_INSTALL " remove TAP0901"))
    //     LOGE(TAG, "Removing tap device failed.");

    return true;
}

void TapDev::getMacAddr(Mac& mac) {
    std::memcpy(mac.addr, &tapLanTapDevice.adapterMac, 6);
}

bool TapDev::setIPv4Addr(const in_addr* ipv4Addr, uint8_t netIdLen)
{
    std::ostringstream cidr;
    cidr << inet_ntoa(*ipv4Addr) << "/" << +netIdLen;

    std::ostringstream cmd;
    cmd << "netsh interface ip set address \"" << TAP_NAME << "\" static " << cidr.str();
    if (system(cmd.str().c_str())) {
        LOGE(TAG, "Setting %s IP address to %s failed.", TAP_NAME, cidr.str().c_str());
        return false;
    }
    LOGI(TAG, "%s IP address has been set to %s.", TAP_NAME, cidr.str().c_str());

    return true;
}

ssize_t TapDev::write(const void* buf, size_t bufLen) {
    static DWORD writeBytes;
    if (WriteFile(tapLanTapDevice.handle, buf, bufLen, &writeBytes, &tapLanTapDevice.overlapWrite)) {
        ResetEvent(tapLanTapDevice.overlapWrite.hEvent);
        return writeBytes;
    }

    DWORD lastError = GetLastError();
    if (lastError == ERROR_IO_PENDING) {
        GetOverlappedResult(tapLanTapDevice.handle, &tapLanTapDevice.overlapWrite, &writeBytes, TRUE);
        ResetEvent(tapLanTapDevice.overlapWrite.hEvent);
        if (writeBytes < bufLen) {
            LOGE(TAG, "writeBytes[%ld] is less than expected[%lu].", writeBytes, bufLen);
            ++writeErrs_;
        }
    } else {
        LOGE(TAG, "Writting to tap device failed. %u %u", bufLen, lastError);
        ++writeErrs_;
        writeBytes = -1;
    }

    return writeBytes;
}

ssize_t TapDev::read(void* buf, size_t bufLen, int timeout) {
    static DWORD readBytes;
    static bool waitFlag = false;
    if (!waitFlag && ReadFile(tapLanTapDevice.handle, buf, bufLen, &readBytes, &tapLanTapDevice.overlapRead)) {
        ResetEvent(tapLanTapDevice.overlapRead.hEvent);
        return readBytes;
    }

    if (!waitFlag) {
        waitFlag = true;
        DWORD lastError = GetLastError();
        if (lastError != ERROR_IO_PENDING) {
            waitFlag = 0;
            LOGE(TAG, "Reading from tap device failed.");
            ++readErrs_;
            return -1;
        }
    }

    if (WAIT_OBJECT_0 == WaitForSingleObject(tapLanTapDevice.overlapRead.hEvent, timeout)) {
        waitFlag = 0;
        GetOverlappedResult(tapLanTapDevice.handle, &tapLanTapDevice.overlapRead, &readBytes, FALSE);
        ResetEvent(tapLanTapDevice.overlapRead.hEvent);
        return readBytes;
    }

    return 0;
}
