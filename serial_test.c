/* serial_test.c
 * replicates how mta gets disk serial on wine
 *
 * mingw:  x86_64-w64-mingw32-gcc -o mta_serial_test.exe mta_serial_test.c -lole32 -loleaut32 -lwbemuuid
 * msvc:   cl mta_serial_test.c ole32.lib oleaut32.lib wbemuuid.lib
 * run:    wine serial_test.exe
 */

#include <windows.h>
#include <winioctl.h>
#include <stdio.h>

/* ioctl path, same thing wine's get_diskdrive_serialnumber() does */

static void test_ioctl(const char *drive_letter)
{
    char path[16];
    STORAGE_PROPERTY_QUERY query = {0};
    STORAGE_DEVICE_DESCRIPTOR *desc = NULL;
    DWORD size = sizeof(*desc) + 256;
    HANDLE h;

    snprintf(path, sizeof(path), "\\\\.\\%c:", *drive_letter);
    printf("[IOCTL] Opening %s\n", path);

    h = CreateFileA(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("[IOCTL] CreateFile failed: error %lu\n", GetLastError());
        return;
    }

    query.PropertyId = StorageDeviceProperty;
    query.QueryType  = PropertyStandardQuery;

    for (;;) {
        desc = (STORAGE_DEVICE_DESCRIPTOR *)malloc(size);
        if (!desc) break;
        memset(desc, 0, size);

        if (DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
                            &query, sizeof(query), desc, size, NULL, NULL)) {
            if (desc->SerialNumberOffset && desc->SerialNumberOffset < size) {
                const char *serial = (const char *)desc + desc->SerialNumberOffset;
                printf("[IOCTL] SerialNumber = \"%s\"\n", serial);
            } else {
                printf("[IOCTL] SerialNumberOffset = 0 (no serial)\n");
            }
            free(desc);
            break;
        }

        if (GetLastError() == ERROR_MORE_DATA) {
            size = desc->Size;
            free(desc);
            continue;
        }

        printf("[IOCTL] DeviceIoControl failed: error %lu\n", GetLastError());
        free(desc);
        break;
    }

    CloseHandle(h);
}

/* wmi path*/
#include <objbase.h>
#include <wbemcli.h>

static void test_wmi(void)
{
    HRESULT hr;
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    IEnumWbemClassObject *enm = NULL;
    IWbemClassObject *obj = NULL;
    ULONG count = 0;
    VARIANT val;

    printf("\n[WMI] Querying Win32_DiskDrive.SerialNumber ...\n");

    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("[WMI] CoInitialize failed: 0x%08lx\n", hr);
        return;
    }

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (void **)&loc);
    if (FAILED(hr)) {
        printf("[WMI] CoCreateInstance(WbemLocator) failed: 0x%08lx\n", hr);
        goto out;
    }

    hr = loc->lpVtbl->ConnectServer(loc, L"ROOT\\CIMV2", NULL, NULL, NULL,
                                     0, NULL, NULL, &svc);
    if (FAILED(hr)) {
        printf("[WMI] ConnectServer failed: 0x%08lx\n", hr);
        goto out;
    }

    hr = svc->lpVtbl->ExecQuery(svc,
            L"WQL",
            L"SELECT SerialNumber FROM Win32_DiskDrive",
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &enm);
    if (FAILED(hr)) {
        printf("[WMI] ExecQuery failed: 0x%08lx\n", hr);
        goto out;
    }

    while (enm->lpVtbl->Next(enm, WBEM_INFINITE, 1, &obj, &count) == S_OK && count > 0) {
        VariantInit(&val);
        hr = obj->lpVtbl->Get(obj, L"SerialNumber", 0, &val, NULL, NULL);
        if (SUCCEEDED(hr) && val.vt == VT_BSTR) {
            printf("[WMI] Win32_DiskDrive.SerialNumber = \"%ls\"\n", val.bstrVal);
        } else {
            printf("[WMI] Get(SerialNumber) failed or null: hr=0x%08lx vt=%d\n", hr, val.vt);
        }
        VariantClear(&val);
        obj->lpVtbl->Release(obj);
    }

    /* physical media table, where the WINEHDISK fallback lives */
    if (enm) enm->lpVtbl->Release(enm);
    enm = NULL;

    printf("\n[WMI] Querying Win32_PhysicalMedia.SerialNumber ...\n");
    hr = svc->lpVtbl->ExecQuery(svc,
            L"WQL",
            L"SELECT SerialNumber, Tag FROM Win32_PhysicalMedia",
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &enm);
    if (SUCCEEDED(hr)) {
        while (enm->lpVtbl->Next(enm, WBEM_INFINITE, 1, &obj, &count) == S_OK && count > 0) {
            VariantInit(&val);
            obj->lpVtbl->Get(obj, L"SerialNumber", 0, &val, NULL, NULL);
            if (val.vt == VT_BSTR)
                printf("[WMI] PhysicalMedia.SerialNumber = \"%ls\"\n", val.bstrVal);
            VariantClear(&val);

            VariantInit(&val);
            obj->lpVtbl->Get(obj, L"Tag", 0, &val, NULL, NULL);
            if (val.vt == VT_BSTR)
                printf("[WMI] PhysicalMedia.Tag = \"%ls\"\n", val.bstrVal);
            VariantClear(&val);

            obj->lpVtbl->Release(obj);
        }
    }

out:
    if (enm) enm->lpVtbl->Release(enm);
    if (svc) svc->lpVtbl->Release(svc);
    if (loc) loc->lpVtbl->Release(loc);
    CoUninitialize();
}

int main(void)
{
    printf("mta disk serial test\n\n");

    test_ioctl("C");
    test_wmi();

    printf("\nif you see WINEHDISK, wine couldn't get your real serial.\n");

    return 0;
}
