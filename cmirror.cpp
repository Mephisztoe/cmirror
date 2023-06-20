/* ========================================================================

   cmirror.cpp v1.3c

   The authors of this software MAKE NO WARRANTY as to the RELIABILITY,
   SUITABILITY, or USABILITY of this software. USE IT AT YOUR OWN RISK.

   This is a simple source code control utility.  It was initially
   written by Casey Muratori but has since grown to include the fine
   work of Sean Barrett and Jeff Roberts.  It is in the public domain.
   Anyone can use it, modify it, roll'n'smoke hardcopies of the source
   code, sell it to the terrorists, etc.

   But the authors make absolutely no warranty as to the reliability,
   suitability, or usability of the software.  It works with files -
   probably files that are important to you.  There might be bad bugs
   in here.  It could delete them all.  It could format your hard drive.
   We have no idea.  If you lose all your files from using it, we will
   point and laugh.  Cmirror is not a substitute for making backups.

   If you're a snappy coder and you'd like to contribute bug fixes or
   feature additions, please see http://www.mollyrocket.com/tools
   
   ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <windows.h>
#include <limits.h>
#include <ctype.h>
#define STB_DEFINE
#include "stb.h"

// To try to be extra safe when people make changes, we have all the
// "low level" file delete/move/copy operations at the beginning of
// the file, wrapped in these "LowLevel" functions that do some sanity
// checking and maybe even prompt the user if it's potentially legit
// but rare. Then we scan the source and make sure all the unwrapped
// calls appear before the EOLLFO tag below. So if you write some new
// code and forget and call the unwrapped functions, hopefully it'll
// catch this and print a warning.

static bool
ShouldContinue(bool &ContinueAlways, char *Message, char *Alternatives=NULL, char *ResultChar=NULL)
{
    bool Result = ContinueAlways;

    if(!Result)
    {
        if (Message == NULL)
            Message = "Continue?  (y/n/a)";
        printf("%s\n", Message);

        bool ValidResponse = false;
        do
        {
            char Character;
            scanf("%c", &Character);
            switch(Character)
            {
                case 'y':
                {
                    Result = true;
                    ValidResponse = true;
                } break;

                case 'n':
                {
                    Result = false;
                    ValidResponse = true;
                } break;

                case 'a':
                {
                    Result = ContinueAlways = true;
                    ValidResponse = true;
                } break;

                default:
                {
                    if (Alternatives) {
                        char *s = stb_strichr(Alternatives, Character);
                        if (s) {
                            *ResultChar = *s;
                            Result = false;
                            ValidResponse = true;
                        }
                    }
                }
            }
        } while(!ValidResponse);
    }

    return(Result);
}

static BOOL DeleteFileLowLevel(char *File)
{
   BOOL Result = DeleteFile(File);

   // If deletion fails, try to move the offending file to the
   // Windows temp directory so it will be out of the repository
   // and hopefully cleaned up by Windows sometime in the future
   // when it is no longer in use.
   if(!Result)
   {
       char TempPath[MAX_PATH];
       char TempFile[MAX_PATH];
       GetTempPath(sizeof(TempPath), TempPath);
       if(GetTempFileName(TempPath, "cm", 0, TempFile))
       {
           // TODO: DeleteFile is required here because Windows actually
           // creates the file, and so the move would fail.
           DeleteFile(TempFile);
           printf("\nUnable to delete \"%s\", attempting rename to \"%s\".\n",
                  File, TempFile);
           Result = MoveFile(File, TempFile);
       }            
   }

   return Result;
}

static bool CopyFileAlways;
static bool DeleteFileAlways;

//#if !defined CopyFileEx
#define COPY_FILE_FAIL_IF_EXISTS              0x00000001
#define COPY_FILE_RESTARTABLE                 0x00000002
#define COPY_FILE_OPEN_SOURCE_FOR_WRITE       0x00000004
#define COPY_FILE_ALLOW_DECRYPTED_DESTINATION 0x00000008

typedef
DWORD
(WINAPI *LPPROGRESS_ROUTINE)(LARGE_INTEGER TotalFileSize,
                             LARGE_INTEGER TotalBytesTransferred,
                             LARGE_INTEGER StreamSize,
                             LARGE_INTEGER StreamBytesTransferred,
                             DWORD dwStreamNumber,
                             DWORD dwCallbackReason,
                             HANDLE hSourceFile,
                             HANDLE hDestinationFile,
                             LPVOID lpData
                             );
typedef BOOL WINAPI copy_file_ex_a(LPCSTR lpExistingFileName,
                                   LPCSTR lpNewFileName,
                                   LPPROGRESS_ROUTINE lpProgressRoutine,
                                   LPVOID lpData,
                                   LPBOOL pbCancel,
                                   DWORD dwCopyFlags);
//#endif

static BOOL
Win32CopyFile(char *ExistingName, char *NewName, bool FailIfExists)
{
    BOOL Result = FALSE;

    static copy_file_ex_a *CopyFileExPtr;
    static bool InitializationAttempted;
    if(!InitializationAttempted)
    {
        HINSTANCE Kernel32 = LoadLibrary("kernel32.dll");
        if(Kernel32)
        {
            CopyFileExPtr = (copy_file_ex_a *)GetProcAddress(Kernel32, "CopyFileExA");
        }
        
        InitializationAttempted = true;
    }
    
    if(CopyFileExPtr)
    {
        Result = CopyFileExPtr(ExistingName, NewName, 0, 0, 0, COPY_FILE_ALLOW_DECRYPTED_DESTINATION | (FailIfExists ? COPY_FILE_FAIL_IF_EXISTS : 0));
    }
    else
    {
        Result = CopyFile(ExistingName, NewName, FailIfExists ? TRUE : FALSE);
    }

    return(Result);
}

BOOL CopyFileWrapped(char *Workspace, char *ExistingName, char *NewName, bool FailIfExists)
{
    if (FailIfExists)
    {
        BOOL res = Win32CopyFile(ExistingName, NewName, FailIfExists);
        if (!res) {
            int err = GetLastError();
        }
        return res;
 
    } else {
 
        if (!stb_prefixi(NewName, Workspace))
        {
            printf(" Internal error! Attempted to copy '%s' over '%s' with overwrite allowed,\n"
                   "but the target filename was not in the workspace.\n", ExistingName, NewName);
            exit(1);
        }
        #if 0
        if (stb_fexists(NewName))
        {
            if (!CopyFileAlways)
                printf("About to copy '%s' over existing '%s'.\n", ExistingName, NewName);
            bool OkToProceed = ShouldContinue(CopyFileAlways, "Continue?  (y/n/a, choose 'a' to allow all copy-overwrites)");
            if (!OkToProceed)
                return FALSE;
        }
        #endif
        return Win32CopyFile(ExistingName, NewName, FailIfExists);
    }
}

BOOL DeleteFileWrapped(char *Workspace, char *Local, char *Central,
                       char *File)
{
    if (stb_prefixi(File, Local) || stb_prefixi(File, Central))
    {
        // PARANOIA:
        // make sure there's a cmirror version number in the filename
        if (!strstr(File, ",cm")) {
            printf("Internal error! Attempted to delete '%s', which is in the %s repository, but it\n"
                   "does not have a cmirror version number, so we have no business deleting it!\n",
                   File, stb_prefixi(File,Local) ? "local" : "central");
            exit(1);
        }
        // ok, go ahead and delete it
        return DeleteFileLowLevel(File);
    }
    else if (stb_prefixi(File, Workspace))
    {
        #if 0
        if (!DeleteFileAlways)
            printf("About to delete file '%s'.\n", File);
        bool OkToProceed = ShouldContinue(DeleteFileAlways, "Continue?  (y/n/a, choose 'a' to allow all workspace deletes)");
        if (!OkToProceed)
            return FALSE;
        #endif
        return DeleteFileLowLevel(File);
    }
    else
    {
        printf("Internal error! Attempted to delete '%s', which does not come from any\n"
               "directory that cmirror is supposed to be manipulating.\n", File);
        exit(1);
    }
}

// EOLLFO: end of low-level file operations (this marker is for source-checking tools; do not delete it)

enum file_type
{
    NullFileType,
    
    WorkspaceFileType,
    LocalFileType,
    CentralFileType,

    OnePastLastFileType
};

char *FileTypeText[] =
{
    "NullFileType",
    "WorkspaceFileType",
    "LocalFileType",
    "CentralFileType",
};

struct file_version
{
    int VersionNumber;
    bool Deleted;
    unsigned int dwHighDateTime;
    unsigned int dwLowDateTime;
    bool OldStyle;

    file_version stb_bst_fields_parent(fv);
};

stb_bst_parent(file_version,  // node name
               fv,            // same name as field above; also name prefix for functions that take nodes
               file_versions, // name of (optional) 'tree' data type that holds the root
               Version,       // function name prefix for functions that take the tree type
               VersionNumber, // name of field to compare
               int,           // type of that field
               a - b)         // comparison function for that field

struct file_record
{
    bool Present;
    bool Deleted;
    
    int MinVersion;
    int MaxVersion;
    file_versions Versions;
};

struct directory_entry
{
    char *Name;

    file_record File[OnePastLastFileType];

    directory_entry stb_bst_fields_parent(dire);
};

stb_bst_parent(directory_entry, dire, directory_contents, Dir, Name, char *, stricmp(a,b))

enum conflict_behavior
{
    HaltOnConflicts = 0,
    
    IgnoreConflicts,
};

enum ignore_behavior
{
    NoIgnoreEffect,
    DoIgnore,
    DoNotIgnore
};

struct file_rule
{
    // NOTE: When you add something to this structure, please make sure
    // to fill in a default value in the UpdateRuleFor function immediately
    // following.
    
    char *WildCard;
    int WildCardLength; // for reverse scanning

    conflict_behavior Behavior;

    // When Ignore is set to true, files matching the wildcard behave
    // literally as if they did not exist.  They will not be sync'd,
    // versioned, or copied by cmirror.
    ignore_behavior Ignore;

    // When CapVersionCount is set to true, files matching the wildcard
    // will have their last MaxVersionCount versions stored instead of
    // an unbounded number of versions.  If the file is deleted from
    // the repository, on the next sync, cmirror will delete all versions
    // stored that are later than MaxVersionCountIfDeleted.  
    bool CapVersionCount;
    int MaxVersionCount;
    int MaxVersionCountIfDeleted;

    // When PreCheckinCommand is non-zero, it is treated as an
    // executable name that will be run with the workspace file and
    // version information as arguments when cmirror detects that it
    // has changed from the repository version (this is designed to
    // faciliate keyword substitution and such)
    char *PreCheckinCommand;
    char *PreCheckinCommandParameters;
};

typedef file_rule* file_rule_list;

static file_rule *
UpdateRuleFor(file_rule_list *FileRuleList, char *Wildcard)
{
    // NOTE: These used to be kept sorted, but now they are left
    // as a linked list to facilitate processing them in the order
    // in which they appeared.
    // file_rule *Rule = Find(FileRuleList, Wildcard);
    file_rule *Rule = stb_arr_add(*FileRuleList);

    Rule->WildCard = Wildcard;
    Rule->WildCardLength = strlen(Wildcard);
    Rule->Behavior = HaltOnConflicts;
    Rule->Ignore = NoIgnoreEffect;
    Rule->CapVersionCount = false;
    Rule->MaxVersionCount = INT_MAX;
    Rule->MaxVersionCountIfDeleted = INT_MAX;
    Rule->PreCheckinCommand = 0;
    Rule->PreCheckinCommandParameters = 0;

    return(Rule);
}

enum mirror_mode
{
    FullSyncMode,
    LocalSyncMode,
    SidewaysSyncMode,
    CheckpointMode,
};

enum diff_method
{
    ByteByByteDiff,
    ModificationStampDiff,
    TimestampRepresentationTolerantStampDiff,
    TRTSWithByteFallbackDiff,
};

struct mirror_config
{
    file_rule_list FileRuleList;
    
    mirror_mode MirrorMode;
    diff_method DiffMethod[OnePastLastFileType][OnePastLastFileType];
    char *CopyCommand;
    char *DeleteCommand;
    
    bool WritableWorkspace;
    
    bool ContinueAlways;

    bool SyncWorkspace;
    bool SyncCentral;
    char *CentralCache;

    bool SuppressIgnores;
    bool SummarizeIgnores;
    int SyncPrintThreshold;
    int LineLength;
    bool PrintReason;
    bool SummarizeSync;
    bool SummarizeSyncIfNotPrinted;
    bool SummarizeDirs;
    bool SummarizeDirsIfNotPrinted;

    int RetryTimes[OnePastLastFileType];

    char *Directory[OnePastLastFileType];
};

struct string_table
{
    char *StoreCurrent;
    char *StoreBase;
};

//
// --- === ---
//

static void
InitializeStringTable(string_table &Table)
{
    Table.StoreCurrent = Table.StoreBase = (char *)malloc(1 << 24);
}

static char *
PushTableState(string_table &Table)
{
    return(Table.StoreCurrent);
}

static void
PopTableState(string_table &Table, char *Mark)
{
    Table.StoreCurrent = Mark;
}

static void
Cat(string_table &Table, char *String)
{
    while(*String) {*Table.StoreCurrent++ = *String++;}
}

static void
CatSlash(string_table &Table, char *String)
{
    while(*String)
    {
        if(*String == '\\')
        {
            *Table.StoreCurrent++ = '/';
        }
        else
        {
            *Table.StoreCurrent++ = *String++;
        }
    }
}

static void
Cat(string_table &Table, char Character)
{
    *Table.StoreCurrent++ = Character;
}

static char *
StoreDirectoryFile(string_table &Table, char *Directory, char *File)
{
    char *Result = Table.StoreCurrent;

    if(Directory && *Directory)
    {
        CatSlash(Table, Directory);
        Cat(Table, '/');
    }
    CatSlash(Table, File);
    Cat(Table, '\0');

    return(Result);
}

static void
Unixify(char *String)
{
    stb_fixpath(String);
}

static void
FreeStringTable(string_table &Table)
{
    free(Table.StoreBase);
    Table.StoreBase = 0;
    Table.StoreCurrent = 0;
}

static char *
FindLastChar(char *String, int ch)
{
    return strrchr(String,ch);
}

static char *
FindLastSlash(char *Path)
{
    char *p = stb_strrchr2((char *) Path, '/', '\\');
    return p ? p : Path;
}

static void
AddFileVersion(file_record &Record, int VersionIndex, unsigned int dwLowDateTime, unsigned int dwHighDateTime, bool Deleted, bool OldStyle)
{
    Record.Present = true;
    if(VersionIndex != -1)
    {
        if((Record.MinVersion == -1) || (Record.MinVersion > VersionIndex))
        {
            Record.MinVersion = VersionIndex;
        }

        if((Record.MaxVersion == -1) || (Record.MaxVersion < VersionIndex))
        {
            Record.Deleted = Deleted;
            Record.MaxVersion = VersionIndex;
        }
    }

    file_version *Version = (file_version *) malloc(sizeof(*Version));
    if(!Version)
    {
        fprintf(stderr, "Fatal error we're toast\n");
        return;
    }
    Version->VersionNumber = VersionIndex;
    Version->dwLowDateTime = dwLowDateTime;
    Version->dwHighDateTime = dwHighDateTime;
    Version->Deleted = Deleted;
    Version->OldStyle = OldStyle;
    VersionInsert(&Record.Versions, Version);
}

static void
RemoveFileVersion(file_record &Record, int VersionIndex)
{
    file_versions *VersionList = &Record.Versions;
    file_version *Version = VersionFind(VersionList, VersionIndex);
    if (!Version)
    {
        fprintf(stderr, "Tried to remove a version that wasn't present.\n");
        return;
    }
    VersionRemove(VersionList, Version);
    if (VersionFind(VersionList, VersionIndex))
        fprintf(stderr, "Internal error: attempt to remove version failed -- problem with stb_bst\n");
}

static bool
Win32FindFile(char *FileName, WIN32_FIND_DATA &Data)
{
    HANDLE Handle = FindFirstFile(FileName, &Data);
    if(Handle != INVALID_HANDLE_VALUE)
    {
        FindClose(Handle);
        return(true);
    }

    return(false);
}

static bool
WildCardMatch(char* name, char* wild)
{
    return stb_wildmatchi(wild, name) != 0;
}

static bool
Accept(file_rule_list &IgnoreList, char *Name)
{
    int NameLen = strlen(Name);
    bool result = true;

    {for(file_rule *Ignore = IgnoreList;
         !stb_arr_end(IgnoreList,Ignore);
         ++Ignore)
    {
        if (Ignore->Ignore == NoIgnoreEffect)
            continue;
        // could put this in WildCardMatch, but the recursion there screws us up
        if (Ignore->WildCard[0] == '*') {
            // check the ending
            char *WildEnd = Ignore->WildCard + Ignore->WildCardLength;
            char *NameEnd = Name + NameLen;
            // scan backwards until we exhaust name characters or
            // until we hit a '*' in the pattern
            for (int i=0; i < NameLen; ++i) {
                --NameEnd, --WildEnd;
                if (*WildEnd == '*') break;
                if (*WildEnd == '?') continue;
                if (tolower(*WildEnd) != tolower(*NameEnd))
                   goto no_effect; // 'continue' at next level of nesting
            }
        }
        switch(Ignore->Ignore) {
            case DoIgnore:
                if(WildCardMatch(Name, Ignore->WildCard)) {
                    result = false;
                }
                break;
            case DoNotIgnore:
                if(WildCardMatch(Name, Ignore->WildCard)) {
                    result = true;
                }
                break;
        }
       no_effect:
        ;
    }}
    
    return result;
}

static bool
GetFileVersionCap(mirror_config &Config, directory_entry *Entry, int &MaxVersionCount)
{
    bool Result = false;
    MaxVersionCount = 1 << 24;
    
    if(Entry)
    {
        // This always returns the LAST version cap it finds, because that
        // way the user can control which wildcard takes precendence by
        // intelligently ordering them in the file.
        {for(file_rule *Cap = Config.FileRuleList;
             !stb_arr_end(Config.FileRuleList,Cap);
             ++Cap)
        {
            if(WildCardMatch(Entry->Name, Cap->WildCard) &&
               Cap->CapVersionCount)
            {
                if(Entry->File[CentralFileType].Deleted)
                {
                    MaxVersionCount = Cap->MaxVersionCountIfDeleted;
                }
                else
                {
                    MaxVersionCount = Cap->MaxVersionCount;
                }
                Result = true;
            }
        }}
    }

    return(Result);
}

static conflict_behavior
GetConflictBehaviorFor(mirror_config &Config, directory_entry *Entry)
{
    conflict_behavior Result = HaltOnConflicts;
    
    if(Entry)
    {
        // This always returns the LAST conflict behavior it finds, because that
        // way the user can control which wildcard takes precendence by
        // intelligently ordering them in the file.
        {for(file_rule *Behavior = Config.FileRuleList;
             !stb_arr_end(Config.FileRuleList, Behavior);
             ++Behavior)
        {
            if(WildCardMatch(Entry->Name, Behavior->WildCard))
            {
                Result = Behavior->Behavior;
            }
        }}
    }

    return(Result);
}

static char SearchString[3 * MAX_PATH];

bool
ProcessFilename(string_table &Strings, directory_contents &Contents,
                file_rule_list &IgnoreList, 
                file_type FileType, char *Name, FILETIME ftLastWriteTime,
                bool SuppressIgnores, int &IgnoredFileCount)
{
    bool Result = false;
    bool OldStyle = false;
    
    bool Deleted = false;
    int Version = -1;
                
    if(FileType != WorkspaceFileType)
    {
        char *LastComma = FindLastChar(Name, ',');
        if(LastComma)
        {
            char *CommaLoc = LastComma;
            // check for new-style internal comma
            if (LastComma[1] == 'c' && LastComma[2] == 'm') {
               char *Dot = strchr(LastComma, '.');
               LastComma += 3;
               if (Dot) {
                  // double-check that it's the last dot
                  if (strchr(Dot+1, '.')) {
                     // ok, somehow we ended up with a file of the form:
                     //     "somefile,cm00001.foo.bar"
                     // even though we only insert ",cm000001" before the _last_ dot!
                     // so this code path is for future expansion, and for now is an error
                     // what do we do on error? casey just lets it through with version -1
                     // so do nothing
                  } else {
                     if ( LastComma[ 0 ] == 'd' ) {
                        Deleted = true;
                        ++LastComma;
                     }
                     *Dot = '\0';
                     Version = atoi(LastComma);
                     *Dot = '.';
                     // splice the extension over the ",cm0000" part
                     strcpy(CommaLoc, Dot);
                  }
               } else {
                  // no dot!
                  *CommaLoc = '\0';
                  if ( LastComma[ 0 ] == 'd') {
                     Deleted = true;
                     ++LastComma;
                  }
                  Version = atoi(LastComma);
               }
            } else {
               OldStyle = true;
               *LastComma = '\0';
               ++LastComma;
               if ( ( LastComma[ 0 ] == 'd' ) )
               {
                   Deleted = true;
                   ++LastComma;
               }
               Version = atoi(LastComma);
            }
        }
    }       

    assert(Name[0]);
    if(Accept(IgnoreList, Name))
    {
        directory_entry *Entry = DirFind(&Contents, Name);
        if(!Entry)
        {
            Entry = (directory_entry *) malloc(sizeof(*Entry));
            if(!Entry)
            {
                fprintf(stderr, "Fatal error we're toast\n");
                return(false);
            }

            Entry->Name = Name;
            DirInsert(&Contents, Entry);

            {for(int FileIndex = 0;
                 FileIndex < OnePastLastFileType;
                 ++FileIndex)
            {
                file_record &Record = Entry->File[FileIndex];
                Record.Present = false;
                Record.Deleted = false;
                Record.MinVersion = -1;
                Record.MaxVersion = -1;
                VersionInit(&Record.Versions);
            }}
        }
        assert(stricmp(Entry->Name, Name) == 0);

        file_record &Record = Entry->File[FileType];
        AddFileVersion(Record, Version,
                       ftLastWriteTime.dwLowDateTime,
                       ftLastWriteTime.dwHighDateTime, Deleted, OldStyle);

        Result = true;
    }
    else
    {
        ++IgnoredFileCount;
        if (!SuppressIgnores)
        {
            printf("\n  (ignored: %s)\n", Name);
        }
    }

    return(Result);
}

void
BuildDirectoryRecursive(string_table &Strings, directory_contents &Contents,
                        file_rule_list &IgnoreList,
                        file_type FileType, char *Base, char *Directory,
                        char *Description, int &EntryIndex,
                        bool SuppressIgnores, int &IgnoredFiles, int &IgnoredDirectories)
{
    if(Directory)
    {
        sprintf(SearchString, "%s/%s/*", Base, Directory);
    }
    else
    {
        sprintf(SearchString, "%s/*", Base);
    }

    WIN32_FIND_DATA FindData;
    HANDLE SearchHandle = FindFirstFile(SearchString, &FindData);
    if(SearchHandle != INVALID_HANDLE_VALUE)
    {
        do
        {
            if(stricmp(FindData.cFileName, ".") && stricmp(FindData.cFileName, ".."))
            {
                bool IsDirectory = ((FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);

                // TODO: This potentially stores the string multiple times
                char *Name = StoreDirectoryFile(Strings, Directory, FindData.cFileName);

                if(IsDirectory)
                {
                    if(Accept(IgnoreList, Name))
                    {
                        BuildDirectoryRecursive(Strings, Contents, IgnoreList,
                                                FileType, Base, Name,
                                                Description, EntryIndex,
                                                SuppressIgnores, IgnoredFiles, IgnoredDirectories);
                    } else {
                        ++IgnoredDirectories;
                        if (!SuppressIgnores)
                        {
                           printf("\n  (ignored: %s)\n", Name);
                        }
                    }
                }
                else
                {
                    if(ProcessFilename(Strings, Contents, IgnoreList, 
                                       FileType, Name, FindData.ftLastWriteTime,
                                       SuppressIgnores, IgnoredFiles))
                    {
                        ++EntryIndex;
                        if ( ( EntryIndex & 127 ) == 0 )
                        {
                            printf("\r  %s: %d    ", Description, EntryIndex);
                        }
                    }
                }
            }
        } while(FindNextFile(SearchHandle, &FindData));

        FindClose(SearchHandle);
    }
}

bool
AcceptDirectories(file_rule_list &IgnoreList, char *filename)
{
    char path[MAX_PATH * 3], *s;

    strcpy(path, filename);
    s = path + strlen(path) - 1;
    while (s >= path) {
        if (*s == '/' || *s == '\\') {
            *s = 0;
            if (!Accept(IgnoreList, path))
                return false;
        }
        --s;
    }
    return true;
}

int unsigned
ReadUnsigned(char *From)
{
    int unsigned Number = 0;
    while((*From >= '0') && (*From <= '9'))
    {
        Number *= 10;
        Number += (*From - '0');
        ++From;
    }

    return(Number);
}

bool
BuildDirectoryFromCache(string_table &Strings, directory_contents &Contents,
                        file_rule_list &IgnoreList,
                        file_type FileType, char *CacheFileName,
                        char *Description, int &EntryIndex,
                        bool &SuppressIgnores, int &IgnoredFiles, int &IgnoredDirectories)
{
    bool Result = false, Compressed = true;
    unsigned int FileSize;
    size_t FileSize_t;
    char *CacheBuffer;
    if (!CacheFileName[0]) {
        return Result;
    }

    // try a compressed file
    CacheBuffer = stb_decompress_fromfile(CacheFileName, &FileSize);
    if (!CacheBuffer) {
        // try uncompressed
        CacheBuffer = stb_filec(CacheFileName, &FileSize_t);
        Compressed = false;
    }
    if (CacheBuffer)
    {
        // drawback to stb_file is this doesn't get printed until AFTER we read it!
        printf("Using%s directory cache (%dkb)...\n", Compressed ? " compressed": "", FileSize / 1024);

        Result = true;
      
        char *Parse = CacheBuffer;
        while(*Parse)
        {
            FILETIME ftLastWriteTime;
              
            ftLastWriteTime.dwHighDateTime = ReadUnsigned(Parse);
            while(*Parse && (*Parse != ' ')) {++Parse;}
            while(*Parse == ' ') {++Parse;}

            ftLastWriteTime.dwLowDateTime = ReadUnsigned(Parse);
            while(*Parse && (*Parse != ' ')) {++Parse;}
            while(*Parse == ' ') {++Parse;}

            char *ParsedName = Parse;
            while(*Parse && (*Parse != '\n' && *Parse != '\r')) {++Parse;}
            if(*Parse)
            {
                *Parse = '\0';
                ++Parse;
                while((*Parse == '\n') ||
                      (*Parse == '\r')) {++Parse;}
            }

            char *Name = StoreDirectoryFile(Strings, 0, ParsedName);

            if(AcceptDirectories(IgnoreList, Name))
            {
                if(ProcessFilename(Strings, Contents, IgnoreList,
                                   FileType, Name, ftLastWriteTime,
                                   SuppressIgnores, IgnoredFiles))
                {
                    ++EntryIndex;
                    if ( ( EntryIndex & 127 ) == 0 )
                    {
                        printf("\r  %s: %d    ", Description, EntryIndex);
                    }
                }
            } else {
                ++IgnoredDirectories;
                if (!SuppressIgnores)
                {
                   printf("\n  (ignored: %s)\n", Name);
                }
            }
        }
        free(CacheBuffer);
    }

    return(Result);
}

static char BufferA[4096];
static char BufferB[4096];

static void
GetVersionFileNameRelative(char *Directory, directory_entry *Entry, file_type FileType,
                           int VersionIndex, bool Deleted, bool OldStyle, char *Buffer)
{
    if(Entry)
    {
        char *Name = Entry->Name;
        char *Separator = (Directory && *Directory) ? "/" : "";
    
        if(FileType == WorkspaceFileType)
        {
            sprintf(Buffer, "%s%s%s", Directory, Separator, Name);
        }
        else
        {
            if (OldStyle) {
               sprintf(Buffer, "%s%s%s,%s%08d",
                       Directory, Separator, Name,
                       Deleted ? "d" : "", VersionIndex
                       );
            } else {
               char *Slash = FindLastSlash(Name);
               char NameWithoutExt[4096], *Ext, *Format;
               strcpy(NameWithoutExt, Name);
               Ext = FindLastChar(Slash ? Slash : Name, '.');
               if (Ext == NULL) {
                  Ext = "";
               } else {
                  int ExtLoc = Ext - Name;
                  NameWithoutExt[ ExtLoc ] = 0;
               }
               // Shorten the length of the version numbers, because with
               // inline version numbers it's harder to see that something
               // is e.g. "foo.txt"... that is, "foo.txt,00000004" is easy to
               // read as "foo.txt", but "foo,cm00000004.txt" is not.
               if (VersionIndex < 1000)
                  Format = "%s%s%s,cm%s%04d%s";
               else
                  Format = "%s%s%s,cm%s%d%s";
               sprintf(Buffer, Format,
                       Directory, Separator, NameWithoutExt,
                       Deleted ? "d" : "", VersionIndex, Ext
                       );
            }
        }
    }
    else
    {
        *Buffer = '\0';
    }
}

static void
GetVersionFileName(mirror_config &Config,
                   directory_entry *Entry, file_type FileType,
                   int VersionIndex, bool Deleted, bool OldStyle, char *Buffer)
{
    GetVersionFileNameRelative(Config.Directory[FileType],
                               Entry, FileType, VersionIndex, Deleted, OldStyle, Buffer);
}

static bool
WriteDirectoryToCache(directory_contents &Contents, file_type FileType,
                      char *Description, char *CacheFileName)
{
    bool Result = false;
    int EntryIndex = 0;
    
    FILE *Cache = fopen(CacheFileName, "wb");
    if(Cache)
    {
        stb_compress_stream_start(Cache);

        {for(directory_entry *Entry = DirFirst(&Contents);
             Entry;
             Entry = DirNext(&Contents, Entry))
        {
            file_record &Record = Entry->File[FileType];
            if(Record.Present)
            {
                {for(file_version *Version = VersionFirst(&Record.Versions);
                     Version;
                     Version = VersionNext(&Record.Versions, Version))
                {
                    char timebuffer[100];

                    GetVersionFileNameRelative("", Entry, FileType, Version->VersionNumber, Version->Deleted, Version->OldStyle, BufferA);
                    sprintf(timebuffer, "%u %u ",
                            Version->dwHighDateTime,
                            Version->dwLowDateTime);
                    stb_write(timebuffer, strlen(timebuffer));
                    stb_write(BufferA, strlen(BufferA));
                    stb_write("\r\n", 2);

                    ++EntryIndex;
                    if ( ( EntryIndex & 127 ) == 0 )
                    {
                        printf("\r  %s: %d    ", Description, EntryIndex);
                    }
                }}
            }
        }}

        stb_compress_stream_end(TRUE);
        Result = true;
    }

    printf("\r  %s: %d    ", Description, EntryIndex);
    
    return(Result);
}

static void
PrintContents(directory_contents &Contents)
{
    {for(directory_entry *Entry = DirFirst(&Contents);
         Entry;
         Entry = DirNext(&Contents, Entry))
    {
        printf("[%c%c%c] %s\n",
               Entry->File[WorkspaceFileType].Present ? 'W' : ' ',
               Entry->File[CentralFileType].Present ? 'C' : ' ',
               Entry->File[LocalFileType].Present ? 'L' : ' ',
               Entry->Name);
    }}
}

void
BuildDirectory(mirror_config &Config, directory_contents &RootContents, string_table &Strings)
{
    int EntryIndex;
    int IgnoredFiles;
    int IgnoredDirectories;

    printf("Building directory structure:\n");

    if(Config.SyncCentral)
    {
        EntryIndex = 0;
        IgnoredFiles = 0;
        IgnoredDirectories = 0;
        if(!BuildDirectoryFromCache(Strings, RootContents, Config.FileRuleList,
                                    CentralFileType, Config.CentralCache,
                                    "Central (cached)", EntryIndex,
                                    Config.SuppressIgnores, IgnoredFiles, IgnoredDirectories))
        {
            BuildDirectoryRecursive(Strings, RootContents, Config.FileRuleList,
                                    CentralFileType,
                                    Config.Directory[CentralFileType], 0,
                                    "Central", EntryIndex,
                                    Config.SuppressIgnores, IgnoredFiles, IgnoredDirectories);
        }
        if(EntryIndex)
        {
            printf("\r  Central: %d    \n", EntryIndex);
        }
        if (Config.SummarizeIgnores && (IgnoredFiles || IgnoredDirectories))
        {
            printf("\r  (Central ignored %d dirs, %d files)    \n", IgnoredDirectories, IgnoredFiles);
        }
    }
    
    if(Config.SyncWorkspace)
    {
        EntryIndex = 0;
        IgnoredFiles = 0;
        IgnoredDirectories = 0;
        BuildDirectoryRecursive(Strings, RootContents, Config.FileRuleList,
                                WorkspaceFileType,
                                Config.Directory[WorkspaceFileType], 0,
                                "Workspace", EntryIndex,
                                Config.SuppressIgnores, IgnoredFiles, IgnoredDirectories);
        if(EntryIndex)
        {
            printf("\r  Workspace: %d    \n", EntryIndex);
        }
        if (Config.SummarizeIgnores) // always show workspace if requested
        {
            printf("\r  (Workspace ignored %d dirs, %d files)    \n", IgnoredDirectories, IgnoredFiles);
        }
    }

    EntryIndex = 0;
    IgnoredFiles = 0;
    IgnoredDirectories = 0;
    BuildDirectoryRecursive(Strings, RootContents, Config.FileRuleList,
                            LocalFileType,
                            Config.Directory[LocalFileType], 0,
                            "Local", EntryIndex,
                            Config.SuppressIgnores, IgnoredFiles, IgnoredDirectories);
    if(EntryIndex)
    {
        printf("\r  Local: %d    \n", EntryIndex);
    }
    if (Config.SummarizeIgnores && (IgnoredFiles || IgnoredDirectories))
    {
        printf("\r  (Local ignored %d dirs, %d files)    \n", IgnoredDirectories, IgnoredFiles);
    }

    printf("\n");
}

enum sync_operation_type
{
    VersionCopyOperation,
    VersionDeleteOperation,
    VersionMoveOperation,
    MarkAsDeletedOperation,
    PreCheckinOperation,

    OperationCount
};

char *OperationName[] =
{
   "copy operations",
   "delete operations",
   "move operations",
   "delete-mark operations",
   "pre-checkin",
};

enum sync_reason
{
   PreCheckinReason,
   WorkspaceNewReason,
   WorkspaceChangedReason,
   WorkspaceDeletedReason,
   LocalNonLatestReason,
   LocalOnlyNonLatestReason,
   LocalOnlyLatestReason,
   LocalCentralConflictIgnoreReason,
   VersionCapReason,
   CentralNewReason,
   CentralOnlyLatestReason,
   CentralLatestDeletedReason,

   ReasonCount
};

char *ReasonName[] =
{
   "pre-checkin rule",
   "new file in workspace",
   "changed file in workspace",
   "deleted file from workspace",
   "old version in local",
   "old version only in local",
   "newest version only in local",
   "conflict between local & central",
   "version cap",
   "new file in central",
   "newest version only in central",
   "newest version deleted in central",
};


struct sync_operation
{
    sync_operation_type OperationType;
    sync_reason Reason;
    
    directory_entry *EntryA;
    file_type EntryTypeA;

    directory_entry *EntryB;
    file_type EntryTypeB;

    int VersionIndex;

    // Set Overwrite to true if it's OK to clobber existing files
    bool Overwrite;

    // Used for external-command operations
    char *Command;
    char *Parameters;
};

struct sync_context
{
    mirror_config *Config;
    bool ContinueAlways;
    
    sync_operation* Operations;
};

static void
AddError(sync_context &Context, char *FormatString, ...)
{
    va_list Args;
    va_start(Args, FormatString);    
    vfprintf(stderr, FormatString, Args);
    fprintf(stderr, "\n");
    va_end(Args);
}

static void
AddNotice(sync_context &Context, char *FormatString, ...)
{
    va_list Args;
    va_start(Args, FormatString);    
    vfprintf(stderr, FormatString, Args);
    fprintf(stderr, "\n");
    va_end(Args);
}

static bool
InitializeSyncContext(sync_context &Context, mirror_config &Config)
{
    Context.Config = &Config;
    Context.ContinueAlways = Config.ContinueAlways;
    Context.Operations = NULL;
    return(Context.Operations != 0);
}

static sync_operation *
AddOperation(sync_context &Context,
             sync_operation_type OperationType, sync_reason Reason,
             directory_entry *EntryA, file_type TypeA,
             directory_entry *EntryB, file_type TypeB,
             int VersionIndex)
{
    sync_operation &Operation = *stb_arr_add(Context.Operations);
    Operation.OperationType = OperationType;
    Operation.Reason = Reason;
    Operation.EntryA = EntryA;
    Operation.EntryTypeA = TypeA;
    Operation.EntryB = EntryB;
    Operation.EntryTypeB = TypeB;
    Operation.VersionIndex = VersionIndex;
    Operation.Overwrite = false;

    return &Operation;
}

static file_version *
GetVersion(directory_entry *Entry, file_type Type, int VersionIndex)
{
    file_version *Result = 0;
    
    if(Entry)
    {
        file_versions *Versions = &Entry->File[Type].Versions;
        if(Type == WorkspaceFileType)
        {
            Result = VersionFirst(Versions);
        }
        else
        {
            Result = VersionFind(Versions, VersionIndex);
            assert(!Result || (Result->VersionNumber == VersionIndex));
        }
    }
    
    return(Result);
}

static void
EnsureDirectoryExistsForFile(char *FileName)
{
    {for(char *Scan = FileName;
         *Scan;
         ++Scan)
    {
        if((*Scan == '/') || (*Scan == '\\'))
        {
            char Swap = *Scan;
            *Scan = '\0';
            CreateDirectory(FileName, 0);
            *Scan = Swap;
        }
    }}
}

char ExecuteBuffer[1 << 16];
char CommandLineBuffer[1 << 16];

static bool
ExecuteSystemCommand(char *CommandPath, char *CommandLine,
                     char *WorkingDirectory, int &ExitCodeReturn)
{
    STARTUPINFO StartupInformation = {sizeof(StartupInformation)};
    StartupInformation.lpTitle = CommandPath;
    StartupInformation.dwFlags = 0;//STARTF_USESTDHANDLES;
    StartupInformation.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    StartupInformation.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    StartupInformation.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    char *Scan = CommandPath;
    char *OutBuffer = ExecuteBuffer;
    *OutBuffer++='"'; // quote the command
    while(*Scan)
    {
        if(*Scan == '/')
        {
            *OutBuffer++ = '\\';
            ++Scan;
        }
        else
        {
            *OutBuffer++ = *Scan++;
        }
    }
    *OutBuffer++='"'; // quote the command
    *OutBuffer++ = ' ';
    Scan = CommandLine;
    while(*Scan)
    {
        *OutBuffer++ = *Scan++;
    }
    *OutBuffer++ = '\0';


    PROCESS_INFORMATION ProcessInformation;
    if(CreateProcess(0, ExecuteBuffer,
                     0, 0,
                     FALSE, // Don't inherit handles
                     NORMAL_PRIORITY_CLASS,
                     0, // TODO: Manage the environment here?
                     WorkingDirectory,
                     &StartupInformation,
                     &ProcessInformation))
    {
        if(WaitForSingleObject(ProcessInformation.hProcess, INFINITE) == WAIT_OBJECT_0)
        {
            DWORD ExitCode = -1;
            GetExitCodeProcess(ProcessInformation.hProcess, &ExitCode);
            CloseHandle(ProcessInformation.hProcess);

            ExitCodeReturn = (int)ExitCode;
            return(true);
        }
    }

    return(false);
}

static bool
FileExists(char *FileName)
{
    DWORD InvalidFileAttributes = 0xFFFFFFFF;
    return(GetFileAttributes(FileName) != InvalidFileAttributes);
}

static bool
CMirrorDeleteFile(mirror_config &Config, file_type FileType, char *File)
{
    bool Result = false;
    
    if(FileExists(File))
    {
        if(Config.DeleteCommand)
        {
            sprintf(CommandLineBuffer, "%s \"%s\"",
                    FileTypeText[FileType], File);
        
            int ExitCode;
            Result = (ExecuteSystemCommand(Config.DeleteCommand,
                                           CommandLineBuffer, 0, ExitCode) &&
                      (ExitCode == 0));
        }
        else
        {
            DWORD Attributes = GetFileAttributes(File); 
            if (Attributes & FILE_ATTRIBUTE_READONLY) 
            { 
                SetFileAttributes(File, Attributes & (~FILE_ATTRIBUTE_READONLY)); 
            }

            Result = (DeleteFileWrapped(Config.Directory[WorkspaceFileType],
                                        Config.Directory[LocalFileType],
                                        Config.Directory[CentralFileType],
                                        File) == TRUE);
        }
    }

    return(Result);
}

static bool
CMirrorCopyFile(mirror_config &Config,
                file_type FromFileType,
                file_type ToFileType,
                char *From,
                char *To,
                bool Overwrite)
{
    if(Config.CopyCommand)
    {
        sprintf(CommandLineBuffer, "%s %s \"%s\" \"%s\" \"%s\"",
                FileTypeText[FromFileType],
                FileTypeText[ToFileType],
                From, To,
                Config.Directory[ToFileType]);

        int ExitCode;
        return(ExecuteSystemCommand(Config.CopyCommand,
                                    CommandLineBuffer, 0, ExitCode) &&
               (ExitCode == 0));
    }
    else
    {
        // Copy the new file in its place
        if(CopyFileWrapped(Config.Directory[WorkspaceFileType], From, To, !Overwrite))
        {
            return(true);
        }
        else
        {
            // if we failed, AND overwrite is enabled, try deleting it ourselves...
            // important to structure it this way so it can use Config.DeleteCommand...
            // alternative is to move the Config.DeleteCommand stuff down to the low level...
            // which maybe is a good idea? Note old code had a bug that it didn't check Overwrite.
            if (Overwrite && FileExists(To)) {
               // Delete the dest file if it exists
               CMirrorDeleteFile(Config, ToFileType, To);
               if (!FileExists(To)) {
                  CopyFileWrapped(Config.Directory[WorkspaceFileType], From, To, !Overwrite);
               }
            }
            return(false);
        }
    }
}

static bool
CMirrorPreCheckin(sync_context &Context, char *Command, char *Params,
                  char *Filename, char *RepositoryRelativeName, int VersionNumber)
{
    bool Result = false;

    sprintf(CommandLineBuffer, "%s \"%s\" \"%s\" %d",
            Params, Filename, RepositoryRelativeName, VersionNumber);
    
    int ExitCode;
    if((ExecuteSystemCommand(Command, CommandLineBuffer, 0, ExitCode) &&
        (ExitCode == 0)))
    {
        // Success
        Result = true;
    }

    return(Result);
}

static bool
Execute(sync_context &Context, sync_operation &Operation)
{
    bool Result = false;

    int VersionIndex = Operation.VersionIndex;

    mirror_config &Config = *Context.Config;
    
    directory_entry *EntryA = Operation.EntryA;
    file_type TypeA = Operation.EntryTypeA;
    file_version *VersionA = GetVersion(EntryA, TypeA, VersionIndex);
    
    directory_entry *EntryB = Operation.EntryB;
    file_type TypeB = Operation.EntryTypeB;
    file_version *VersionB = GetVersion(EntryB, TypeB, VersionIndex);
    
    switch(Operation.OperationType)
    {
        case VersionCopyOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);
            assert(VersionA);

            bool Deleted = VersionA->Deleted;
            bool OldStyle = VersionA->OldStyle;
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, Deleted, OldStyle, BufferA);
            GetVersionFileName(*Context.Config, EntryB, TypeB, VersionIndex, Deleted, false   , BufferB);

            EnsureDirectoryExistsForFile(BufferA);
            EnsureDirectoryExistsForFile(BufferB);

            if((TypeB != WorkspaceFileType) || Config.WritableWorkspace)
            {
                if(CMirrorCopyFile(Config, TypeA, TypeB, BufferA, BufferB, Operation.Overwrite))
                {
                    // NOTE: We re-check the file times here instead of just
                    // using the ones we assume it will be, because it's possible
                    // that someone has changed the file during the cmirror sync
                    // operation and we don't want to leave the cache in an
                    // invalid state if caching is active.
                    WIN32_FIND_DATA FindData;
                    if(Win32FindFile(BufferB, FindData))
                    {
                        AddFileVersion(EntryB->File[TypeB], VersionIndex,
                                       FindData.ftLastWriteTime.dwLowDateTime,
                                       FindData.ftLastWriteTime.dwHighDateTime,
                                       Deleted, false);
                    } else {
                        // do something besides Result = true ???
                         }
                    
                    Result = true;
                }
                else
                {
                    AddError(Context, "Copy failed from %s to %s", BufferA, BufferB);
                }
            }
            else
            {
                AddNotice(Context, "Suppressed write of %s (workspace is not writable)", BufferB);
            }
        } break;

        case VersionDeleteOperation:
        {
            assert(Operation.EntryA);
        
            if ( VersionA )
            {
                bool Deleted = VersionA->Deleted;
                bool OldStyle = VersionA->OldStyle;
                GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, Deleted, OldStyle, BufferA);

                if((TypeA != WorkspaceFileType) || Config.WritableWorkspace)
                {
                    if(CMirrorDeleteFile(Config, TypeA, BufferA))
                    {
                        if (TypeA == CentralFileType && *Config.CentralCache)
                        {
                            RemoveFileVersion(EntryA->File[TypeA], VersionIndex);
                        }
                        Result = true;
                    }
                    else
                    {
                        AddError(Context, "Deletion failed on file %s", BufferA);
                    }
                }
                else
                {
                    AddNotice(Context, "Suppressed deletion of %s (workspace is not writable)", BufferA);
                }
            }
            else
            {
                AddNotice(Context, "Version %i didn't exist for %s", VersionIndex, EntryA->Name );
            }
        } break;

        case VersionMoveOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);
            assert(VersionA);

            bool Deleted = VersionA->Deleted;
            bool OldStyle = VersionA->OldStyle;
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, Deleted, OldStyle, BufferA);
            GetVersionFileName(*Context.Config, EntryB, TypeB, VersionIndex, Deleted, false   , BufferB);

            EnsureDirectoryExistsForFile(BufferA);
            EnsureDirectoryExistsForFile(BufferB);

            if((TypeB != WorkspaceFileType) || Config.WritableWorkspace)
            {
                if(CMirrorCopyFile(Config, TypeA, TypeB, BufferA, BufferB, Operation.Overwrite))
                {
                    // NOTE: We re-check the file times here instead of just
                    // using the ones we assume it will be, because it's possible
                    // that someone has changed the file during the cmirror sync
                    // operation and we don't want to leave the cache in an
                    // invalid state if caching is active.
                    WIN32_FIND_DATA FindData;
                    if(Win32FindFile(BufferB, FindData))
                    {
                        AddFileVersion(EntryB->File[TypeB], VersionIndex,
                                       FindData.ftLastWriteTime.dwLowDateTime,
                                       FindData.ftLastWriteTime.dwHighDateTime,
                                       Deleted, false);
                    } else {
                        // do something besides Result = true ???
                         }
                    
                    if(CMirrorDeleteFile(Config, TypeA, BufferA))
                    {
                        if (TypeA == CentralFileType && *Config.CentralCache)
                        {
                            RemoveFileVersion(EntryA->File[TypeA], VersionIndex);
                        }
                        Result = true;
                    }
                    else
                    {
                        AddError(Context, "Deletion-for-move failed on file %s", BufferA);
                    }
                }
                else
                {
                    AddError(Context, "Copy-for-move failed from %s to %s", BufferA, BufferB);
                }
            }
            else
            {
                AddNotice(Context, "Suppressed write of %s (workspace is not writable)", BufferB);
            }
            break;
        }
        
        case MarkAsDeletedOperation:
        {
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, true, false, BufferA);
            HANDLE File = CreateFile(BufferA, 0, FILE_SHARE_WRITE, 0, CREATE_NEW, 0, 0);
            if(File != INVALID_HANDLE_VALUE)
            {
                WIN32_FIND_DATA FindData;

                CloseHandle(File);

                // now get the file and reinsert into the list
                if(Win32FindFile( BufferA, FindData))
                {
                    AddFileVersion(EntryA->File[TypeA], VersionIndex, FindData.ftLastWriteTime.dwLowDateTime, FindData.ftLastWriteTime.dwHighDateTime, true, false);
                }

                Result = true;
            }
            else
            {
                AddError(Context, "Creation of deletion marker failed on file %s", BufferA);
            }
        } break;

        case PreCheckinOperation:
        {
            assert(TypeA == WorkspaceFileType);
            assert(Operation.EntryA);
            assert(VersionA);

            GetVersionFileName(*Context.Config, EntryA, TypeA,
                               VersionIndex, false, false, BufferA);
            if(CMirrorPreCheckin(Context, Operation.Command, Operation.Parameters,
                                 BufferA, EntryA->Name, VersionIndex))
            {
                Result = true;
            }
            else
            {
                AddError(Context, "Pre-checkin operation [%s %s] failed on file \"%s\"",
                         Operation.Command, Operation.Parameters, BufferA);
            }
        } break;

        default:
        {
            AddError(Context, "INTERNAL ERROR: Unrecognized operation type");
        } break;
    }

    return(Result);
}

static bool
ExecuteSyncContext(sync_context &Context)
{
    bool Success = true;
    
    {for(int SyncIndex = 0;
         (SyncIndex < stb_arr_len(Context.Operations)) && Success;
         ++SyncIndex)
    {
        printf("\r  Executing: %d / %d    ", SyncIndex + 1, stb_arr_len(Context.Operations));
        sync_operation &Operation = Context.Operations[SyncIndex];
        if(!Execute(Context, Operation))
        {
            Success = ShouldContinue(Context.ContinueAlways, NULL);
        }
    }}
    printf("\n");

    return(Success);
}

static void
FreeSyncContext(sync_context &Context)
{
    Context.Operations = NULL;
}

// TODO: Pick this buffer size with a little less arbitrariness, shall we?
static unsigned char CompareBufferA[1 << 16];
static unsigned char CompareBufferB[1 << 16];
static bool
FilesAreEquivalentByteForByte(sync_context &Context, char *FileNameA, char *FileNameB)
{
    bool Result = false;
    
    FILE *FileA = fopen(FileNameA, "rb");
    FILE *FileB = fopen(FileNameB, "rb");
    if(FileA && FileB)
    {
        int ReadSizeA, ReadSizeB;
        Result = true;
        while(Result &&
              (ReadSizeA = fread(CompareBufferA, 1, sizeof(CompareBufferA), FileA)) &&
              (ReadSizeB = fread(CompareBufferB, 1, sizeof(CompareBufferB), FileB)))
        {
            Result = ((ReadSizeA == ReadSizeB) &&
                      (memcmp(CompareBufferA, CompareBufferB, ReadSizeA) == 0));
        }
    }
    else
    {
        AddError(Context, "Unable to byte-compare %s and %s", FileNameA, FileNameB);
    }

    // TODO: Does fclose() accept 0 nicely?
    if(FileA) {fclose(FileA);}
    if(FileB) {fclose(FileB);}

    return(Result);
}

/*static bool
Win32GetFileDate(char *FileName, FILETIME &Date)
{
    bool DateDetermined = false;

    WIN32_FIND_DATA Data;
    if(Win32FindFile(FileName, Data))
    {
        Date = Data.ftLastWriteTime;
        DateDetermined = true;
    }

    return(DateDetermined);
}

static bool
FilesHaveEquivalentModificationStamps(sync_context &Context, char *FileNameA, char *FileNameB)
{
    bool Result = false;

    FILETIME DateA, DateB;
    if(Win32GetFileDate(FileNameA, DateA) &&
       Win32GetFileDate(FileNameB, DateB))
    {
        Result = ((DateA.dwHighDateTime == DateB.dwHighDateTime) &&
                  (DateA.dwLowDateTime == DateB.dwLowDateTime));
    }
    else
    {
        AddError(Context, "Unable to date-compare %s and %s", BufferA, BufferB);
    }
    
    return(Result);
}*/

static bool
AreEquivalentMethod(sync_context &Context,
                    directory_entry &EntryA, file_type TypeA,
                    directory_entry &EntryB, file_type TypeB,
                    int VersionIndex, diff_method Method)
{
    bool Result = false;

    file_version *VersionA = GetVersion(&EntryA, TypeA, VersionIndex);
    file_version *VersionB = GetVersion(&EntryB, TypeB, VersionIndex);

    if(VersionA && VersionB &&
       (VersionA->Deleted == VersionB->Deleted))
    {
        switch(Method)
        {
            case ByteByByteDiff:
            {
                GetVersionFileName(*Context.Config, &EntryA, TypeA,
                                   VersionIndex, VersionA->Deleted, VersionA->OldStyle, BufferA);
                GetVersionFileName(*Context.Config, &EntryB, TypeB,
                                   VersionIndex, VersionB->Deleted, VersionA->OldStyle, BufferB);
                
                Result = FilesAreEquivalentByteForByte(Context, BufferA, BufferB);
            } break;

            case ModificationStampDiff:
            {
              Result = ((VersionA->dwHighDateTime == VersionB->dwHighDateTime) &&
                        (VersionA->dwLowDateTime == VersionB->dwLowDateTime));
                
              //Result = FilesHaveEquivalentModificationStamps(Context, BufferA, BufferB);
            } break;

            case TimestampRepresentationTolerantStampDiff:
            {
                __int64 time_A = ((__int64) VersionA->dwHighDateTime << 32) +
                    (VersionA->dwLowDateTime);
                __int64 time_B = ((__int64) VersionB->dwHighDateTime << 32) +
                    (VersionB->dwLowDateTime);
                __int64 dt = time_A - time_B;

                Result = ((VersionA->dwHighDateTime == VersionB->dwHighDateTime) &&
                          (VersionA->dwLowDateTime == VersionB->dwLowDateTime));

                // Times are in nanoseconds. Different filesystems have different
                // precision as to what they represent, so when converting between
                // file systems we can expect a certain amount of error.
                //
                // on a NAS using the Linux ext3 file system, the largest error
                // in I saw was:             5,600,000
                //
                //   one second        = 1,000,000,000
                //   one tenth         =   100,000,000
                //   one one-hundredth =    10,000,000
                //
                // The bottom 7 digits of the time from the NAS are consistently 0,
                // so basically it's just rounding the NTFS time to 100ths of a second.
                // However, because the error was a little MORE than 5,000,000, that
                // means it's not _exactly_ rounding, so we can't detect specifically
                // that.
                //
                // One solution would be to accept times that are within 6,000,000
                // nanoseconds. This would correctly address the behavior I see from
                // the ext3 NAS. However, it is probably more future-proof to
                // accept a wider range. We're not worried about clock drift, just
                // errors due to storing _our_ times on a foreign filesystem. Since
                // it's infeasible to run cmirror 10 times a second (much less have
                // change files at that rate simultaneously), we can safely set a tolerance
                // on the order of 1/10th of a second. If we hypothesize a file system
                // that has only 1/10th second storage precision, we need to deal
                // with rounding to that level, so we'll use 2/10ths of a second.

                // @TODO: allow putting this in cmirror.config ?
                #define TimestampTolerance 200000000

                Result = (-TimestampTolerance <= dt && dt <= TimestampTolerance);
            } break;
        }
    }

    return(Result);
}

static bool
AreEquivalent(sync_context &Context,
              directory_entry &EntryA, file_type TypeA,
              directory_entry &EntryB, file_type TypeB,
              int VersionIndex)
{
    bool Result = false;
    
    diff_method Method = Context.Config->DiffMethod[TypeA][TypeB];
    switch(Method)
    {
        case TRTSWithByteFallbackDiff:
        {
            Result = AreEquivalentMethod(Context, EntryA, TypeA, EntryB, TypeB, VersionIndex, TimestampRepresentationTolerantStampDiff);
            if(!Result)
            {
                Result = AreEquivalentMethod(Context, EntryA, TypeA, EntryB, TypeB, VersionIndex, ByteByByteDiff);
            }
        } break;
        
        default:
        {
            Result = AreEquivalentMethod(Context, EntryA, TypeA, EntryB, TypeB, VersionIndex, Method);
        } break;
    }

    return(Result);
}

static void
AddVersionCopyOperation(sync_context &Context, sync_reason Reason,
                        directory_entry &EntryA, file_type TypeA,
                        directory_entry &EntryB, file_type TypeB,
                        int VersionIndex)
{
    AddOperation(Context, VersionCopyOperation, Reason, &EntryA, TypeA, &EntryB, TypeB, VersionIndex);
}

static void
AddVersionOverwriteOperation(sync_context &Context, sync_reason Reason,
                             directory_entry &EntryA, file_type TypeA,
                             directory_entry &EntryB, file_type TypeB,
                             int VersionIndex)
{
    sync_operation *Operation =
        AddOperation(Context, VersionCopyOperation, Reason,
                     &EntryA, TypeA,
                     &EntryB, TypeB, VersionIndex);
    if(Operation)
    {
        Operation->Overwrite = true;
    }
}

static void
AddVersionMoveOperation(sync_context &Context, sync_reason Reason,
                        directory_entry &EntryA, file_type TypeA,
                        directory_entry &EntryB, file_type TypeB,
                        int VersionIndex)
{
    AddOperation(Context, VersionMoveOperation, Reason, &EntryA, TypeA, &EntryB, TypeB, VersionIndex);
}

static void
AddVersionDeleteOperation(sync_context &Context, sync_reason Reason,
                          directory_entry &EntryA, file_type TypeA,
                          int VersionIndex)
{
    AddOperation(Context, VersionDeleteOperation, Reason, &EntryA, TypeA, 0, NullFileType, VersionIndex);
}

static void
AddMarkAsDeletedOperation(sync_context &Context, sync_reason Reason,
                          directory_entry &Entry, file_type Type,
                          int VersionIndex)
{
    AddOperation(Context, MarkAsDeletedOperation, Reason, &Entry, Type, 0, NullFileType, VersionIndex);
}

static void
AddPreCheckinOperation(sync_context &Context,
                       directory_entry &EntryA, int VersionIndex)
{
    file_rule_list *FileRuleList = &Context.Config->FileRuleList;
    {for(file_rule *Rule = *FileRuleList;
         !stb_arr_end(*FileRuleList, Rule);
         ++Rule)
    {
        if(Rule->PreCheckinCommand &&
           WildCardMatch(EntryA.Name, Rule->WildCard))
        {
            sync_operation *Operation =
                AddOperation(Context, PreCheckinOperation, PreCheckinReason,
                             &EntryA, WorkspaceFileType,
                             0, NullFileType, VersionIndex);
            Operation->Command = Rule->PreCheckinCommand;
            Operation->Parameters = Rule->PreCheckinCommandParameters;
        }
    }}
}

static int
GetLatestVersion(file_record &Record)
{
    assert(Record.MaxVersion != -1);
    return(Record.MaxVersion);
}

static int
GetNextVersion(file_record &Record)
{    
    return(Record.MaxVersion + 1);
}

bool
SynchronizeLocalAndWorkspace(sync_context &Context, directory_contents &RootContents)
{
    bool Result = true;

    int EntryIndex = 0;
    {for(directory_entry *Entry = DirFirst(&RootContents);
         Entry && Result;
         Entry = DirNext(&RootContents, Entry))
    {
        ++EntryIndex;

        if ( ( EntryIndex & 127 ) == 0 )
        {
          printf("\r  Workspace <-> Local: %d    ", EntryIndex);
        }
        
        //
        // First, sync the workspace and local files
        //
        if(Entry->File[WorkspaceFileType].Present)
        {
            // See if we have any local versions of this file at all
            if(Entry->File[LocalFileType].Present)
            {
                if(AreEquivalent(Context,
                                 *Entry, WorkspaceFileType,
                                 *Entry, LocalFileType, 
                                 GetLatestVersion(Entry->File[LocalFileType])))
                {
                    // The file has not changed
                }
                else
                {
                    // The file has changed - sync it back to local as a new version
                    int VersionIndex = GetNextVersion(Entry->File[LocalFileType]);
                    AddPreCheckinOperation(Context, *Entry, VersionIndex);
                    AddVersionCopyOperation(Context, WorkspaceChangedReason,
                                            *Entry, WorkspaceFileType,
                                            *Entry, LocalFileType,
                                            VersionIndex);
                }
            }
            else
            {
                // This is a new file, copy it to the local directory
                int VersionIndex = 1;
                AddPreCheckinOperation(Context, *Entry, VersionIndex);
                AddVersionCopyOperation(Context, WorkspaceNewReason,
                                        *Entry, WorkspaceFileType,
                                        *Entry, LocalFileType,
                                        VersionIndex);
            }
        }
        else
        {
            // See if we ever had a version of this file
            if(Entry->File[LocalFileType].Present)
            {
                // We did, so it must have been deleted
                if(!Entry->File[LocalFileType].Deleted)
                {
                    AddMarkAsDeletedOperation(Context, WorkspaceDeletedReason,
                                              *Entry, LocalFileType,
                                              GetNextVersion(Entry->File[LocalFileType]));
                }
            }
            else
            {
                // The server alone has this file.  We don't do
                // anything with it here, then, we just let the
                // server sync pick it up.
            }
        }
    }}

    if ( EntryIndex )
    {
      printf("\r  Workspace <-> Local: %d    \n", EntryIndex);
    }

    return(Result);
}

bool
SynchronizeLocalAndCentral(sync_context &Context, directory_contents &RootContents)
{
    bool Result = true;

    // TODO: I should probably issue an overwrite operation ONLY if the workspace
    // is marked as having the file, to guard against the possibility of overwriting
    // a file I missed somehow...
    
    int EntryIndex = 0;
    {for(directory_entry *Entry = DirFirst(&RootContents);
         Entry && Result;
         Entry = DirNext(&RootContents, Entry))
    {
        ++EntryIndex;

        if ( ( EntryIndex & 127 ) == 0 )
        {
          printf("\r  Local <-> Central: %d    ", EntryIndex);
        }
        
        //
        // Second, sync the local and server files
        //
        if(Entry->File[CentralFileType].Present)
        {
            if(Entry->File[LocalFileType].Present)
            {
                int MaxVersion = Entry->File[CentralFileType].MaxVersion;
                if(MaxVersion < Entry->File[LocalFileType].MaxVersion)
                {
                    MaxVersion = Entry->File[LocalFileType].MaxVersion;
                }

                {for(int VersionIndex = Entry->File[LocalFileType].MinVersion;
                     VersionIndex < MaxVersion;
                     ++VersionIndex)
                {
                    file_version *LocalVersion = GetVersion(Entry, LocalFileType, VersionIndex);
                    file_version *CentralVersion = GetVersion(Entry, CentralFileType, VersionIndex);
                    
                    if(LocalVersion)
                    {
                        if(!CentralVersion)
                        {
                            AddVersionMoveOperation(Context, LocalOnlyNonLatestReason,
                                                    *Entry, LocalFileType,
                                                    *Entry, CentralFileType,
                                                    VersionIndex);
                        }
                        else
                        {
                            if(AreEquivalent(Context,
                                             *Entry, CentralFileType, 
                                             *Entry, LocalFileType,
                                             VersionIndex))
                            {
                                AddVersionDeleteOperation(Context, LocalNonLatestReason, *Entry,
                                                          LocalFileType,
                                                          VersionIndex);
                            }
                            else if (GetConflictBehaviorFor(*Context.Config, Entry) ==
                                IgnoreConflicts)
                            {
                                AddVersionDeleteOperation(Context, LocalCentralConflictIgnoreReason, *Entry,
                                                          LocalFileType,
                                                          VersionIndex);
                            }
                            else
                            {
                                AddError(Context,
                                         "Local \"%s\" differs with central version with same index (%d) - "
                                         "please repair manually.", Entry->Name, VersionIndex);

                                Result = false;
                            }
                        }
                    }
                    else
                    {
                        // Nothing to do
                    }
                }}

                // Handle the max version specially
                {
                    file_version *LocalVersion = GetVersion(Entry, LocalFileType, MaxVersion);
                    file_version *CentralVersion = GetVersion(Entry, CentralFileType, MaxVersion);
                    
                    if(LocalVersion)
                    {
                        if(!CentralVersion)
                        {
                            AddVersionCopyOperation(Context, LocalOnlyLatestReason,
                                                    *Entry, LocalFileType,
                                                    *Entry, CentralFileType,
                                                    MaxVersion);
                        }
                        else if(AreEquivalent(Context,
                                              *Entry, CentralFileType, 
                                              *Entry, LocalFileType,
                                              MaxVersion) ||
                                (GetConflictBehaviorFor(*Context.Config, Entry) ==
                                 IgnoreConflicts))
                        {
                            // Nothing to do
                        }
                        else
                        {
                            AddError(Context,
                                     "Local \"%s\" differs with central version with same index (%d) - "
                                     "please repair manually.", Entry->Name, MaxVersion);

                            Result = false;
                        }
                    }
                    else
                    {
                        if(CentralVersion)
                        {
                            AddVersionCopyOperation(Context, CentralOnlyLatestReason,
                                                    *Entry, CentralFileType,
                                                    *Entry, LocalFileType,
                                                    MaxVersion);

                            if(Entry->File[WorkspaceFileType].Present)
                            {
                                if(Entry->File[CentralFileType].Deleted)
                                {
                                    AddVersionDeleteOperation(Context, CentralLatestDeletedReason, *Entry,
                                                              WorkspaceFileType,
                                                              MaxVersion);
                                }
                                else
                                {
                                    AddVersionOverwriteOperation(Context, CentralOnlyLatestReason,
                                                                 *Entry, LocalFileType,
                                                                 *Entry, WorkspaceFileType,
                                                                 MaxVersion);
                                }
                            }
                            else
                            {
                                AddVersionCopyOperation(Context, CentralOnlyLatestReason,
                                                        *Entry, LocalFileType,
                                                        *Entry, WorkspaceFileType,
                                                        MaxVersion);
                            }
                        }
                        else
                        {
                            AddError(Context,
                                     "INTERNAL ERROR: It should not be possible to have "
                                     "neither a local nor a server version at this point");
 
                            Result = false;
                        }
                    }
                }
            }
            else
            {
                // This file exists only on the server - copy down the latest version.
                AddVersionCopyOperation(Context, CentralNewReason,
                                        *Entry, CentralFileType,
                                        *Entry, LocalFileType,
                                        GetLatestVersion(Entry->File[CentralFileType]));
                if(!Entry->File[CentralFileType].Deleted)
                {
                    AddVersionOverwriteOperation(Context, CentralNewReason,
                                                 *Entry, LocalFileType,
                                                 *Entry, WorkspaceFileType,
                                                 GetLatestVersion(Entry->File[CentralFileType]));
                }
            }
        }
        else
        {
            if(Entry->File[LocalFileType].Present)
            {
                // The local machine has this file, but the server does not, so we
                // copy the local machines' versions to the server and delete them
                {for(int VersionIndex = Entry->File[LocalFileType].MinVersion;
                     VersionIndex <= Entry->File[LocalFileType].MaxVersion;
                     ++VersionIndex)
                {
                    if(VersionIndex == Entry->File[LocalFileType].MaxVersion)
                    {
                        AddVersionCopyOperation(Context, LocalOnlyLatestReason,
                                                *Entry, LocalFileType,
                                                *Entry, CentralFileType,
                                                VersionIndex);
                    }
                    else
                    {
                        AddVersionMoveOperation(Context, LocalOnlyNonLatestReason,
                                                *Entry, LocalFileType,
                                                *Entry, CentralFileType,
                                                VersionIndex);
                    }
                }}
            }
            else
            {
                // Neither the server nor the local machine have this file.
                // How is that possible???
                AddError(Context,
                         "INTERNAL ERROR: It should not be possible to only "
                         "have a workspace file at this point");
                Result = ShouldContinue( Context.ContinueAlways, NULL );
            }
        }

        // If this file has a version cap imposed, clean up all versions
        // that exceed the cap

        // Subtlety: this cleanup check is done here, which is before any deletions
        // from this particular sync have been completed.  That's so that it won't
        // immediately use the deletion file cap on the sync in which the file is
        // first deleted, in case you suddenly realize you've made a mistake deleting
        // the file.  So, it will wait until the NEXT time you cmirror to attempt the
        // delete, and hopefully you'll realize it then when it prints out the deletion
        // action.
        int VersionCap;
        if(GetFileVersionCap(*Context.Config, Entry, VersionCap))
        {
            {for(int VersionIndex = Entry->File[CentralFileType].MinVersion;
                 VersionIndex <= (Entry->File[CentralFileType].MaxVersion - VersionCap);
                 ++VersionIndex)
            {
                // Subtlety: we must check for existence of each version here because
                // a sync copies filse up from local before executing this loop.  Hence,
                // a machine that's been out of sync for a while might copy up an old
                // version of a file from its local which has already been deleted from
                // central (due to the version cap) by a more recent sync on another machine.
                // This will leave a gap between the min versions and the max versions,
                // which would cause us to issue an invalid deletion request for some
                // versions in the middle, which don't actually exist anywhere.
                file_version *CentralVersion = GetVersion(Entry, CentralFileType, VersionIndex);
                if(CentralVersion)
                {
                    AddVersionDeleteOperation(Context, VersionCapReason,
                                              *Entry, CentralFileType,
                                              VersionIndex);
                }
            }}
        }
    }}
    if ( EntryIndex )
    {
      printf("\r  Local <-> Central: %d    \n", EntryIndex);
    }

    return(Result);
}

// squish a qualified filename for printing by eliding some of the path
bool did_squish;

void
SquishFilename(char *filename, int length)
{
    int n,k;
    --length; // printing in last column might force a newline
    if ((int) strlen(filename) <= length) return;
    char *s = stb_strrchr2(filename, '/', '\\');
    char *t = stb_strchr2(filename, '/', '\\');
    if (s == NULL || t == NULL || s==t) return;
    n = strlen(s);
    // we need to insert '...'
    length -= 5;
    if (n + (t-filename) > length) return;
    // now we want to choose some k such that:
    //    t += k 
    //    s -= k
    //    (strlen(s) + (t-filename) == length), +-1
    // this would mean:
    //    ((n+k) + (t+k-filename) == length)
    //    n+2k+t-filename = length
    k = (length - n - (t-filename))/2;
    if (k <= 0) return;
    s -= k;
    t += k;
    if ((int) strlen(s) + (t-filename) < length) --s;
    assert((int) strlen(s) + (t-filename) == length);
    if (s < t+5) return;
    strcpy(t, " ... ");
    memmove(t+5, s, strlen(s)+1);
    did_squish = true;
}

void
Print(sync_context &Context, sync_operation &Operation, bool Squish)
{
    char *Reason="", *Pad="";
    int ReasonLen = 0;

    int VersionIndex = Operation.VersionIndex;

    directory_entry *EntryA = Operation.EntryA;
    file_type TypeA = Operation.EntryTypeA;
    file_version *VersionA = GetVersion(EntryA, TypeA, VersionIndex);
    
    directory_entry *EntryB = Operation.EntryB;
    file_type TypeB = Operation.EntryTypeB;
    file_version *VersionB = GetVersion(EntryB, TypeB, VersionIndex);

    if (Context.Config->PrintReason) {
        ReasonLen = 4;
        Pad = "    ";
        switch(Operation.Reason) {
            case PreCheckinReason:                  Reason = "    ";  break;
            case WorkspaceChangedReason:            Reason = "wc  "; break;
            case WorkspaceNewReason:                Reason = "WA  "; break;
            case WorkspaceDeletedReason:            Reason = "WD  "; break;
            case LocalNonLatestReason:              Reason = "lo  "; break;
            case LocalOnlyNonLatestReason:          Reason = "ln  "; break;
            case LocalCentralConflictIgnoreReason:  Reason = "C!  "; break;
            case LocalOnlyLatestReason:             Reason = "ln  "; break;
            case CentralOnlyLatestReason:           Reason = "CC  "; break;
            case CentralLatestDeletedReason:        Reason = "CD  "; break;
            case CentralNewReason:                  Reason = "CA  "; break;
            case VersionCapReason:                  Reason = "vc  "; break;
            default: AddError(Context, "INTERNAL ERROR: Unrecognized reason");
        }
    }
    
    switch(Operation.OperationType)
    {
        case VersionCopyOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);

            bool Deleted = false;
            bool OldStyle = false; // if doesn't exist yet, it will be new style
            if(VersionA)
            {
                // TODO: This has to happen because sometimes you're copying from
                // a version that won't be there unless a previous command has
                // executed - what to do in this case?  Perhaps that's bad because
                // it means you can't continue after failure?  Etc., etc.
                Deleted = VersionA->Deleted;
                OldStyle = VersionA->OldStyle;
            }
            
            GetVersionFileName(*Context.Config, EntryA, TypeA,
                               VersionIndex, Deleted, OldStyle, BufferA);
            GetVersionFileName(*Context.Config, EntryB, TypeB,
                               VersionIndex, Deleted, false, BufferB);

            if (Squish) {
                if (Context.Config->LineLength) SquishFilename(BufferA, Context.Config->LineLength - 6 - ReasonLen);
                if (Context.Config->LineLength) SquishFilename(BufferB, Context.Config->LineLength - 6 - ReasonLen);
            }
            printf("%scopy: %s\n", Reason, BufferA);
            printf("%s   -> %s\n", Pad, BufferB);
        } break;

        case VersionDeleteOperation:
        {
            assert(Operation.EntryA);
            if ( VersionA == 0 )
            {
              printf("missing version: %s (%i)\n", EntryA->Name, VersionIndex );
            }
            else
            {
              GetVersionFileName(*Context.Config, EntryA, TypeA,
                                 VersionIndex, VersionA->Deleted, VersionA->OldStyle, BufferA);

              if (Squish && Context.Config->LineLength) SquishFilename(BufferA, Context.Config->LineLength - 8 - ReasonLen);
              printf("%sdelete: %s\n", Reason, BufferA);
            }
        } break;
        
        case VersionMoveOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);

            bool Deleted = false;
            bool OldStyle = false; // if doesn't exist yet, it will be new style
            if(VersionA)
            {
                // TODO: This has to happen because sometimes you're copying from
                // a version that won't be there unless a previous command has
                // executed - what to do in this case?  Perhaps that's bad because
                // it means you can't continue after failure?  Etc., etc.
                Deleted = VersionA->Deleted;
                OldStyle = VersionA->OldStyle;
            }
            
            GetVersionFileName(*Context.Config, EntryA, TypeA,
                               VersionIndex, Deleted, OldStyle, BufferA);
            GetVersionFileName(*Context.Config, EntryB, TypeB,
                               VersionIndex, Deleted, false, BufferB);

            if (Squish) {
                if (Context.Config->LineLength) SquishFilename(BufferA, Context.Config->LineLength - 6 - ReasonLen);
                if (Context.Config->LineLength) SquishFilename(BufferB, Context.Config->LineLength - 6 - ReasonLen);
            }
            printf("%smove: %s\n", Reason, BufferA);
            printf("%s   -> %s\n", Pad, BufferB);
        } break;

        case MarkAsDeletedOperation:
        {
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, true, false, BufferA);
            if (Squish && Context.Config->LineLength) SquishFilename(BufferA, Context.Config->LineLength - 8 - ReasonLen);
            printf("%sd-mark: %s\n", Reason, BufferA);
        } break;

        case PreCheckinOperation:
        {
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, true, false, BufferA);
            // @TODO: I don't use pre-checkin so I'm not sure how best to approach SquishFilenaming it
            printf("%spre-checkin: %s %s %s\n", Reason, Operation.Command, Operation.Parameters, BufferA);
        } break;

        default:
        {
            AddError(Context, "INTERNAL ERROR: Unrecognized operation type");
        } break;
    }
}

char *
FirstFileName(sync_context &Context, sync_operation &Operation)
{
    int VersionIndex = Operation.VersionIndex;

    directory_entry *EntryA = Operation.EntryA;
    file_type TypeA = Operation.EntryTypeA;
    file_version *VersionA = GetVersion(EntryA, TypeA, VersionIndex);
    
    directory_entry *EntryB = Operation.EntryB;
    file_type TypeB = Operation.EntryTypeB;
    file_version *VersionB = GetVersion(EntryB, TypeB, VersionIndex);

    switch(Operation.OperationType)
    {
        case VersionCopyOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);

            bool Deleted = false;
            bool OldStyle = false; // if doesn't exist yet, it will be new style
            if(VersionA)
            {
                // TODO: This has to happen because sometimes you're copying from
                // a version that won't be there unless a previous command has
                // executed - what to do in this case?  Perhaps that's bad because
                // it means you can't continue after failure?  Etc., etc.
                Deleted = VersionA->Deleted;
                OldStyle = VersionA->OldStyle;
            }
            
            GetVersionFileName(*Context.Config, EntryA, TypeA,
                               VersionIndex, Deleted, OldStyle, BufferA);
            return BufferA;
        } break;

        case VersionDeleteOperation:
        {
            assert(Operation.EntryA);
            if ( VersionA == 0 )
            {
              printf("missing version: %s (%i)\n", EntryA->Name, VersionIndex );
            }
            else
            {
              GetVersionFileName(*Context.Config, EntryA, TypeA,
                                 VersionIndex, VersionA->Deleted, VersionA->OldStyle, BufferA);

              return BufferA;
            }
        } break;
        
        case VersionMoveOperation:
        {
            assert(Operation.EntryA);
            assert(Operation.EntryB);

            bool Deleted = false;
            bool OldStyle = false; // if doesn't exist yet, it will be new style
            if(VersionA)
            {
                // TODO: This has to happen because sometimes you're copying from
                // a version that won't be there unless a previous command has
                // executed - what to do in this case?  Perhaps that's bad because
                // it means you can't continue after failure?  Etc., etc.
                Deleted = VersionA->Deleted;
                OldStyle = VersionA->OldStyle;
            }
            
            GetVersionFileName(*Context.Config, EntryA, TypeA,
                               VersionIndex, Deleted, OldStyle, BufferA);
            return BufferA;
        } break;

        case MarkAsDeletedOperation:
        {
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, true, false, BufferA);
            return BufferA;
        } break;

        case PreCheckinOperation:
        {
            GetVersionFileName(*Context.Config, EntryA, TypeA, VersionIndex, true, false, BufferA);
            return BufferA;
        } break;

        default:
        {
            AddError(Context, "INTERNAL ERROR: Unrecognized operation type");
        } break;
    }
    return NULL;
}

void
SummarizeOperation(int Operation, int *OpsPerReason, char *Pad) // Operation is really sync_operation_type
{
    int NumDifferentReasons=0, FirstReason = -1, TotalOps=0;
    {for (int i=0; i < ReasonCount; ++i)
    {
        TotalOps += OpsPerReason[i];
        if (OpsPerReason[i])
        {
            ++NumDifferentReasons;
            if (FirstReason == -1) FirstReason = i;
        }
    }}

    if (NumDifferentReasons == 0)
        return;

    if (NumDifferentReasons == 1)
    {
        printf("%s%5d %s due to %s\n", Pad, OpsPerReason[FirstReason],
                                     OperationName[Operation],
                                     ReasonName[FirstReason]);
        return;
    }

    printf("%s%5d %s\n", Pad,  TotalOps,
                        OperationName[Operation]);
    {for (int i=0; i < ReasonCount; ++i)
    {
        if (OpsPerReason[i])
        {
            printf("%s    %5d due to %s\n", Pad, OpsPerReason[i], ReasonName[i]);
        }
    }}
}

static void
PrintMatrixOpCount(int *ReasonArray)
{
    int Total = 0;
    
    {for(int ReasonIndex = 0;
         ReasonIndex < ReasonCount;
         ++ReasonIndex)
    {
        Total += ReasonArray[ReasonIndex];
    }}

    if(Total)
    {
        printf("%3d ", Total);
    }
    else
    {
        printf("  - ");
    }
}

#define MatrixStyle 1

void
SummarizeRecursively(stb_dirtree2 *d, stb_ptrmap *map, sync_context &Context)
{
    // if there are non-zero files, print a summary record
    if (stb_arr_len(d->files)) {
        int OpCounts[OperationCount][ReasonCount];
        memset(OpCounts, 0, sizeof(OpCounts));
        {for (int i=0; i < stb_arr_len(d->files); ++i) {
            sync_operation *s = (sync_operation *) stb_ptrmap_get(map, d->files[i]);
            OpCounts[s->OperationType][s->Reason] += 1;
        }}

        if(MatrixStyle)
        {
            printf("  ");
            PrintMatrixOpCount(OpCounts[VersionCopyOperation]);
            PrintMatrixOpCount(OpCounts[VersionDeleteOperation]);
            PrintMatrixOpCount(OpCounts[VersionMoveOperation]);
            PrintMatrixOpCount(OpCounts[MarkAsDeletedOperation]);
            PrintMatrixOpCount(OpCounts[PreCheckinOperation]);
            printf("  %s\n", d->fullpath);
        }
        else
        {
            printf("  %s\n", d->fullpath);
            {for (int i=0; i < OperationCount; ++i) {
                SummarizeOperation(i, OpCounts[i], "   ");
            }}
        }
    }

    {for (int i=0; i < stb_arr_len(d->subdirs); ++i) {
        SummarizeRecursively(d->subdirs[i], map, Context);
    }}
}


void
DoSummarizeDirs(sync_context &Context)
{
    // there are easier ways to do this -- we don't need to build a
    // tree structure, just find operations whose names have common
    // paths... but since the tree structure DOES gather them together
    // automatically, might as well use it

    // map between filenames and operations
    stb_ptrmap *map = stb_ptrmap_new();

    // gather up a list of all source filenames
    char** files=NULL;
    for (int i=0; i < stb_arr_len(Context.Operations); ++i)
    {
        char *name = FirstFileName(Context, Context.Operations[i]);
        if (name) {
        name = strdup(name);
        stb_arr_push(files, name);
        // create a mapping from the filename to the operation
        stb_ptrmap_add(map, name, &Context.Operations[i]);
        }
    }

    // build a tree out of the list
    stb_dirtree2 *d = stb_dirtree2_from_files(files, stb_arr_len(files));

    printf("Directory Summary\n");
    if(MatrixStyle)
    {
        printf("   cp  rm  mv  +d  pc   Path\n");
    }
    SummarizeRecursively(d, map, Context);

    stb_ptrmap_destroy(map);
    stb_pointer_array_free((void **) (char **) files, stb_arr_len(files));
    stb_arr_free(files);
}


bool
PromptSyncContext(sync_context &Context)
{
    if(Context.Operations)
    {
        int Count = stb_arr_len(Context.Operations);
        bool Suppress = (Count >= Context.Config->SyncPrintThreshold);
        bool allow_squish = true;
       retry:
        did_squish = false;
        if (!Suppress)
        {
            {for(int SyncIndex = 0;
                 SyncIndex < Count;
                 ++SyncIndex)
            {
                sync_operation &Operation = Context.Operations[SyncIndex];
                Print(Context, Operation, allow_squish);
            }}
        }
        else
        {
            printf("Synchronization requires %d operations (over the printable limit of %d)\n",
                   Count, Context.Config->SyncPrintThreshold);
        }

        if (Context.Config->SummarizeDirs || (Suppress && Context.Config->SummarizeDirsIfNotPrinted))
        {
            DoSummarizeDirs(Context);
        }
        if (Context.Config->SummarizeSync || (Suppress && Context.Config->SummarizeSyncIfNotPrinted))
        {
            printf("Summary:\n");
            int OpCounts[OperationCount][ReasonCount];
            memset(OpCounts, 0, sizeof(OpCounts));
            {for(int SyncIndex = 0;
                 SyncIndex < Count;
                 ++SyncIndex)
            {
                sync_operation &Operation = Context.Operations[SyncIndex];
                OpCounts[Operation.OperationType][Operation.Reason] += 1;
            }}
            {for(int i=0; i < OperationCount; ++i)
            {
                SummarizeOperation(i, OpCounts[i], "");
            }}
        }
        bool Choice;
        if (Suppress || did_squish)
        {
            char Selection=0;
            Choice = ShouldContinue(Context.ContinueAlways,
                        "Continue?  (y/n/a/v to view operations)", "v", &Selection);
            if (!Choice && Selection == 'v')
            {
                Suppress = false;
                allow_squish = false;
                goto retry; // could turn this into a loop, but it's the abnormal case so I hate that
            }
        }
        else
        {
            Choice = ShouldContinue(Context.ContinueAlways, NULL);
        }
        return Choice;
    }
    else
    {
        return(true);
    }
}

static bool
DirectoryIsAccessible(char *DirectoryName, char * Description, int RetryTimes)
{
  bool result;
  int times = RetryTimes;

  while ( 1 )
  {
    #define INVALID_FILE_ATTRIBUTES (-1)
    
    result = (GetFileAttributes(DirectoryName) != INVALID_FILE_ATTRIBUTES);
    
    --times;

    if ( ( result == true ) || ( times <= 0 ) )
    {
      return( result );
    }

    printf("** %s is unavailable: %s. Retrying... **\n", Description, DirectoryName);

    Sleep( 60000 ); // wait one minute
  }

  // will never get here
}

bool
Synchronize(mirror_config &Config, directory_contents &RootContents)
{
    bool Result = true;

    bool LocalIsAvailable =
        DirectoryIsAccessible(Config.Directory[LocalFileType], 
                              "Local", Config.RetryTimes[LocalFileType]);
    if(LocalIsAvailable)
    {
        printf("Synchronizing:\n");
    
        if ( Config.SyncWorkspace )
        {
          bool WorkspaceIsAvailable =
            DirectoryIsAccessible(Config.Directory[WorkspaceFileType],
                                  "Workspace", Config.RetryTimes[WorkspaceFileType]);

          if(WorkspaceIsAvailable)
          {
              sync_context Context;
  
              InitializeSyncContext(Context, Config);    
              Result = Result && SynchronizeLocalAndWorkspace(Context, RootContents);
              Result = Result && PromptSyncContext(Context);
              Result = Result && ExecuteSyncContext(Context);
              FreeSyncContext(Context);
          }
          else
          {
            printf("** Workspace is unavailable **\n");
            return( false );
          }
        }
    
        bool ServerIsAvailable =
            DirectoryIsAccessible(Config.Directory[CentralFileType],
                                  "Central", Config.RetryTimes[CentralFileType]);
        if(Result)
        {
            if(ServerIsAvailable)
            {
                sync_context Context;

                InitializeSyncContext(Context, Config);
                Result = Result && SynchronizeLocalAndCentral(Context, RootContents);
                Result = Result && PromptSyncContext(Context);
                Result = Result && ExecuteSyncContext(Context);
                FreeSyncContext(Context);
            }
            else
            {
                printf("** Server is unavailable **\n");
            }
        }
        
        printf("\n");
    }
    else
    {
        printf("** Local is unavailable **\n");
    }
    
    return(Result);
}

struct file_rule_expander
{
    file_rule_list *RuleList;
    char **z;
    file_rule *Rule;
};

static bool
Continue(file_rule_expander &Expander)
{
    return((*Expander.z) != 0);
}

static void
Next(file_rule_expander &Expander)
{
    ++Expander.z;
    Expander.Rule = Continue(Expander) ? UpdateRuleFor(Expander.RuleList, *Expander.z) : 0;
}

static file_rule_expander
ExpandRule(file_rule_list *RuleList, char *WildCard)
{
    file_rule_expander Expander;
    Expander.RuleList = RuleList;
    Expander.z = stb_tokens(WildCard, ";", 0);
    Expander.Rule = Continue(Expander) ? UpdateRuleFor(Expander.RuleList, *Expander.z) : 0;

    // NOTE: we never free the z array, as the rule stores what we passed in, rather than copying

    return(Expander);
}

void
MakeIgnoreRule(file_rule_list *RuleList, char *Wildcard, ignore_behavior behavior)
{
    {for(file_rule_expander RuleExpand = ExpandRule(RuleList, Wildcard);
         Continue(RuleExpand);
         Next(RuleExpand))
    {
        file_rule *Rule = RuleExpand.Rule;
        Rule->Ignore = behavior;
    }}
}

char *
FindConfigFile(string_table &Strings, char *CurrentDirectory)
{
    char *BaseName = "cmirror.config";
    // NOTE: This is braindead.
    // NOTE: we don't bother pushing & popping strings anymore because we need to keep the filename around!
        
    while(1)
    {
        // cmirror.config is a file?
        char *ConfigFileName = StoreDirectoryFile(Strings, CurrentDirectory, BaseName);
        FILE *ConfigFile = fopen(ConfigFileName, "rb");
        if(ConfigFile)
        {
            fclose(ConfigFile);
            return ConfigFileName;
        }

        // cmirror.config is a directory?
        WIN32_FIND_DATA FindData;
        if(Win32FindFile(ConfigFileName, FindData))
        {
            if (FindData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                ConfigFileName = StoreDirectoryFile(Strings, ConfigFileName, BaseName);
                ConfigFile = fopen(ConfigFileName, "rb");
                if (ConfigFile)
                {
                    fclose(ConfigFile);
                    return(ConfigFileName);
                }
                fprintf(stderr, "Found cmirror.config directory in %s, but no cmirror.config file within.\n", CurrentDirectory);
                exit(0);
            }
            // or there was a file according to FindFile, but not fopen!
            return 0;
        }

        char *NewCurrentDirectory = FindLastSlash(CurrentDirectory);
        if(NewCurrentDirectory != CurrentDirectory)
        {
            *NewCurrentDirectory = '\0';
        }
        else
        {
            return(0);
        }
    }
}

struct stream
{
    int LineIndex;
    char *At;
};

enum token_type
{
    UnrecongizedTokenType,
    IdentifierToken,
    EqualsToken,
    StringToken,
    IntegerToken,
    SemiColonToken,
    EndOfStreamToken,
};

struct token
{
    token_type Type;
    char *Content;
    int Integer;
};

static bool
IsWhitespace(char Character)
{
    return((Character == ' ') ||
           (Character == '\t'));
}

static bool
IsEOL(char Character)
{
    return((Character == '\n') ||
           (Character == '\r'));
}

static bool
IsAlpha(char Character)
{
    return(((Character >= 'a') && (Character <= 'z')) ||
           ((Character >= 'A') && (Character <= 'Z')));
}

static bool
IsNumeric(char Character)
{
    return((Character >= '0') && (Character <= '9'));
}

static void
SkipToNextLine(stream &Stream)
{
    while(*Stream.At && !IsEOL(*Stream.At))
    {
        ++Stream.At;
    }

    if(((Stream.At[0] == '\n') && (Stream.At[1] == '\r')) ||
       ((Stream.At[0] == '\r') && (Stream.At[1] == '\n')))
    {
        Stream.At += 2;
    }    
    else if(IsEOL(*Stream.At))
    {
        ++Stream.At;
    }

    ++Stream.LineIndex;
}

static void
StripQuotesInPlace(char *String)
{
    char *Source = String;
    char *Dest = String;

    bool Escaped = false;
    while(*Source)
    {
        if(!Escaped &&
           ((*Source == '"') ||
            (*Source == '\'')))
        {
            ++Source;
        }
        else if(!Escaped &&
                (*Source == '\\'))
        {
            ++Source;
            Escaped = true;
        }
        else
        {
            *Dest++ = *Source++;
            Escaped = false;
        }
    }

    *Dest = '\0';
}

static char *
ParseString(stream &Stream, string_table &Strings)
{
    char start_quote;

    start_quote = *Stream.At;

    assert((start_quote == '"') || (start_quote == '\''));
    ++Stream.At;
    
    char *Result = Strings.StoreCurrent;    
    while(*Stream.At && (*Stream.At != start_quote))
    {
        *Strings.StoreCurrent++ = *Stream.At++;
    }
    *Strings.StoreCurrent++ = '\0';

    if(*Stream.At == start_quote)
    {
        ++Stream.At;
    }
    else
    {
        printf("Unterminated string constant\n");
    }

    return(Result);
}

static char *
ParseIdentifier(stream &Stream, string_table &Strings)
{
    assert(IsAlpha(*Stream.At));
    
    char *Result = Strings.StoreCurrent;    
    while(IsAlpha(*Stream.At) || IsNumeric(*Stream.At))
    {
        *Strings.StoreCurrent++ = *Stream.At++;
    }
    *Strings.StoreCurrent++ = '\0';

    return(Result);
}

static token
GetToken(stream &Stream, string_table &Strings)
{
    while(*Stream.At)
    {
        if((Stream.At[0] == '/') && (Stream.At[1] == '/'))
        {
            Stream.At += 2;
            SkipToNextLine(Stream);
        }
        else if(IsWhitespace(*Stream.At))
        {
            ++Stream.At;
        }
        else if(IsEOL(*Stream.At))
        {
            SkipToNextLine(Stream);
        }
        else
        {
            break;
        }
    }

    token Token;
    Token.Type = UnrecongizedTokenType;
    Token.Content = Stream.At;
    switch(*Stream.At)
    {
        case '=':
        {
            Token.Type = EqualsToken;
            ++Stream.At;
        } break;

        case ';':
        {
            Token.Type = SemiColonToken;
            ++Stream.At;
        } break;
        
        case '\0':
        {
            Token.Type = EndOfStreamToken;
        } break;

        case '\'':
        case '"':
        {
            Token.Type = StringToken;
            Token.Content = ParseString(Stream, Strings);
        } break;

        default:
        {
            if(IsAlpha(*Stream.At))
            {
                Token.Type = IdentifierToken;
                Token.Content = ParseIdentifier(Stream, Strings);
            }
            else if(IsNumeric(*Stream.At))
            {
                Token.Type = IntegerToken;
                Token.Integer = atoi(Stream.At);
                while(IsNumeric(*Stream.At)) {++Stream.At;}
            }
                
        } break;
    }

    return(Token);
}

bool
TrueOrFalse(char *Value)
{
    if((stricmp(Value, "true") == 0) ||
       (stricmp(Value, "yes") == 0) ||
       (stricmp(Value, "1") == 0))
    {
        return(true);
    }

    if((stricmp(Value, "false") == 0) ||
       (stricmp(Value, "no") == 0) ||
       (stricmp(Value, "0") == 0))
    {
        return(false);
    }

    printf("Unrecognized true-or-false value \"%s\"\n", Value);
    return(false);
}

diff_method
GetDiffTypeFromString(char *Value)
{
    if(stricmp(Value, "ByteByByte") == 0)
    {
        return(ByteByByteDiff);
    }
    
    if(stricmp(Value, "ModificationStamp") == 0)
    {
        return(ModificationStampDiff);
    }

    if(stricmp(Value, "TimestampRepresentationTolerantStamp") == 0)
    {
        return(TimestampRepresentationTolerantStampDiff);
    }

    if(stricmp(Value, "TRTSWithByteFallback") == 0)
    {
        return(TRTSWithByteFallbackDiff);
    }

    fprintf(stderr, "Unrecognized diff type %s\n", Value);
    return(TimestampRepresentationTolerantStampDiff);
}

char *
GetDiffString(diff_method DiffMethod)
{
    switch(DiffMethod)
    {
        case ByteByByteDiff: {return("ByteByByte");} break;
        case ModificationStampDiff: {return("ModificationStamp");} break;
        case TimestampRepresentationTolerantStampDiff: {return("TimestampRepresentationTolerantStamp");} break;
        case TRTSWithByteFallbackDiff: {return("TRTSWithByteFallback");} break;
    }

    return("Unknown");
}

void
ParseConfigFile(stream &Stream, string_table &Strings, mirror_config &Config)
{
    bool Parsing = true;
    while(Parsing)
    {
        token Token = GetToken(Stream, Strings);
        switch(Token.Type)
        {
            case IdentifierToken:
            {
                token Equals = GetToken(Stream, Strings);
                if(Equals.Type == EqualsToken)
                {
                    token Value = GetToken(Stream, Strings);
                    if((Value.Type == StringToken) ||
                       (Value.Type == IntegerToken) ||
                       (Value.Type == IdentifierToken))
                    {
                        Unixify(Value.Content);
                        if(stricmp(Token.Content, "Central") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            Config.Directory[CentralFileType] = Value.Content;
                        }
                        else if(stricmp(Token.Content, "CentralCache") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            Config.CentralCache = Value.Content;
                        }
                        else if(stricmp(Token.Content, "Local") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            Config.Directory[LocalFileType] = Value.Content;
                        }
                        else if(stricmp(Token.Content, "Workspace") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            Config.Directory[WorkspaceFileType] = Value.Content;
                        }
                        else if(stricmp(Token.Content, "LocalRetryTimes") == 0)
                        {
                            if ( Value.Type == IntegerToken )
                            {
                              Config.RetryTimes[LocalFileType] = Value.Integer;
                            }
                        }
                        else if(stricmp(Token.Content, "WorkspaceRetryTimes") == 0)
                        {
                            if ( Value.Type == IntegerToken )
                            {
                              Config.RetryTimes[WorkspaceFileType] = Value.Integer;
                            }
                        }
                        else if(stricmp(Token.Content, "CentralRetryTimes") == 0)
                        {
                            if ( Value.Type == IntegerToken )
                            {
                                Config.RetryTimes[CentralFileType] = Value.Integer;
                            }
                        }
                        else if(stricmp(Token.Content, "Ignore") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            MakeIgnoreRule(&Config.FileRuleList, Value.Content, DoIgnore);
                        }
                        else if(stricmp(Token.Content, "Allow") == 0)
                        {
                            StripQuotesInPlace(Value.Content);
                            MakeIgnoreRule(&Config.FileRuleList, Value.Content, DoNotIgnore);
                        }
                        else if(stricmp(Token.Content, "IgnoreConflicts") == 0)
                        {
                            StripQuotesInPlace(Value.Content);

                            {for(file_rule_expander RuleExpand = ExpandRule(&Config.FileRuleList, Value.Content);
                                 Continue(RuleExpand);
                                 Next(RuleExpand))
                            {
                                file_rule *Rule = RuleExpand.Rule;

                                Rule->Behavior = IgnoreConflicts;
                            }}
                        }
                        else if(stricmp(Token.Content, "HaltOnConflicts") == 0)
                        {
                            StripQuotesInPlace(Value.Content);

                            {for(file_rule_expander RuleExpand = ExpandRule(&Config.FileRuleList, Value.Content);
                                 Continue(RuleExpand);
                                 Next(RuleExpand))
                            {
                                file_rule *Rule = RuleExpand.Rule;

                                Rule->Behavior = HaltOnConflicts;
                            }}
                        }
                        else if(stricmp(Token.Content, "VersionCap") == 0)
                        {
                            token VersionCountToken = GetToken(Stream, Strings);
                            if(VersionCountToken.Type == IntegerToken)
                            {
                                token DeletedVersionCountToken = GetToken(Stream, Strings);
                                if(DeletedVersionCountToken.Type == IntegerToken)
                                {
                                    StripQuotesInPlace(Value.Content);

                                    {for(file_rule_expander RuleExpand = ExpandRule(&Config.FileRuleList, Value.Content);
                                         Continue(RuleExpand);
                                         Next(RuleExpand))
                                    {
                                        file_rule *Rule = RuleExpand.Rule;

                                        Rule->CapVersionCount = true;
                                        Rule->MaxVersionCount = VersionCountToken.Integer;
                                        Rule->MaxVersionCountIfDeleted = DeletedVersionCountToken.Integer;
                                    }}
                                }                                
                                else
                                {
                                    printf("Missing second integer in version cap definition\n");
                                }
                            }
                            else
                            {
                                printf("Missing first integer in version cap definition\n");
                            }                                
                        }
                        else if(stricmp(Token.Content, "PreCheckin") == 0)
                        {
                            token CommandToken = GetToken(Stream, Strings);
                            if((CommandToken.Type == IdentifierToken) ||
                               (CommandToken.Type == StringToken))
                            {
                                token ParamsToken = GetToken(Stream, Strings);
                                if(ParamsToken.Type == StringToken)
                                {
                                    StripQuotesInPlace(Value.Content);
                                    StripQuotesInPlace(CommandToken.Content);
                                    StripQuotesInPlace(ParamsToken.Content);

                                    {for(file_rule_expander RuleExpand = ExpandRule(&Config.FileRuleList, Value.Content);
                                         Continue(RuleExpand);
                                         Next(RuleExpand))
                                    {
                                        file_rule *Rule = RuleExpand.Rule;
                                        Rule->PreCheckinCommand = CommandToken.Content;
                                        Rule->PreCheckinCommandParameters = ParamsToken.Content;
                                    }}
                                }
                                else
                                {
                                    printf("Missing parameter string in pre-checkin definition\n");
                                }
                            }
                            else
                            {
                                printf("Missing command string in pre-checkin definition\n");
                            }                                
                        }
                        else if(stricmp(Token.Content, "WritableWorkspace") == 0)
                        {
                            Config.WritableWorkspace = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SyncWorkspace") == 0)
                        {
                            Config.SyncWorkspace = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SyncCentral") == 0)
                        {
                            Config.SyncCentral = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "ContinueAlways") == 0)
                        {
                            Config.ContinueAlways = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SuppressIgnores") == 0)
                        {
                            Config.SuppressIgnores = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SummarizeIgnores") == 0)
                        {
                            Config.SummarizeIgnores = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "PrintReason") == 0)
                        {
                            Config.PrintReason = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "LineLength") == 0)
                        {
                            if(Value.Type == IntegerToken)
                            {
                                Config.LineLength = Value.Integer;
                            }
                            else
                            {
                                printf("Missing first integer in LineLength definition\n");
                            }                                
                        }
                        else if(stricmp(Token.Content, "SummarizeSync") == 0)
                        {
                            Config.SummarizeSync = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SummarizeSyncIfNotPrinted") == 0)
                        {
                            Config.SummarizeSyncIfNotPrinted = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SummarizeDirs") == 0)
                        {
                            Config.SummarizeDirs = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SummarizeDirsIfNotPrinted") == 0)
                        {
                            Config.SummarizeDirsIfNotPrinted = TrueOrFalse(Value.Content);
                        }
                        else if(stricmp(Token.Content, "SyncPrintThreshold") == 0)
                        {
                            if(Value.Type == IntegerToken)
                            {
                                Config.SyncPrintThreshold = Value.Integer;
                            }
                            else
                            {
                                printf("Missing first integer in SyncPrintThreshold definition\n");
                            }                                
                        }
                        else if(stricmp(Token.Content, "CopyCommand") == 0)
                        {
                            StripQuotesInPlace( Value.Content );
                            Config.CopyCommand = Value.Content;
                        }
                        else if(stricmp(Token.Content, "DeleteCommand") == 0)
                        {
                            StripQuotesInPlace( Value.Content );
                            Config.DeleteCommand = Value.Content;
                        }
                        else if(stricmp(Token.Content, "WorkspaceToLocalDiff") == 0)
                        {
                            Config.DiffMethod[WorkspaceFileType][LocalFileType] =
                                Config.DiffMethod[LocalFileType][WorkspaceFileType] =
                                GetDiffTypeFromString(Value.Content);
                        }
                        else if(stricmp(Token.Content, "LocalToCentralDiff") == 0)
                        {
                            Config.DiffMethod[LocalFileType][CentralFileType] =
                                Config.DiffMethod[CentralFileType][LocalFileType] =
                                GetDiffTypeFromString(Value.Content);
                        }
                        else
                        {
                            printf("Unrecognized configuration variable \"%s\"\n",
                                   Token.Content);
                        }

                        token SemiColon = GetToken(Stream, Strings);
                        if(SemiColon.Type != SemiColonToken)
                        {
                            printf("Expected semi-colon after assignment on line %d\n",
                                   Stream.LineIndex);
                            SkipToNextLine(Stream);
                        }
                    }
                    else
                    {
                        printf("Expected string after %s assignment on line %d\n",
                               Token.Content, Stream.LineIndex);
                        SkipToNextLine(Stream);
                    }
                }
                else
                {
                    printf("Expected = after %s on line %d\n",
                           Token.Content, Stream.LineIndex);
                    SkipToNextLine(Stream);
                }
            } break;

            case EndOfStreamToken:
            {
                Parsing = false;
            } break;

            default:
            {
                printf("Unexpected token type begins line %d ('%c')\n",
                       Stream.LineIndex, *Stream.At);
                SkipToNextLine(Stream);
            } break;
        }
    }
}

static void
ProcessCommandLine(mirror_config &Config, string_table &Strings,
                   int ArgCount, char **Args,
                   bool AllowConfigs, char *&ConfigFileName)
{
    bool ConfigParam = false;
    {for(int ArgIndex = 1;
         ArgIndex < ArgCount;
         ++ArgIndex)
    {
        char *Arg = Args[ArgIndex];

        if(ConfigParam)
        {
            if(AllowConfigs)
            {
                stream Stream;
                Stream.At = Arg;
                Stream.LineIndex = 1;
                ParseConfigFile(Stream, Strings, Config);
            }

            ConfigParam = false;
        }
        else
        {
            if(strcmp(Arg, "-config") == 0)
            {
                ConfigParam = true;
            }
            else
            {
                ConfigFileName = Arg;
            }
        }
    }}
}

void
InitializeCacheIgnore(mirror_config &Config)
{
    if (Config.CentralCache) {
        char *s = Config.CentralCache;
        char *t = Config.Directory[CentralFileType];
        int   n = strlen(t);
        if (strnicmp(s,t,n) == 0) {
            s += n;
            // if the directory doesn't end in '/', pretend it did
            if (t[n-1] != '/') {
                if (s[0] != '/') return;
                ++s;
            }
            MakeIgnoreRule(&Config.FileRuleList, s, DoIgnore);
        }
    }
}

static bool check(char *text, char *id)
{
    // check for "unwrapped" calls to 'id'... note that this code is not
    // smart and looks inside comments and strings.
    int n = strlen(id);
    char *t;
    t = strstr(text, id);
    for (; t; t = strstr(t+1,id) )
    {
        // store the length of the identifier we're looking for in 'len'
        int len = n;
        // allow an identifier that's two longer if there's an 'Ex' at the end
        if (stb_prefix(t+len, "Ex")) len += 2;
        // allow an identifier that's prefixed with a "_" at the beginning
        if (t[-1] == '_') { --t; ++len; }
        // if there are identifier characters BEFORE the id, it's ok
        if (__iscsym(t[ -1])) continue;
        // if there are identifier characters AFTER the id, it's ok
        if (__iscsym(t[len])) continue;
        // neither before nor after, so now check the tail
        t += len;
        while (*t && isspace(*t)) ++t;
        if (*t == '(')
            return true; // whoops!
    }
    return false; // none found
}

void CheckCmirrorSource(void)
{
    bool ok = true;

    // if cmirror.cpp is present, check it for any egregious violations
    char *text = stb_filec("cmirror.cpp", NULL);
    if (!text) return;
    char *s = strstr(text, "// EOLLFO");
    // windows versions
    if (check(s, "MoveFile"  )) ok=false, printf("Warning: cmirror.cpp contains unwrapped MoveFile call\n");
    if (check(s, "DeleteFile")) ok=false, printf("Warning: cmirror.cpp contains unwrapped DeleteFile call\n");
    if (check(s, "CopyFile"  )) ok=false, printf("Warning: cmirror.cpp contains unwrapped CopyFile call\n");
    // portable versions
    if (check(s,  "remove"  )) ok=false, printf("Warning: cmirror.cpp contains unwrapped remove call\n");
    if (check(s,  "rename"  )) ok=false, printf("Warning: cmirror.cpp contains unwrapped rename call\n");
    // no portable copy?!?

    if (ok)
        printf("Checked cmirror.cpp successfully.\n");
    printf("\n");
}

int
main(int ArgCount, char **Args)
{
    printf("CMirror v1.3c by Casey Muratori, Sean Barrett, and Jeff Roberts\n");
    printf("NO WARRANTY IS EXPRESSED OR IMPLIED. USE AT YOUR OWN RISK.\n");
    printf("This program and its source code are in public domain. For the latest\n");
    printf("version or the source code, see http://www.mollyrocket.com/tools\n");
    printf("\n");
    
    assert(sizeof(ReasonName)/sizeof(ReasonName[0]) == ReasonCount);
    assert(sizeof(OperationName)/sizeof(OperationName[0]) == OperationCount);

    string_table Strings;
    InitializeStringTable(Strings);

    CheckCmirrorSource();

    char CurrentDirectory[MAX_PATH];
    GetCurrentDirectory(sizeof(CurrentDirectory), CurrentDirectory);
    char *LastSlash = FindLastSlash(CurrentDirectory);
    if(!LastSlash[1])
    {
        *LastSlash = 0;
    }
    Unixify(CurrentDirectory);

    mirror_config Config;
    Config.FileRuleList = NULL;
    Config.MirrorMode = FullSyncMode;
    {for(int FileTypeA = 0;
         FileTypeA < OnePastLastFileType;
         ++FileTypeA)
    {
        {for(int FileTypeB = 0;
             FileTypeB < OnePastLastFileType;
             ++FileTypeB)
        {
            Config.DiffMethod[FileTypeA][FileTypeB] = TimestampRepresentationTolerantStampDiff;
        }}
    }}
    Config.CopyCommand = 0;
    Config.DeleteCommand = 0;
    Config.WritableWorkspace = true;
    Config.ContinueAlways = false;
    Config.Directory[WorkspaceFileType] = CurrentDirectory;
    Config.Directory[CentralFileType] = "";
    Config.Directory[LocalFileType] = "";
    Config.RetryTimes[WorkspaceFileType] = 0;
    Config.RetryTimes[CentralFileType] = 0;
    Config.RetryTimes[LocalFileType] = 0;
    Config.SyncWorkspace = true;
    Config.SyncCentral = true;
    Config.CentralCache = "";
    Config.SuppressIgnores = false;
    Config.SummarizeIgnores = false;
    Config.SyncPrintThreshold = 256;
    Config.PrintReason = true;
    Config.SummarizeSync = true;
    Config.SummarizeSyncIfNotPrinted = true;
    Config.SummarizeDirs = true;
    Config.SummarizeDirsIfNotPrinted = true;
    Config.LineLength = 0;
    
    FILE *ConfigFile = 0;

    char *ConfigFileName = 0;
    ProcessCommandLine(Config, Strings, ArgCount, Args, false, ConfigFileName);
    if(!ConfigFileName)
    {
        ConfigFileName = FindConfigFile(Strings, CurrentDirectory);
        SetCurrentDirectory(CurrentDirectory);
    }

    if(ConfigFileName)
    {
        printf("Looking for config file at \"%s\"...\n", ConfigFileName);
        size_t FileSize;
        char *ConfigFileBuffer = stb_filec(ConfigFileName, &FileSize);
        if (ConfigFileBuffer)
        {
            stream Stream;
            Stream.At = ConfigFileBuffer;
            Stream.LineIndex = 1;
            ParseConfigFile(Stream, Strings, Config);
            ProcessCommandLine(Config, Strings, ArgCount, Args, true, ConfigFileName);

            InitializeCacheIgnore(Config);

            printf("Configuration:\n");
            printf("  Workspace: \"%s\" (%s)\n", Config.Directory[WorkspaceFileType],
                   Config.WritableWorkspace ? "writable" : "read-only");
            printf("  Central: \"%s\"\n", Config.Directory[CentralFileType]);
            printf("  Central cache: \"%s\"\n", Config.CentralCache);
            printf("  Local: \"%s\"\n", Config.Directory[LocalFileType]);
            printf("  Workspace <-> Local: \"%s\"\n", GetDiffString(Config.DiffMethod[WorkspaceFileType][LocalFileType]));
            printf("  Local <-> Central: \"%s\"\n", GetDiffString(Config.DiffMethod[LocalFileType][CentralFileType]));
            printf("  File rules:\n");
            {for(file_rule *FileRule = Config.FileRuleList;
                 !stb_arr_end(Config.FileRuleList, FileRule);
                 ++FileRule)
            {
                printf("    ");
                if(FileRule->Ignore == DoIgnore)
                {
                    printf("Ignore ");
                }
                if(FileRule->Ignore == DoNotIgnore)
                {
                    printf("Include ");
                }
                if(FileRule->CapVersionCount)
                {
                    printf("Keep%d(Del%d) ", 
                           FileRule->MaxVersionCount,
                           FileRule->MaxVersionCountIfDeleted);
                }
                switch(FileRule->Behavior)
                {
                    case HaltOnConflicts:
                    {
                        // This is the default
                    } break;
   
                    case IgnoreConflicts:
                    {
                        printf("IgnoreConflicts ");
                    } break;

                    default:
                    {
                        printf("\n** WARNING ** UNKNOWN CONFLICT BEHAVIOR\n");
                    } break;
                }
                if(FileRule->PreCheckinCommand)
                {
                    printf("PreCheck[%s] ",
                           FileRule->PreCheckinCommand);
                }
                printf("- \"%s\"\n", FileRule->WildCard);
            }}
            if(Config.CopyCommand)
            {
                printf("  Custom copy: \"%s\"\n", Config.CopyCommand);
            }
            if(Config.DeleteCommand)
            {
                printf("  Custom delete: \"%s\"\n", Config.DeleteCommand);
            }
            if(Config.ContinueAlways)
            {
                printf("  Continue always\n");
            }

            printf("\n");
          
            directory_contents RootContents;
            DirInit(&RootContents);
            BuildDirectory(Config, RootContents, Strings);
            Synchronize(Config, RootContents);
            if(*Config.CentralCache)
            {
                if(!WriteDirectoryToCache(RootContents, CentralFileType,
                                          "Writing central cache", Config.CentralCache))
                {
                    printf("Unable to write to central cache.\n");
                }
            }
            free(ConfigFileBuffer);            
        }
        else
        {
            printf("Unable to read configuration file.\n");
        }
    }
    else
    {
        fprintf(stderr, "No config file found along path %s\n", CurrentDirectory);
    }
    
    return(0);
}
