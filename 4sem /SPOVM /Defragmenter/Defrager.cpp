#include "Defrager.h"

// ������������ ����� �� �����
int WorkIn(CString directory, CString dr, bool first = false)
{
    int res = 1;
    // ���� ����� ��� �������� ����������, �� �������� #:\ ����� ���� C:\ 
    if (first)
    {
        directory += ":\\";
    }

    WIN32_FIND_DATA FindFileData;
    HANDLE hf;

    hf = FindFirstFile((directory + L"*"), &FindFileData);
    if (hf != INVALID_HANDLE_VALUE)
    {
        res = -1;
        do
        {
            CString tmp = FindFileData.cFileName;
            // ���� ��� �������� ����� . ��� .., �� ����������
            if (tmp != "." && tmp != "..")
            {
                CString full_file_name = directory + FindFileData.cFileName;
                // ���� �������� �����, �� �������� ���������� ������� ��� ��� ��� �����
                if (FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                {
                    WorkIn(full_file_name + "\\", dr);
                }
                else
                {
                    // �������� ������������ �����
                    RETRIEVAL_POINTERS_BUFFER* fileBitmap = readFileBitmap(full_file_name.GetString());
                    if (fileBitmap != NULL)
                    {
                        // ���� ���� �� ������ �� �����, �� ������� +
                        if (fileBitmap->ExtentCount == 1)
                        {
                            cout << "+ ";
                            cout << full_file_name << endl;
                        }
                        else {
                            // ���� ������, �� ��������� ��� ��������������
                            // ���� �������������� �������, �� ������� = ����� -
                            res = Move(full_file_name.GetString(), dr.GetString());
                            cout << ((!res) ? "= " : "- ");
                            cout << full_file_name << endl;
                        }
                    }
                    free(fileBitmap);
                }
            }
        } while (FindNextFile(hf, &FindFileData) != 0);
        FindClose(hf);
    }

    return res;
}

// ������� ������ ������� ����� ����� � ��������� ���������/������� ���������
VOLUME_BITMAP_BUFFER* readVolumeBitmap(LPCWSTR drive)
{
    HANDLE hDrive = 0;
    DWORD Bytes;
    int ret;
    _int64 nOutSize = 0;
    STARTING_LCN_INPUT_BUFFER  InBuf;
    VOLUME_BITMAP_BUFFER  Buffer;
    VOLUME_BITMAP_BUFFER* Result = &Buffer;

    // ��������� ���� ��� ������
    hDrive = CreateFile(drive, 
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        0);
    if (hDrive == INVALID_HANDLE_VALUE)
    {
        ret = GetLastError();
        return NULL;
    }

    nOutSize = sizeof(VOLUME_BITMAP_BUFFER);
    InBuf.StartingLcn.QuadPart = 0;

    // �������� ������� �����   
    ret = DeviceIoControl(hDrive,
        FSCTL_GET_VOLUME_BITMAP,
        &InBuf,
        sizeof(InBuf),
        Result,
        nOutSize,
        &Bytes,
        NULL);
    // ���� ��������� ������ ��� ��������� ������
    if (!ret && GetLastError() == ERROR_MORE_DATA)
    {
        // �������� ���-�� ��������� �� ���� (������� � StartingLcn)
        _int64 CountClusters = Result->BitmapSize.QuadPart - Result->StartingLcn.QuadPart;
        // ���������, ������� ����� ���� ��� ����� (1 ������� = 1 ���)
        nOutSize = CountClusters / sizeof(char) + sizeof(VOLUME_BITMAP_BUFFER);
        Result = (PVOLUME_BITMAP_BUFFER)new char[nOutSize];
        if (!Result)
            return NULL;
        Result->StartingLcn.QuadPart = 0;

        // �������� ������� �����
        ret = DeviceIoControl(hDrive,
            FSCTL_GET_VOLUME_BITMAP,
            &InBuf,
            sizeof(InBuf),
            Result,
            nOutSize,
            &Bytes,
            NULL);
        if (ret)
        {
            CloseHandle(hDrive);
            return Result;
        }
        else
        {
            ret = GetLastError();
        }
    }

    CloseHandle(hDrive);

    return 0;
}

// ������� ��������� ������� ���������� ����� �� �����
RETRIEVAL_POINTERS_BUFFER* readFileBitmap(wstring fileName) 
{
    HANDLE hFile;
    STARTING_VCN_INPUT_BUFFER startingVcn = { 0 };
    RETRIEVAL_POINTERS_BUFFER* fileBitmap;
    UINT32 bitmapSize;
    DWORD bytesReturned;
    int ret;
    wstring bitmapFile;

    // ������������ � �����
    hFile = CreateFile(fileName.c_str(),
        GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ret = GetLastError();
    }

    startingVcn.StartingVcn.QuadPart = 0;
    bitmapSize = 32 * 1024 + sizeof(LARGE_INTEGER) * 2;
    fileBitmap = (RETRIEVAL_POINTERS_BUFFER*)malloc(bitmapSize);

    char* pBuf = new char[1024 * 2];
    ZeroMemory(pBuf, 1024 * 2);
    DWORD nOutBufferSize = 1024 * 2;

    while (TRUE) {
        // �������� ���������� ����� �� ��������� �����
        ret = DeviceIoControl(hFile,
            FSCTL_GET_RETRIEVAL_POINTERS,
            &startingVcn,
            sizeof(STARTING_VCN_INPUT_BUFFER),
            fileBitmap,
            bitmapSize,
            &bytesReturned,
            NULL);
        if (FALSE == ret) {
            if (GetLastError() != ERROR_MORE_DATA) {
                delete[] pBuf;
                free(fileBitmap);
                CloseHandle(hFile);
                return NULL;
            }
        }

        bytesReturned -= sizeof(LARGE_INTEGER) * 2;
        if (ret)
            break;
        startingVcn.StartingVcn.QuadPart += bytesReturned * 8;
    }

    delete[] pBuf;

    CloseHandle(hFile);

    return fileBitmap;
}

// ������� ����������� ������ ����� �� �����
int Move(LPCWSTR lpSrcName, LPCWSTR drive)
{
    int ret = 0;
    ULONG nClusterSize = 0;
    ULONG nBlockSize = 0;
    ULONG nClCount = 0;
    ULONG nFileSize = 0;
    ULONG nBytes = 0;
    HANDLE hDrive = 0;
    HANDLE hFile = 0;
    ULONG nSecPerCl, nBtPerSec;
    MOVE_FILE_DATA InBuffer;
    ULONG nOutSize = 0;
    LPDWORD pBytes = new DWORD[100];
    ULONG nCls = 0;
    _int64* pClusters = NULL;
    BOOLEAN bResult = FALSE;
    LARGE_INTEGER PrevVCN, LCN;
    VOLUME_BITMAP_BUFFER* pOutBuf = readVolumeBitmap(drive);
    STARTING_VCN_INPUT_BUFFER  InBuf;
    RETRIEVAL_POINTERS_BUFFER* OutBuf = NULL;
    WCHAR* Name = new WCHAR[3];
    int res = 1;

    if (pOutBuf != NULL)
    {
        res = 0;
        // ������������ � �����
        hFile = CreateFile(lpSrcName,
            FILE_READ_ATTRIBUTES, 
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, OPEN_EXISTING,
            0, 
            0);

        Name[0] = lpSrcName[0];
        Name[1] = ':';
        Name[2] = 0;

        // �������� ������ ���������� ����� �� �����
        if (GetDiskFreeSpace((LPWSTR)Name,
            &nSecPerCl,
            &nBtPerSec,
            NULL,
            NULL) == FALSE)
        {
            ret = GetLastError();

            delete[] pBytes;
            delete[] Name;
            delete[] pOutBuf;

            CloseHandle(hFile);

            return ret;
        }

        // �������� ����� ������ ������� ���������� ����� �� �����
        // � ������� �������� �����
        nClusterSize = nSecPerCl * nBtPerSec;
        if (hFile != INVALID_HANDLE_VALUE)
        {
            nFileSize = GetFileSize(hFile, NULL);
            nOutSize = sizeof(RETRIEVAL_POINTERS_BUFFER) + (nFileSize / nClusterSize) * sizeof(OutBuf->Extents);
            OutBuf = new RETRIEVAL_POINTERS_BUFFER[nOutSize];
            InBuf.StartingVcn.QuadPart = 0;

            if (DeviceIoControl(hFile,
                FSCTL_GET_RETRIEVAL_POINTERS,
                &InBuf,
                sizeof(InBuf),
                OutBuf,
                nOutSize,
                pBytes,
                NULL))
            {
                nClCount = (nFileSize + nClusterSize - 1) / nClusterSize;
                pClusters = new _int64[nClCount];
                PrevVCN = OutBuf->StartingVcn;
                for (ULONG i = 0, Cls = 0; i < OutBuf->ExtentCount; i++)
                {
                    LCN = OutBuf->Extents[i].Lcn;

                    for (int j = (ULONG)(OutBuf->Extents[i].NextVcn.QuadPart - PrevVCN.QuadPart); j > 0; j--, nCls++, LCN.QuadPart++)
                    {
                        *(pClusters + Cls) = LCN.QuadPart;
                    }

                    PrevVCN = OutBuf->Extents[i].NextVcn;
                }
            }

        }
        CloseHandle(hFile);

        // ������������ � �����
        hFile = CreateFile(lpSrcName,
            FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL, 
            OPEN_EXISTING,
            FILE_FLAG_NO_BUFFERING,
            NULL);

        // ������������ � �����
        hDrive = CreateFile(drive,
            GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            NULL, OPEN_EXISTING,
            0,
            0);

        if (pClusters)
        {
            // ����������� ������� ������� ����� � ���� ���������� ����������������� ���������� �����
            // ��� ���������� �����
            LONGLONG nStartLCN(0), nEmptyCluster(0), nHelpLCN(0), nMask(1), nInUse(0);
            for (__int64 i = 0; i < pOutBuf->BitmapSize.QuadPart; i++)
            {
                if (pOutBuf->Buffer[i] == 255)
                {
                    nStartLCN += 8;
                    nEmptyCluster = 0;
                    continue;
                }

                while (nMask != 256)
                {
                    if (!nEmptyCluster)
                        nHelpLCN = nStartLCN;

                    nInUse = pOutBuf->Buffer[i] & nMask;

                    if (!nInUse)
                        nEmptyCluster++;
                    else
                        nEmptyCluster = 0;

                    nMask <<= 1;
                    nStartLCN++;
                }

                if (nEmptyCluster >= nClCount)
                {
                    nStartLCN = nHelpLCN;
                    break;
                }

                nMask = 1;
            }

            // ���������� ����� �����
            PrevVCN.QuadPart = 0;
            InBuffer.FileHandle = hFile;
            InBuffer.StartingLcn.QuadPart = nStartLCN;

            for (int k = 0; k < OutBuf->ExtentCount; k++)
            {
                InBuffer.StartingVcn = PrevVCN;
                InBuffer.ClusterCount = OutBuf->Extents[k].NextVcn.QuadPart - PrevVCN.QuadPart;

                if (DeviceIoControl(hDrive,
                    FSCTL_MOVE_FILE,
                    &InBuffer,
                    sizeof(InBuffer),
                    NULL,
                    NULL,
                    &nBytes,
                    NULL) == FALSE)
                {
                    ret = GetLastError();

                    delete[] pBytes;
                    delete[] Name;
                    delete[] pOutBuf;
                    delete[] OutBuf;
                    delete[] pClusters;

                    CloseHandle(hFile);
                    CloseHandle(hDrive);

                    return ret;
                }

                InBuffer.StartingLcn.QuadPart += InBuffer.ClusterCount;
                PrevVCN = OutBuf->Extents[k].NextVcn;
            }
        }
    }

    delete[] pBytes;
    delete[] Name;
    delete[] OutBuf;
    delete[] pOutBuf;
    delete[] pClusters;

    CloseHandle(hDrive);
    CloseHandle(hFile);

    return res;
}