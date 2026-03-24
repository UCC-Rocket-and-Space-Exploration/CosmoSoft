#include "gateway/comms/windows/SerialPortScannerWindows.h"


// #include "comms/SerialPortScannerWindows.h"

#include "gateway/comms/windows/SerialPortScannerWindows.h"

#include <string>
#include <vector>
#include <windows.h>

std::vector<std::string> SerialPortScannerWindows::enumeratePorts() {
    std::vector<std::string> ports = {};

    HKEY hKey;
    const char* subKey = "HARDWARE\\DEVICEMAP\\SERIALCOMM";

    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, subKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS) {
        // cout << "Failed to open key" << endl;
        throw "Failed to open key";
    }
    // ports.push_back("\\\\.\\COM1");
    DWORD index = 0;
    char valueName[256];
    BYTE data[256];
    DWORD type;

    while (true) {
        DWORD valueNameSize = sizeof(valueName);
        DWORD dataSize = sizeof(data);
        DWORD result = RegEnumValueA(
            hKey,
            index,
            valueName,
            &valueNameSize,
            NULL,
            &type,
            data,
            &dataSize
        );
        if (result == ERROR_NO_MORE_ITEMS)
            break;
        if (result == ERROR_SUCCESS) {
            ports.push_back((char*)data);
            // cout << "Val: " << valueName << " DATA: " << (char*)data << endl;
        }
        else {
            RegCloseKey(hKey);
            break;
        }
        index++;
    }
    RegCloseKey(hKey);
    return ports;
}

bool SerialPortScannerWindows::tryOpenPort(const std::string &portName) {
    HANDLE h = CreateFile(portName.c_str(), GENERIC_READ | GENERIC_WRITE,
                               0, NULL, OPEN_EXISTING,NULL,NULL);

    bool isValid = h != INVALID_HANDLE_VALUE;
    CloseHandle(h);
    return isValid;
}
