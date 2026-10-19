/*
 *  COPY.C -- copy internal command.
 *
 *
 *  History:
 *
 *    01-Aug-98 (Rob Lake z63rrl@morgan.ucs.mun.ca)
 *        started
 *
 *    13-Aug-1998 (John P. Price)
 *        fixed memory leak problem in copy function.
 *        fixed copy function so it would work with wildcards in the source
 *
 *    13-Dec-1998 (Eric Kohl)
 *        Added COPY command to CMD.
 *
 *    26-Jan-1998 (Eric Kohl)
 *        Replaced CRT io functions by Win32 io functions.
 *
 *    27-Oct-1998 (Eric Kohl)
 *        Disabled prompting when used in batch mode.
 *
 *    03-Apr-2005 (Magnus Olsen <magnus@greatlord.com>)
 *        Remove all hardcode string to En.rc
 *
 *    13-Jul-2005 (Brandon Turner <turnerb7@msu.edu>)
 *        Rewrite to clean up copy and support wildcard.
 *
 *    20-Jul-2005 (Brandon Turner <turnerb7@msu.edu>)
 *        Add touch syntax.  "copy arp.exe+,,"
 *        Copy command is now completed.
 */

#include "precomp.h"

#ifdef INCLUDE_CMD_COPY

enum
{
    COPY_ASCII       = 0x001,   /* /A  */
    COPY_DECRYPT     = 0x004,   /* /D  */
    COPY_VERIFY      = 0x008,   /* /V  : Dummy, Never will be Implemented */
    COPY_SHORTNAME   = 0x010,   /* /N  : Dummy, Never will be Implemented */
    COPY_NO_PROMPT   = 0x020,   /* /Y  */
    COPY_PROMPT      = 0x040,   /* /-Y */
    COPY_RESTART     = 0x080,   /* /Z  */
    COPY_BINARY      = 0x100,   /* /B  */
};

INT
copy(TCHAR source[MAX_PATH],
     TCHAR dest[MAX_PATH],
     INT append,
     DWORD lpdwFlags,
     BOOL bTouch)
{
    FILETIME srctime,NewFileTime;
    HANDLE hFileSrc;
    HANDLE hFileDest;
    LPBYTE buffer;
    DWORD  dwAttrib;
    DWORD  dwRead;
    DWORD  dwWritten;
    BOOL   bEof = FALSE;
    BOOL   bHaveTime;
    TCHAR TrueDest[MAX_PATH];
    TCHAR TempSrc[MAX_PATH];
    TCHAR * FileName;
    SYSTEMTIME CurrentTime;

    /* Check Breaker */
    if (CheckCtrlBreak(BREAK_INPUT))
        return 0;

    TRACE ("checking mode\n");

    if (bTouch)
    {
        hFileSrc = CreateFile (source, GENERIC_WRITE, FILE_SHARE_READ,
            NULL, OPEN_EXISTING, 0, NULL);
        if (hFileSrc == INVALID_HANDLE_VALUE)
        {
            ConOutResPrintf(STRING_COPY_ERROR1, source);
            nErrorLevel = 1;
            return 0;
        }

        GetSystemTime(&CurrentTime);
        SystemTimeToFileTime(&CurrentTime, &NewFileTime);
        if (SetFileTime(hFileSrc,(LPFILETIME) NULL, (LPFILETIME) NULL, &NewFileTime))
        {
            CloseHandle(hFileSrc);
            return 1;

        }
        else
        {
            CloseHandle(hFileSrc);
            nErrorLevel = 1;
            return 0;
        }
    }

    dwAttrib = GetFileAttributes (source);

    hFileSrc = CreateFile (source, GENERIC_READ, FILE_SHARE_READ,
        NULL, OPEN_EXISTING, 0, NULL);
    if (hFileSrc == INVALID_HANDLE_VALUE)
    {
        ConOutResPrintf(STRING_COPY_ERROR1, source);
        nErrorLevel = 1;
        return 0;
    }

    TRACE ("getting time\n");

    bHaveTime = GetFileTime (hFileSrc, &srctime, NULL, NULL);

    TRACE ("copy: flags has %s\n",
        lpdwFlags & COPY_ASCII ? "ASCII" : "BINARY");

    /* Check to see if /D or /Z are true, if so we need a middle
       man to copy the file too to allow us to use CopyFileEx later */
    if (lpdwFlags & COPY_DECRYPT)
    {
        GetEnvironmentVariable(_T("TEMP"),TempSrc,MAX_PATH);
        _tcscat(TempSrc,_T("\\"));
        FileName = _tcsrchr(source,_T('\\'));
        FileName++;
        _tcscat(TempSrc,FileName);
        /* This is needed to be on the end to prevent an error
           if the user did "copy /D /Z foo bar then it would be copied
           too %TEMP%\foo here and when %TEMP%\foo when it sets it up
           for COPY_RESTART, this would mean it is copying to itself
           which would error when it tried to open the handles for ReadFile
           and WriteFile */
        _tcscat(TempSrc,_T(".decrypt"));
        if (!CopyFileEx(source, TempSrc, NULL, NULL, FALSE, COPY_FILE_ALLOW_DECRYPTED_DESTINATION))
        {
            CloseHandle (hFileSrc);
            nErrorLevel = 1;
            return 0;
        }
        _tcscpy(source, TempSrc);
    }


    if (lpdwFlags & COPY_RESTART)
    {
        _tcscpy(TrueDest, dest);
        GetEnvironmentVariable(_T("TEMP"),dest,MAX_PATH);
        _tcscat(dest,_T("\\"));
        FileName = _tcsrchr(TrueDest,_T('\\'));
        FileName++;
        _tcscat(dest,FileName);
    }


    if (!IsExistingFile (dest))
    {
        TRACE ("opening/creating\n");
        hFileDest =
            CreateFile (dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    }
    else if (!append)
    {
        TRACE ("SetFileAttributes (%s, FILE_ATTRIBUTE_NORMAL);\n", debugstr_aw(dest));
        SetFileAttributes (dest, FILE_ATTRIBUTE_NORMAL);

        TRACE ("DeleteFile (%s);\n", debugstr_aw(dest));
        DeleteFile (dest);

        hFileDest =	CreateFile (dest, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    }
    else
    {
        LONG lFilePosHigh = 0;

        if (!_tcscmp (dest, source))
        {
            CloseHandle (hFileSrc);
            return 0;
        }

        TRACE ("opening/appending\n");
        SetFileAttributes (dest, FILE_ATTRIBUTE_NORMAL);

        hFileDest =
            CreateFile (dest, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);

        /* Move to end of file to start writing */
        SetFilePointer (hFileDest, 0, &lFilePosHigh,FILE_END);
    }


    if (hFileDest == INVALID_HANDLE_VALUE)
    {
        CloseHandle (hFileSrc);
        ConOutResPuts(STRING_ERROR_PATH_NOT_FOUND);
        nErrorLevel = 1;
        return 0;
    }

    /* A page-aligned buffer usually give more speed */
    buffer = VirtualAlloc(NULL, BUFF_SIZE, MEM_COMMIT, PAGE_READWRITE);
    if (buffer == NULL)
    {
        CloseHandle (hFileDest);
        CloseHandle (hFileSrc);
        ConOutResPuts(STRING_ERROR_OUT_OF_MEMORY);
        nErrorLevel = 1;
        return 0;
    }

    do
    {
        ReadFile (hFileSrc, buffer, BUFF_SIZE, &dwRead, NULL);
        if (lpdwFlags & COPY_ASCII)
        {
            LPBYTE pEof = memchr(buffer, 0x1A, dwRead);
            if (pEof != NULL)
            {
                bEof = TRUE;
                dwRead = (DWORD)(pEof - buffer);
            }
        }

        if (dwRead == 0)
            break;

        WriteFile (hFileDest, buffer, dwRead, &dwWritten, NULL);
        if (dwWritten != dwRead || CheckCtrlBreak(BREAK_INPUT))
        {
            ConOutResPuts(STRING_COPY_ERROR3);

            VirtualFree (buffer, 0, MEM_RELEASE);
            CloseHandle (hFileDest);
            CloseHandle (hFileSrc);
            nErrorLevel = 1;
            return 0;
        }
    }
    while (!bEof);

    TRACE ("setting time\n");
    if (bHaveTime)
        SetFileTime (hFileDest, &srctime, NULL, NULL);

    VirtualFree (buffer, 0, MEM_RELEASE);
    CloseHandle (hFileDest);
    CloseHandle (hFileSrc);

    TRACE ("setting mode\n");
    /* For MS-DOS backwards-compatibility, always remove the read-only attribute */
    if (dwAttrib != INVALID_FILE_ATTRIBUTES && !append)
        SetFileAttributes (dest, dwAttrib & ~FILE_ATTRIBUTE_READONLY);

    /* Now finish off the copy if needed with CopyFileEx */
    if (lpdwFlags & COPY_RESTART)
    {
        if (!CopyFileEx(dest, TrueDest, NULL, NULL, FALSE, COPY_FILE_RESTARTABLE))
        {
            nErrorLevel = 1;
            DeleteFile(dest);
            return 0;
        }
        /* Take care of file in the temp folder */
        DeleteFile(dest);

    }

    if (lpdwFlags & COPY_DECRYPT)
        DeleteFile(TempSrc);

    return 1;
}


static INT CopyOverwrite (LPTSTR fn)
{
    /*ask the user if they want to override*/
    INT res;
    ConOutResPrintf(STRING_COPY_HELP1, fn);
    res = FilePromptYNA (0);
    return res;
}

/* The following lines of copy were written by someone else
(most likely Eric Kohl) and it was taken from ren.c */
static void
BuildFileName(
    LPTSTR pszSource,
    LPTSTR pszTarget,
    LPTSTR pszOutput)
{
    /* build destination file name */
    while (*pszTarget != 0)
    {
        if (*pszTarget == _T('*'))
        {
            pszTarget++;
            while ((*pszSource != 0) && (*pszSource != *pszTarget))
            {
                *pszOutput++ = *pszSource++;
            }
        }
        else if (*pszTarget == _T('?'))
        {
            pszTarget++;
            if (*pszSource != 0)
            {
                *pszOutput++ = *pszSource++;
            }
        }
        else
        {
            *pszOutput++ = *pszTarget++;
            if (*pszSource != 0)
                pszSource++;
        }
    }

    *pszOutput = 0;
}

typedef struct _COPY_ENTRY
{
    struct _COPY_ENTRY *Next;
    LPTSTR Name;
    INT Binary;
} COPY_ENTRY, *PCOPY_ENTRY;

typedef struct _COPY_STATE
{
    DWORD dwFlags;
    INT DestBinary;
    BOOL bAnyConcats;
    BOOL bAppendFirstSource;
    BOOL bDestIsDirectory;
    BOOL bDestHasWildcard;
    BOOL bWrittenOneConcat;
    BOOL bStop;
    INT nFiles;
    TCHAR szDestPath[MAX_PATH];
    LPTSTR pszDestPattern;
} COPY_STATE, *PCOPY_STATE;

static PCOPY_ENTRY
CopyNewEntry(LPCTSTR pszName, INT Binary)
{
    PCOPY_ENTRY Entry = cmd_alloc(sizeof(*Entry));

    if (Entry == NULL)
        return NULL;

    Entry->Name = cmd_dup(pszName);
    if (Entry->Name == NULL)
    {
        cmd_free(Entry);
        return NULL;
    }

    Entry->Next = NULL;
    Entry->Binary = Binary;
    return Entry;
}

static VOID
CopyFreeEntries(PCOPY_ENTRY Entry)
{
    while (Entry != NULL)
    {
        PCOPY_ENTRY Next = Entry->Next;

        cmd_free(Entry->Name);
        cmd_free(Entry);
        Entry = Next;
    }
}

static BOOL
CopyIsSameFile(LPCTSTR pszPath1, LPCTSTR pszPath2)
{
    BY_HANDLE_FILE_INFORMATION Info1, Info2;
    HANDLE hFile1, hFile2;
    BOOL bSame = FALSE;

    hFile1 = CreateFile(pszPath1, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile1 == INVALID_HANDLE_VALUE)
        return FALSE;

    hFile2 = CreateFile(pszPath2, 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        NULL, OPEN_EXISTING, 0, NULL);
    if (hFile2 != INVALID_HANDLE_VALUE)
    {
        if (GetFileInformationByHandle(hFile1, &Info1) &&
            GetFileInformationByHandle(hFile2, &Info2))
        {
            bSame = (Info1.dwVolumeSerialNumber == Info2.dwVolumeSerialNumber) &&
                    (Info1.nFileIndexHigh == Info2.nFileIndexHigh) &&
                    (Info1.nFileIndexLow == Info2.nFileIndexLow);
        }
        CloseHandle(hFile2);
    }
    CloseHandle(hFile1);
    return bSame;
}

static BOOL
CopyAppendEof(LPCTSTR pszPath)
{
    BYTE Eof = 0x1A;
    DWORD dwWritten;
    HANDLE hFile;
    BOOL bResult;

    hFile = CreateFile(pszPath, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
        return FALSE;

    SetFilePointer(hFile, 0, NULL, FILE_END);
    bResult = WriteFile(hFile, &Eof, sizeof(Eof), &dwWritten, NULL) && (dwWritten == sizeof(Eof));
    CloseHandle(hFile);
    return bResult;
}

static BOOL
CopyIsDevicePath(LPCTSTR pszPath)
{
    return !_tcsncmp(pszPath, _T("\\\\.\\"), 4);
}

static VOID
CopyOneFile(
    PCOPY_STATE State,
    PCOPY_ENTRY Entry,
    LPCTSTR pszSrcPath,
    LPCTSTR pszFileName,
    BOOL bDisplayName)
{
    TCHAR szDest[MAX_PATH];
    TCHAR szWorkSrc[MAX_PATH];
    TCHAR szWorkDest[MAX_PATH];
    TCHAR szName[2 * MAX_PATH];
    LPCTSTR pszName = NULL;
    DWORD dwCopyFlags;
    BOOL bSameFile, bOverwrite, bResult;
    INT nOverwrite;

    if (State->bDestHasWildcard && !State->bAnyConcats)
    {
        BuildFileName((LPTSTR)pszFileName, State->pszDestPattern, szName);
        pszName = szName;
    }
    else if (State->bDestIsDirectory || State->bAppendFirstSource)
    {
        pszName = pszFileName;
    }

    if ((_tcslen(pszSrcPath) >= ARRAYSIZE(szWorkSrc)) ||
        (_tcslen(State->szDestPath) + (pszName ? _tcslen(pszName) : 0) >= ARRAYSIZE(szDest)))
    {
        ConOutFormatMessage(ERROR_FILENAME_EXCED_RANGE, (LPTSTR)pszSrcPath);
        nErrorLevel = 1;
        return;
    }

    _tcscpy(szDest, State->szDestPath);
    if (pszName)
        _tcscat(szDest, pszName);

    bSameFile = CopyIsSameFile(pszSrcPath, szDest);
    if (!State->bAnyConcats && bSameFile)
    {
        ConOutPrintf(_T("%s\n"), pszSrcPath);
        ConOutResPrintf(STRING_COPY_ERROR2);
        nErrorLevel = 1;
        State->bStop = TRUE;
        return;
    }

    bOverwrite = TRUE;
    if (!(State->bAnyConcats && (State->bWrittenOneConcat || bSameFile)) &&
        !CopyIsDevicePath(szDest) &&
        (((!(State->dwFlags & COPY_NO_PROMPT)) && IsExistingFile(szDest)) || (State->dwFlags & COPY_PROMPT)))
    {
        nOverwrite = CopyOverwrite(szDest);
        if (nOverwrite == PROMPT_BREAK)
        {
            State->bStop = TRUE;
            return;
        }
        if (nOverwrite == PROMPT_NO)
            bOverwrite = FALSE;
        if (nOverwrite == PROMPT_ALL || (nOverwrite == PROMPT_YES && State->bAnyConcats))
            State->dwFlags |= COPY_NO_PROMPT;
    }
    if (!bOverwrite)
        return;

    if (State->bAppendFirstSource)
    {
        _tcscpy(State->szDestPath, szDest);
        State->bAppendFirstSource = FALSE;
        State->bDestIsDirectory = FALSE;
    }

    if (bDisplayName)
        ConOutPrintf(_T("%s\n"), pszSrcPath);

    dwCopyFlags = State->dwFlags & ~(COPY_ASCII | COPY_BINARY);
    if (!Entry->Binary)
        dwCopyFlags |= COPY_ASCII;

    _tcscpy(szWorkSrc, pszSrcPath);
    _tcscpy(szWorkDest, szDest);
    if (State->bAnyConcats && bSameFile)
        bResult = copy(szWorkSrc, szWorkSrc, FALSE, dwCopyFlags, TRUE);
    else
        bResult = copy(szWorkSrc, szWorkDest, State->bAnyConcats && State->bWrittenOneConcat, dwCopyFlags, FALSE);

    if (!bResult)
    {
        ConOutResPrintf(STRING_COPY_ERROR3);
        ConOutFormatMessage(GetLastError(), (LPTSTR)pszSrcPath);
        nErrorLevel = 1;
        return;
    }

    if (State->bAnyConcats)
    {
        State->bWrittenOneConcat = TRUE;
        State->nFiles = 1;
    }
    else
    {
        State->nFiles++;
        if ((State->DestBinary == 0) && !Entry->Binary && !CopyAppendEof(szDest))
        {
            ConOutFormatMessage(GetLastError(), szDest);
            nErrorLevel = 1;
        }
    }
}

static VOID
CopySource(PCOPY_STATE State, PCOPY_ENTRY Entry)
{
    TCHAR szSrcPath[MAX_PATH];
    WIN32_FIND_DATA findBuffer;
    LPTSTR pszFilePart = NULL;
    BOOL bDisplayName = State->bAnyConcats;
    BOOL bHasWildcard;
    HANDLE hFind;
    DWORD dwLength;

    if (Entry->Binary == -1)
        Entry->Binary = !State->bAnyConcats;

    if ((Entry->Name[0] != _T('\0')) && (Entry->Name[1] == _T(':')) && (Entry->Name[2] == _T('\0')))
    {
        ConOutPrintf(_T("%s\n"), Entry->Name);
        ConOutFormatMessage(ERROR_FILE_NOT_FOUND, Entry->Name);
        nErrorLevel = 1;
        State->bStop = TRUE;
        return;
    }

    dwLength = GetFullPathName(Entry->Name, ARRAYSIZE(szSrcPath) - 2, szSrcPath, &pszFilePart);
    if (dwLength == 0 || dwLength >= ARRAYSIZE(szSrcPath) - 2)
    {
        ConOutFormatMessage(ERROR_FILE_NOT_FOUND, Entry->Name);
        nErrorLevel = 1;
        State->bStop = TRUE;
        return;
    }

    if (CopyIsDevicePath(szSrcPath))
    {
        CopyOneFile(State, Entry, szSrcPath, &szSrcPath[4], bDisplayName);
        return;
    }

    bHasWildcard = (_tcspbrk(szSrcPath, _T("*?")) != NULL);
    if (szSrcPath[_tcslen(szSrcPath) - 1] == _T('\\'))
    {
        _tcscat(szSrcPath, _T("*"));
        bDisplayName = TRUE;
    }
    else if (!bHasWildcard && IsExistingDirectory(szSrcPath))
    {
        _tcscat(szSrcPath, _T("\\*"));
        bDisplayName = TRUE;
    }
    if (bHasWildcard)
        bDisplayName = TRUE;

    hFind = FindFirstFile(szSrcPath, &findBuffer);
    if (hFind == INVALID_HANDLE_VALUE)
    {
        if (!State->bAnyConcats || !State->bWrittenOneConcat)
        {
            if (bDisplayName)
                ConOutPrintf(_T("%s\n"), szSrcPath);
            ConOutFormatMessage(GetLastError(), szSrcPath);
            nErrorLevel = 1;
            State->bStop = TRUE;
        }
        return;
    }

    pszFilePart = _tcsrchr(szSrcPath, _T('\\'));
    pszFilePart = pszFilePart ? pszFilePart + 1 : szSrcPath;

    do
    {
        if (CheckCtrlBreak(BREAK_INPUT))
        {
            State->bStop = TRUE;
            break;
        }

        if (findBuffer.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;

        if ((pszFilePart - szSrcPath) + _tcslen(findBuffer.cFileName) >= ARRAYSIZE(szSrcPath))
            continue;

        _tcscpy(pszFilePart, findBuffer.cFileName);
        CopyOneFile(State, Entry, szSrcPath, findBuffer.cFileName, bDisplayName);
    } while (!State->bStop && FindNextFile(hFind, &findBuffer));

    FindClose(hFind);
}

static BOOL
CopyParseSwitch(LPCTSTR pszArg, PDWORD pdwFlags, PINT pBinaryMode)
{
    if (_tcslen(pszArg) < 2)
        return TRUE;

    switch (_totupper(pszArg[1]))
    {
        case _T('A'):
            *pBinaryMode = 0;
            break;

        case _T('B'):
            *pBinaryMode = 1;
            break;

        case _T('D'):
            *pdwFlags |= COPY_DECRYPT;
            break;

        case _T('V'):
            *pdwFlags |= COPY_VERIFY;
            break;

        case _T('N'):
            *pdwFlags |= COPY_SHORTNAME;
            break;

        case _T('Y'):
            *pdwFlags |= COPY_NO_PROMPT;
            *pdwFlags &= ~COPY_PROMPT;
            break;

        case _T('-'):
            if ((_tcslen(pszArg) >= 3) && (_totupper(pszArg[2]) == _T('Y')))
            {
                *pdwFlags &= ~COPY_NO_PROMPT;
                *pdwFlags |= COPY_PROMPT;
            }
            break;

        case _T('Z'):
            *pdwFlags |= COPY_RESTART;
            break;

        default:
            ConOutResPrintf(STRING_ERROR_INVALID_SWITCH, _totupper(pszArg[1]));
            return FALSE;
    }

    return TRUE;
}

static INT
CopyRun(PCOPY_ENTRY Sources, PCOPY_ENTRY Destination, INT BinaryMode, BOOL bAnyConcats, DWORD dwFlags)
{
    COPY_STATE State;
    PCOPY_ENTRY Entry;
    LPTSTR pszName;

    ZeroMemory(&State, sizeof(State));
    State.dwFlags = dwFlags;
    State.bAnyConcats = bAnyConcats;

    if (Destination == NULL)
    {
        GetCurrentDirectory(ARRAYSIZE(State.szDestPath) - 1, State.szDestPath);
        State.bDestIsDirectory = TRUE;
        State.DestBinary = BinaryMode;
    }
    else
    {
        if ((Destination->Name[0] != _T('\0')) && (Destination->Name[1] == _T(':')) &&
            (Destination->Name[2] == _T('\0')))
        {
            GetRootPath(Destination->Name, State.szDestPath, ARRAYSIZE(State.szDestPath) - 1);
        }
        else
        {
            DWORD dwLength = GetFullPathName(Destination->Name, ARRAYSIZE(State.szDestPath) - 1,
                                             State.szDestPath, NULL);
            if (dwLength == 0 || dwLength >= ARRAYSIZE(State.szDestPath) - 1)
            {
                ConOutFormatMessage(ERROR_FILENAME_EXCED_RANGE, Destination->Name);
                nErrorLevel = 1;
                return 1;
            }
        }

        pszName = _tcsrchr(State.szDestPath, _T('\\'));
        pszName = pszName ? pszName + 1 : State.szDestPath;
        State.bDestHasWildcard = (_tcspbrk(pszName, _T("*?")) != NULL);
        if (!State.bDestHasWildcard &&
            ((*pszName == _T('\0')) || IsExistingDirectory(State.szDestPath)))
        {
            State.bDestIsDirectory = TRUE;
        }
        State.DestBinary = Destination->Binary;
    }

    if (State.bDestIsDirectory &&
        (State.szDestPath[0] != _T('\0')) &&
        (State.szDestPath[_tcslen(State.szDestPath) - 1] != _T('\\')))
    {
        _tcscat(State.szDestPath, _T("\\"));
    }

    if (State.bDestHasWildcard)
    {
        pszName = _tcsrchr(State.szDestPath, _T('\\'));
        pszName = pszName ? pszName + 1 : State.szDestPath;
        State.pszDestPattern = cmd_dup(pszName);
        if (State.pszDestPattern == NULL)
        {
            ConOutResPuts(STRING_ERROR_OUT_OF_MEMORY);
            nErrorLevel = 1;
            return 1;
        }
        *pszName = _T('\0');
    }

    if (State.bAnyConcats)
    {
        if (State.bDestIsDirectory)
            State.bAppendFirstSource = TRUE;
        if (State.DestBinary == -1)
            State.DestBinary = 0;
    }
    else if (!State.bDestIsDirectory && !State.bDestHasWildcard)
    {
        if (_tcspbrk(Sources->Name, _T("*?")) != NULL)
        {
            State.bAnyConcats = TRUE;
            if (State.DestBinary == -1)
                State.DestBinary = 0;
        }
        else if (State.DestBinary == -1)
        {
            State.DestBinary = 1;
        }
    }

    for (Entry = Sources; (Entry != NULL) && !State.bStop; Entry = Entry->Next)
        CopySource(&State, Entry);

    if (State.bAnyConcats && State.bWrittenOneConcat && (State.DestBinary == 0) &&
        !CopyAppendEof(State.szDestPath))
    {
        ConOutFormatMessage(GetLastError(), State.szDestPath);
        nErrorLevel = 1;
    }

    if (State.pszDestPattern != NULL)
        cmd_free(State.pszDestPattern);

    ConOutResPrintf(STRING_COPY_FILE, State.nFiles);
    return 0;
}

INT cmd_copy(LPTSTR param)
{
    LPTSTR *arg;
    INT argc, i;
    DWORD dwFlags = 0;
    INT BinaryMode = -1;
    BOOL bConcatNext = FALSE, bAnyConcats = FALSE, bError = FALSE;
    PCOPY_ENTRY Sources = NULL, LastSource = NULL, Destination = NULL, Entry;
    TCHAR *evar;
    int size;
    INT nResult;


    if (!_tcsncmp(param, _T("/?"), 2))
    {
        ConOutResPaging(TRUE, STRING_COPY_HELP2);
        return 0;
    }

    nErrorLevel = 0;

    evar = cmd_alloc(512 * sizeof(TCHAR));
    if (evar == NULL)
        size = 0;
    else
        size = GetEnvironmentVariable (_T("COPYCMD"), evar, 512);

    if (size > 512)
    {
        TCHAR *old_evar = evar;
        evar = cmd_realloc(evar,size * sizeof(TCHAR) );
        if (evar!=NULL)
            size = GetEnvironmentVariable (_T("COPYCMD"), evar, size);
        else
        {
            size=0;
            evar = old_evar;
        }
    }

    if (size != 0)
    {
        int t = 0;

        for (t = 0; t < size; t++)
        {
            if (_tcsncicmp(_T("/Y"),&evar[t],2) == 0)
            {
                dwFlags |= COPY_NO_PROMPT;
                t++;
            }
            else if (_tcsncicmp(_T("/-Y"),&evar[t],3) == 0)
            {
                dwFlags |= COPY_PROMPT;
                t+=2;
            }
        }
    }
    cmd_free(evar);


    arg = split(param, &argc, FALSE, TRUE);
    if (arg == NULL)
    {
        ConOutResPuts(STRING_ERROR_OUT_OF_MEMORY);
        nErrorLevel = 1;
        return 1;
    }

    for (i = 0; (i < argc) && !bError; i++)
    {
        if (*arg[i] == _T('/'))
        {
            INT NewMode = BinaryMode;

            if (!CopyParseSwitch(arg[i], &dwFlags, &NewMode))
            {
                bError = TRUE;
                break;
            }
            if (NewMode != BinaryMode)
            {
                BinaryMode = NewMode;
                if (Destination != NULL)
                    Destination->Binary = BinaryMode;
                else if (LastSource != NULL)
                    LastSource->Binary = BinaryMode;
            }
            continue;
        }

        if (*arg[i] == _T('+'))
        {
            if (LastSource == NULL)
            {
                ConErrResPrintf(STRING_ERROR_INVALID_PARAM_FORMAT, arg[i]);
                bError = TRUE;
                break;
            }
            bConcatNext = TRUE;
            bAnyConcats = TRUE;
            continue;
        }

        if (_tcsspn(arg[i], _T(",")) == _tcslen(arg[i]))
            continue;

        Entry = CopyNewEntry(arg[i], BinaryMode);
        if (Entry == NULL)
        {
            ConOutResPuts(STRING_ERROR_OUT_OF_MEMORY);
            bError = TRUE;
            break;
        }

        if (Sources == NULL)
        {
            Sources = LastSource = Entry;
        }
        else if (bConcatNext)
        {
            LastSource->Next = Entry;
            LastSource = Entry;
        }
        else if (Destination == NULL)
        {
            Destination = Entry;
        }
        else
        {
            cmd_free(Entry->Name);
            cmd_free(Entry);
            ConErrResPrintf(STRING_ERROR_TOO_MANY_PARAMETERS, _T(""));
            bError = TRUE;
            break;
        }
        bConcatNext = FALSE;
    }

    freep(arg);

    if (!bError && (Sources == NULL))
    {
        ConOutResPuts(STRING_ERROR_REQ_PARAM_MISSING);
        bError = TRUE;
    }

    if (bError)
    {
        CopyFreeEntries(Sources);
        CopyFreeEntries(Destination);
        nErrorLevel = 1;
        return 1;
    }

    if (bc != NULL)
    {
        dwFlags |= COPY_NO_PROMPT;
        dwFlags &= ~COPY_PROMPT;
    }

    nResult = CopyRun(Sources, Destination, BinaryMode, bAnyConcats, dwFlags);

    CopyFreeEntries(Sources);
    CopyFreeEntries(Destination);
    return nResult;
}

#endif /* INCLUDE_CMD_COPY */
