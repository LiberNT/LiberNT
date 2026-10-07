/*
 *  FILECOMP.C - handles filename completion.
 *
 *
 *  Comments:
 *
 *    30-Jul-1998 (John P Price <linux-guru@gcfl.net>)
 *       moved from command.c file
 *       made second TAB display list of filename matches
 *       made filename be lower case if last character typed is lower case
 *
 *    25-Jan-1999 (Eric Kohl)
 *       Cleanup. Unicode safe!
 *
 *    30-Apr-2004 (Filip Navara <xnavara@volny.cz>)
 *       Make the file listing readable when there is a lot of long names.
 *

 *    05-Jul-2004 (Jens Collin <jens.collin@lakhei.com>)
 *       Now expands lfn even when trailing " is omitted.
 */

#include "precomp.h"

#ifdef FEATURE_UNIX_FILENAME_COMPLETION

typedef struct _FileName
{
    TCHAR Name[MAX_PATH];
} FileName;

VOID FindPrefixAndSuffix(LPTSTR strIN, LPTSTR szPrefix, LPTSTR szSuffix)
{
    /* String that is to be examined */
    TCHAR str[MAX_PATH];
    /* temp pointers to used to find needed parts */
    TCHAR * szSearch;
    TCHAR * szSearch1;
    TCHAR * szSearch2;
    TCHAR * szSearch3;
    /* number of quotes in the string */
    INT nQuotes = 0;
    /* used in for loops */
    UINT i;
    /* Char number to break the string at */
    INT PBreak = 0;
    INT SBreak = 0;
    /* when phrasing a string, this tells weather
       you are inside quotes ot not. */
    BOOL bInside = FALSE;

    szPrefix[0] = _T('\0');
    szSuffix[0] = _T('\0');

    /* Copy over the string to later be edited */
    _tcscpy(str,strIN);

    /* Count number of " */
    for(i = 0; i < _tcslen(str); i++)
    {
        if (str[i] == _T('\"'))
            nQuotes++;
    }

    /* Find the prefix and suffix */
    if (nQuotes % 2 && nQuotes >= 1)
    {
        /* Odd number of quotes.  Just start from the last " */
        /* THis is the way MS does it, and is an easy way out */
        szSearch = _tcsrchr(str, _T('\"'));
        /* Move to the next char past the " */
        szSearch++;
        _tcscpy(szSuffix,szSearch);
        /* Find the one closest to end */
        szSearch1 = _tcsrchr(str, _T('\"'));
        szSearch2 = _tcsrchr(str, _T('\\'));
        szSearch3 = _tcsrchr(str, _T('/'));
        if ((szSearch2 != NULL) && (szSearch1 < szSearch2))
            szSearch = szSearch2;
        else if ((szSearch3 != NULL) && (szSearch1 < szSearch3))
            szSearch = szSearch3;
        else
            szSearch = szSearch1;
        /* Move one char past */
        szSearch++;
        szSearch[0] = _T('\0');
        _tcscpy(szPrefix,str);
        return;

    }

    if (!_tcschr(str, _T(' ')))
    {
        /* No spaces, everything goes to Suffix */
        _tcscpy(szSuffix,str);
        /* look for a slash just in case */
        szSearch = _tcsrchr(str, _T('\\'));
        if (szSearch)
        {
            szSearch++;
            szSearch[0] = _T('\0');
            _tcscpy(szPrefix,str);
        }
        else
        {
            szPrefix[0] = _T('\0');
        }
        return;
    }

    if (!nQuotes)
    {
        /* No quotes, and there is a space*/
        /* Take it after the last space */
        szSearch = _tcsrchr(str, _T(' '));
        szSearch++;
        _tcscpy(szSuffix,szSearch);
        /* Find the closest to the end space or \ */
        _tcscpy(str,strIN);
        szSearch1 = _tcsrchr(str, _T(' '));
        szSearch2 = _tcsrchr(str, _T('\\'));
        szSearch3 = _tcsrchr(str, _T('/'));
        if ((szSearch2 != NULL) && (szSearch1 < szSearch2))
            szSearch = szSearch2;
        else if ((szSearch3 != NULL) && (szSearch1 < szSearch3))
            szSearch = szSearch3;
        else
            szSearch = szSearch1;
        szSearch++;
        szSearch[0] = _T('\0');
        _tcscpy(szPrefix,str);
        return;
    }

    /* All else fails and there is a lot of quotes, spaces and |
       Then we search through and find the last space or \ that is
        not inside a quotes */
    for(i = 0; i < _tcslen(str); i++)
    {
        if (str[i] == _T('\"'))
            bInside = !bInside;
        if (str[i] == _T(' ') && !bInside)
            SBreak = i;
        if ((str[i] == _T(' ') || str[i] == _T('\\')) && !bInside)
            PBreak = i;
    }
    SBreak++;
    PBreak++;
    _tcscpy(szSuffix,&strIN[SBreak]);
    strIN[PBreak] = _T('\0');
    _tcscpy(szPrefix,strIN);
    if (szPrefix[_tcslen(szPrefix) - 2] == _T('\"') &&
        szPrefix[_tcslen(szPrefix) - 1] != _T(' '))
    {
        /* need to remove the " right before a \ at the end to
           allow the next stuff to stay inside one set of quotes
            otherwise you would have multiple sets of quotes*/
        _tcscpy(&szPrefix[_tcslen(szPrefix) - 2],_T("\\"));
    }
}

int __cdecl compare(const void *arg1,const void *arg2)
{
    FileName * File1;
    FileName * File2;
    INT ret;

    File1 = cmd_alloc(sizeof(FileName));
    if (!File1)
        return 0;

    File2 = cmd_alloc(sizeof(FileName));
    if (!File2)
    {
        cmd_free(File1);
        return 0;
    }

    memcpy(File1,arg1,sizeof(FileName));
    memcpy(File2,arg2,sizeof(FileName));

     /* ret = _tcsicmp(File1->Name, File2->Name); */
     ret = lstrcmpi(File1->Name, File2->Name);

    cmd_free(File1);
    cmd_free(File2);
    return ret;
}

BOOL
FileNameContainsSpecialCharacters(LPTSTR pszFileName)
{
    TCHAR chr;

    while ((chr = *pszFileName++) != _T('\0'))
    {
        if ((chr == _T(' ')) ||
            (chr == _T('!')) ||
            (chr == _T('%')) ||
            (chr == _T('&')) ||
            (chr == _T('(')) ||
            (chr == _T(')')) ||
            (chr == _T('{')) ||
            (chr == _T('}')) ||
            (chr == _T('[')) ||
            (chr == _T(']')) ||
            (chr == _T('=')) ||
            (chr == _T('\'')) ||
            (chr == _T('`')) ||
            (chr == _T(',')) ||
            (chr == _T(';')) ||
            (chr == _T('^')) ||
            (chr == _T('~')) ||
            (chr == _T('+')) ||
            (chr == 0xB4)) // '´'
        {
            return TRUE;
        }
    }

    return FALSE;
}

static BOOL
IsExecutableCompletion(
    IN LPCTSTR FileName,
    IN LPCTSTR PathExt)
{
    LPCTSTR Extension = _tcsrchr(FileName, _T('.'));
    LPCTSTR Start;
    LPCTSTR End;
    SIZE_T ExtensionLength;

    if (!Extension)
        return FALSE;
    ExtensionLength = _tcslen(Extension);

    Start = PathExt;
    while (*Start)
    {
        while (*Start == _T(';') || _istspace(*Start))
            Start++;
        End = _tcschr(Start, _T(';'));
        if (!End)
            End = Start + _tcslen(Start);
        while (End > Start && _istspace(End[-1]))
            End--;

        if ((SIZE_T)(End - Start) == ExtensionLength &&
            !_tcsnicmp(Start, Extension, ExtensionLength))
        {
            return TRUE;
        }
        Start = *End ? End + 1 : End;
    }
    return FALSE;
}

static BOOL
AddCommandCompletion(
    IN OUT FileName **FileList,
    IN OUT INT *FileListSize,
    IN LPCTSTR Name)
{
    FileName *NewList;
    INT Index;

    for (Index = 0; Index < *FileListSize; Index++)
    {
        if (!_tcsicmp((*FileList)[Index].Name, Name))
            return TRUE;
    }

    NewList = cmd_realloc(*FileList,
                          (*FileListSize + 1) * sizeof(**FileList));
    if (!NewList)
        return FALSE;

    *FileList = NewList;
    _tcsncpy(NewList[*FileListSize].Name, Name, MAX_PATH - 1);
    NewList[*FileListSize].Name[MAX_PATH - 1] = _T('\0');
    (*FileListSize)++;
    return TRUE;
}

static BOOL
AddCommandFilesFromDirectory(
    IN LPCTSTR Directory,
    IN LPCTSTR Prefix,
    IN LPCTSTR PathExt,
    IN OUT FileName **FileList,
    IN OUT INT *FileListSize)
{
    TCHAR SearchPath[MAX_PATH];
    WIN32_FIND_DATA File;
    HANDLE Find;
    SIZE_T DirectoryLength = Directory ? _tcslen(Directory) : 0;
    SIZE_T PrefixLength = _tcslen(Prefix);
    BOOL Result = TRUE;

    if (DirectoryLength)
    {
        BOOL NeedsSeparator = Directory[DirectoryLength - 1] != _T('\\') &&
                              Directory[DirectoryLength - 1] != _T('/');
        if (DirectoryLength + NeedsSeparator + PrefixLength + 2 > ARRAYSIZE(SearchPath))
            return TRUE;
        _tcscpy(SearchPath, Directory);
        if (NeedsSeparator)
            _tcscat(SearchPath, _T("\\"));
        _tcscat(SearchPath, Prefix);
    }
    else
    {
        if (PrefixLength + 2 > ARRAYSIZE(SearchPath))
            return TRUE;
        _tcscpy(SearchPath, Prefix);
    }
    _tcscat(SearchPath, _T("*"));

    Find = FindFirstFile(SearchPath, &File);
    if (Find == INVALID_HANDLE_VALUE)
        return TRUE;

    do
    {
        if (!(File.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            IsExecutableCompletion(File.cFileName, PathExt) &&
            !AddCommandCompletion(FileList, FileListSize, File.cFileName))
        {
            Result = FALSE;
            break;
        }
    } while (FindNextFile(Find, &File));

    FindClose(Find);
    return Result;
}

static BOOL
GetStartedCommandPrefix(
    IN LPCTSTR Line,
    IN UINT Cursor,
    OUT LPTSTR Prefix,
    OUT UINT *PrefixStart)
{
    UINT Index = 0;
    SIZE_T Length = _tcslen(Line);

    /* Command-name completion is intentionally limited to a started first
     * token at the end of the line.  Paths and later arguments retain the
     * existing filename completion behavior. */
    if (Cursor != Length)
        return FALSE;
    while (Index < Cursor && _istspace(Line[Index]))
        Index++;
    if (Index < Cursor && Line[Index] == _T('@'))
        Index++;
    if (Index == Cursor || Cursor - Index >= MAX_PATH)
        return FALSE;

    *PrefixStart = Index;
    for (; Index < Cursor; Index++)
    {
        if (_istspace(Line[Index]) ||
            _tcschr(_T("\\/\":&|<>()*?"), Line[Index]))
        {
            return FALSE;
        }
    }

    _tcsncpy(Prefix, &Line[*PrefixStart], Cursor - *PrefixStart);
    Prefix[Cursor - *PrefixStart] = _T('\0');
    return TRUE;
}

static VOID
ShowCompletionMatches(
    IN FileName *FileList,
    IN INT FileListSize)
{
    INPUT_RECORD ir;
    SHORT ScreenWidth;
    WORD Key;
    INT Width = 0;
    INT Length;
    INT Columns;
    INT Rows;
    INT Row;
    INT Column;
    INT Index;

    if (FileListSize >= 100)
    {
        ConOutChar(_T('\n'));
        ConOutPrintf(_T("Display all %d possibilities? (y or n)"), FileListSize);
        for (;;)
        {
            ConInKey(&ir);
            Key = ir.Event.KeyEvent.wVirtualKeyCode;
            if (Key == _T('Y') || Key == VK_SPACE)
                break;
            if (Key == _T('N') || Key == VK_BACK ||
                Key == VK_DELETE || Key == VK_ESCAPE ||
                (Key == _T('C') &&
                 (ir.Event.KeyEvent.dwControlKeyState &
                  (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))))
            {
                ConOutChar(_T('\n'));
                return;
            }
        }
    }

    for (Index = 0; Index < FileListSize; Index++)
    {
        Length = (INT)ConGetTextWidth(FileList[Index].Name);
        if (Length > Width)
            Width = Length;
    }
    Width += 2;

    GetScreenSize(&ScreenWidth, NULL);
    Columns = ScreenWidth / Width;
    if (Columns > 1 && Columns * Width == ScreenWidth)
        Columns--;
    if (Columns < 1)
        Columns = 1;
    Rows = (FileListSize + Columns - 1) / Columns;

    ConOutChar(_T('\n'));
    for (Row = 0; Row < Rows; Row++)
    {
        for (Column = 0; Column < Columns; Column++)
        {
            Index = Column * Rows + Row;
            if (Index >= FileListSize)
                break;

            Length = 0;
            if (Column + 1 < Columns)
                Length = Width - (INT)ConGetTextWidth(FileList[Index].Name);
            ConOutPrintf(_T("%s%*s"), FileList[Index].Name, Length, _T(""));
        }
        ConOutChar(_T('\n'));
    }
}

static BOOL
SelectCompletion(
    IN FileName *FileList,
    IN INT FileListSize,
    IN LPCTSTR Typed,
    IN BOOL bList,
    OUT LPTSTR Word,
    OUT PBOOL Listed)
{
    SIZE_T TypedLength = _tcslen(Typed);
    SIZE_T Length;
    SIZE_T Common;
    INT Index;

    _tcscpy(Word, FileList[0].Name);
    if (FileListSize == 1)
        return TRUE;

    if (bList)
    {
        ShowCompletionMatches(FileList, FileListSize);
        *Listed = TRUE;
        return FALSE;
    }

    Length = _tcslen(Word);
    for (Index = 1; Index < FileListSize; Index++)
    {
        for (Common = 0; Common < Length; Common++)
        {
            if (_totlower(Word[Common]) !=
                _totlower(FileList[Index].Name[Common]))
            {
                break;
            }
        }
        Length = Common;
    }
    if (Length && IS_HIGH_SURROGATE(Word[Length - 1]))
        Length--;
    Word[Length] = _T('\0');

    MessageBeep(-1);
    return Length > TypedLength && !_tcsnicmp(Word, Typed, TypedLength);
}

BOOL
CompleteCommand(
    LPTSTR strIN,
    BOOL bList,
    LPTSTR strOut,
    UINT Cursor,
    PBOOL Listed)
{
    static const TCHAR DefaultPathExt[] = _T(".COM;.EXE;.BAT;.CMD");
    TCHAR SearchPrefix[MAX_PATH];
    TCHAR Word[MAX_PATH];
    FileName *FileList = NULL;
    INT FileListSize = 0;
    LPTSTR PathExt = NULL;
    LPTSTR Path = NULL;
    LPTSTR Directory;
    LPTSTR End;
    DWORD Length;
    UINT PrefixStart;
    UINT Index;
    BOOL NeededQuote;
    SIZE_T ResultLength;

    strOut[0] = _T('\0');
    if (Cursor >= MAX_PATH ||
        !GetStartedCommandPrefix(strIN, Cursor,
                                 SearchPrefix, &PrefixStart))
    {
        return FALSE;
    }

    Length = GetEnvironmentVariable(_T("PATHEXT"), NULL, 0);
    if (Length)
    {
        PathExt = cmd_alloc(Length * sizeof(*PathExt));
        if (PathExt)
            GetEnvironmentVariable(_T("PATHEXT"), PathExt, Length);
    }
    else
    {
        PathExt = cmd_alloc(sizeof(DefaultPathExt));
        if (PathExt)
            _tcscpy(PathExt, DefaultPathExt);
    }
    if (!PathExt)
        goto OutOfMemory;

    for (Index = 0; cmds[Index].name; Index++)
    {
        if (!_tcsnicmp(cmds[Index].name,
                       SearchPrefix,
                       _tcslen(SearchPrefix)) &&
            !AddCommandCompletion(&FileList,
                                  &FileListSize,
                                  cmds[Index].name))
        {
            goto OutOfMemory;
        }
    }

    if (!AddCommandFilesFromDirectory(NULL,
                                      SearchPrefix,
                                      PathExt,
                                      &FileList,
                                      &FileListSize))
    {
        goto OutOfMemory;
    }

    Length = GetEnvironmentVariable(_T("PATH"), NULL, 0);
    if (Length)
    {
        Path = cmd_alloc(Length * sizeof(*Path));
        if (!Path)
            goto OutOfMemory;
        GetEnvironmentVariable(_T("PATH"), Path, Length);

        Directory = Path;
        while (Directory)
        {
            End = _tcschr(Directory, _T(';'));
            if (End)
                *End = _T('\0');
            while (_istspace(*Directory))
                Directory++;
            Length = _tcslen(Directory);
            while (Length && _istspace(Directory[Length - 1]))
                Directory[--Length] = _T('\0');
            if (Length >= 2 && Directory[0] == _T('"') &&
                Directory[Length - 1] == _T('"'))
            {
                Directory[Length - 1] = _T('\0');
                Directory++;
            }
            if (*Directory &&
                !AddCommandFilesFromDirectory(Directory,
                                              SearchPrefix,
                                              PathExt,
                                              &FileList,
                                              &FileListSize))
            {
                goto OutOfMemory;
            }
            Directory = End ? End + 1 : NULL;
        }
    }

    if (!FileListSize)
    {
        cmd_free(Path);
        cmd_free(PathExt);
        return FALSE;
    }

    qsort(FileList, FileListSize, sizeof(*FileList), compare);
    if (!SelectCompletion(FileList, FileListSize, SearchPrefix,
                          bList, Word, Listed))
    {
        _tcscpy(strOut, strIN);
        cmd_free(FileList);
        cmd_free(Path);
        cmd_free(PathExt);
        return TRUE;
    }

    NeededQuote = FileNameContainsSpecialCharacters(Word);
    ResultLength = PrefixStart + _tcslen(Word) +
                   (NeededQuote ? 2 : 0) + 1;
    if (ResultLength >= MAX_PATH)
    {
        cmd_free(FileList);
        cmd_free(Path);
        cmd_free(PathExt);
        return FALSE;
    }

    _tcsncpy(strOut, strIN, PrefixStart);
    strOut[PrefixStart] = _T('\0');
    if (NeededQuote)
        _tcscat(strOut, _T("\""));
    _tcscat(strOut, Word);
    if (FileListSize == 1)
    {
        if (NeededQuote)
            _tcscat(strOut, _T("\""));
        _tcscat(strOut, _T(" "));
    }

    cmd_free(FileList);
    cmd_free(Path);
    cmd_free(PathExt);
    return TRUE;

OutOfMemory:
    cmd_free(FileList);
    cmd_free(Path);
    cmd_free(PathExt);
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    ConOutFormatMessage(GetLastError());
    return FALSE;
}


VOID CompleteFilename (LPTSTR strIN, BOOL bList, LPTSTR strOut, UINT cusor, PBOOL Listed)
{
    /* Length of string before we complete it */
    INT_PTR StartLength;
    /* Length of string after completed */
    //INT EndLength;
    /* The number of chars added too it */
    //static INT DiffLength = 0;
    /* Used to find and assemble the string that is returned */
    TCHAR szBaseWord[MAX_PATH];
    TCHAR szPrefix[MAX_PATH];
    TCHAR szOriginal[MAX_PATH];
    TCHAR szSearchPath[MAX_PATH];
    TCHAR szWord[MAX_PATH];
    LPTSTR szTyped;
    /* Used to search for files */
    HANDLE hFile;
    WIN32_FIND_DATA file;
    /* List of all the files */
    FileName * FileList = NULL;
    /* Number of files */
    INT FileListSize = 0;
    /* Used for loops */
    UINT i;
    /* Editable string of what was passed in */
    TCHAR str[MAX_PATH];
    BOOL NeededQuote = FALSE;
    BOOL ShowAll = TRUE;
    TCHAR * line = strIN;

    strOut[0] = _T('\0');

    while (_istspace (*line))
        line++;
    if (!_tcsnicmp (line, _T("rd "), 3) || !_tcsnicmp (line, _T("cd "), 3))
        ShowAll = FALSE;

    /* Copy the string, str can be edited and original should not be */
    _tcscpy(str,strIN);
    _tcscpy(szOriginal,strIN);

    /* Look to see if the cusor is not at the end of the string */
    if ((cusor + 1) < _tcslen(str))
        str[cusor] = _T('\0');

    /* We need to know how many chars we added from the start */
    StartLength = _tcslen(str);

    /* no string, we need all files in that directory */
    if (!StartLength)
    {
        _tcscat(str,_T("*"));
    }

    /* Zero it out first */
    szBaseWord[0] = _T('\0');
    szPrefix[0] = _T('\0');

    /*What comes out of this needs to be:
        szBaseWord =  path no quotes to the object
        szPrefix = what leads up to the filename
        no quote at the END of the full name */
    FindPrefixAndSuffix(str,szPrefix,szBaseWord);
    /* Strip quotes */
    for(i = 0; i < _tcslen(szBaseWord); )
    {
        if (szBaseWord[i] == _T('\"'))
            memmove(&szBaseWord[i],&szBaseWord[i + 1], _tcslen(&szBaseWord[i]) * sizeof(TCHAR));
        else
            i++;
    }

    /* clear it out */
    memset(szSearchPath, 0, sizeof(szSearchPath));

    /* Start the search for all the files */
    GetFullPathName(szBaseWord, MAX_PATH, szSearchPath, NULL);

    /* Got a device path? Fallback to the the current dir plus the short path */
    if (szSearchPath[0] == _T('\\') && szSearchPath[1] == _T('\\') &&
        szSearchPath[2] == _T('.') && szSearchPath[3] == _T('\\'))
    {
        GetCurrentDirectory(MAX_PATH, szSearchPath);
        _tcscat(szSearchPath, _T("\\"));
        _tcscat(szSearchPath, szBaseWord);
    }

    if (StartLength > 0)
    {
        _tcscat(szSearchPath,_T("*"));
    }

    szTyped = szBaseWord + _tcslen(szBaseWord);
    while (StartLength && szTyped > szBaseWord &&
           !_tcschr(_T("\\/:"), szTyped[-1]))
    {
        szTyped--;
    }

    /* search for the files it might be */
    hFile = FindFirstFile (szSearchPath, &file);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        /* Assemble the original string and return */
        _tcscpy(strOut,szOriginal);
        return;
    }

    /* assemble a list of all files names */
    do
    {
        FileName * oldFileList = FileList;

        if (!_tcscmp (file.cFileName, _T(".")) ||
            !_tcscmp (file.cFileName, _T("..")))
            continue;

        /* Don't show files when they are doing 'cd' or 'rd' */
        if (!ShowAll &&
            file.dwFileAttributes != INVALID_FILE_ATTRIBUTES &&
            !(file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        {
                continue;
        }

        /* Add the file to the list of files */
        FileList = cmd_realloc(FileList, ++FileListSize * sizeof(FileName));

        if (FileList == NULL)
        {
            /* Don't leak old buffer */
            cmd_free(oldFileList);
            /* Assemble the original string and return */
            _tcscpy(strOut,szOriginal);
            FindClose(hFile);
            ConOutFormatMessage (GetLastError());
            return;
        }
        /* Copies the file name into the struct */
        _tcscpy(FileList[FileListSize-1].Name,file.cFileName);
        if (file.dwFileAttributes != INVALID_FILE_ATTRIBUTES &&
            (file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            _tcslen(file.cFileName) < MAX_PATH - 1)
        {
            _tcscat(FileList[FileListSize-1].Name,_T("\\"));
        }

    } while(FindNextFile(hFile,&file));

    FindClose(hFile);

    /* Check the size of the list to see if we found any matches */
    if (FileListSize == 0)
    {
        _tcscpy(strOut,szOriginal);
        if (FileList != NULL)
            cmd_free(FileList);
        return;

    }
    /* Sort the files */
    qsort(FileList,FileListSize,sizeof(FileName), compare);

    if (!SelectCompletion(FileList, FileListSize, szTyped,
                          bList, szWord, Listed) ||
        _tcslen(szPrefix) + _tcslen(szWord) + 4 > MAX_PATH)
    {
        _tcscpy(strOut,szOriginal);
        cmd_free(FileList);
        return;
    }

    /* nothing found that matched last time so return the first thing in the list */
    strOut[0] = _T('\0');

    /* Special character in the name */
    if (FileNameContainsSpecialCharacters(szWord))
    {
        INT LastSpace;
        BOOL bInside;
        /* It needs a " at the end */
        NeededQuote = TRUE;
        LastSpace = -1;
        bInside = FALSE;
        /* Find the place to put the " at the start */
        for(i = 0; i < _tcslen(szPrefix); i++)
        {
            if (szPrefix[i] == _T('\"'))
                bInside = !bInside;
            if (szPrefix[i] == _T(' ') && !bInside)
                LastSpace = i;
        }

        /* insert the quotation and move things around */
        if (szPrefix[LastSpace + 1] != _T('\"') && LastSpace != -1)
        {
            memmove ( &szPrefix[LastSpace+1], &szPrefix[LastSpace], (_tcslen(szPrefix)-LastSpace+1) * sizeof(TCHAR) );

            if ((UINT)(LastSpace + 1) == _tcslen(szPrefix))
            {
                _tcscat(szPrefix,_T("\""));
            }
            szPrefix[LastSpace + 1] = _T('\"');
        }
        else if (LastSpace == -1)
        {
            /* Add quotation only if none exists already */
            if (szPrefix[0] != _T('\"'))
            {
                _tcscpy(szBaseWord,_T("\""));
                _tcscat(szBaseWord,szPrefix);
                _tcscpy(szPrefix,szBaseWord);
            }
        }
    }

    _tcscpy(strOut,szPrefix);
    _tcscat(strOut,szWord);

    /* check for odd number of quotes means we need to close them */
    if (!NeededQuote)
    {
        for(i = 0; i < _tcslen(strOut); i++)
        {
            if (strOut[i] == _T('\"'))
                NeededQuote = !NeededQuote;
        }
    }

    if (FileListSize == 1 && szWord[_tcslen(szWord) - 1] != _T('\\'))
    {
        if (NeededQuote || (_tcslen(szPrefix) && szPrefix[_tcslen(szPrefix) - 1] == _T('\"')))
            _tcscat(strOut,_T("\""));
        _tcscat(strOut,_T(" "));
    }

    //EndLength = _tcslen(strOut);
    //DiffLength = EndLength - StartLength;
    if (FileList != NULL)
        cmd_free(FileList);
}
#endif
