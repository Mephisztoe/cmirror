/* ========================================================================
   $File: tools/ckeyword/ckeyword.cpp $
   $Date: 2007/04/12 03:22:15AM $
   $Revision: 7 $
   $Creator: Casey Muratori $
   $Notice: $
   ======================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <time.h>

#include <sys/stat.h>

#define CMIRROR_MAX_PATH 4096

struct keyword_context
{
    bool SuspectedBinary;
    bool ErrorOnRead;
    bool ErrorOnWrite;
    bool TypoSuspected;
    bool ReplacedSomething;

    bool UseFileKeyword;
    bool UseNoticeKeyword;
    bool UseDateKeyword;
    bool UseRevisionKeyword;

    FILE *SourceFile;
    FILE *DestFile;

    char *SourceFileName;
    char *SourceRepositoryRelativeName;
    char *NoticeText;
    int SourceRevisionIndex;
    struct stat SourceStat;
};

static int
Input(keyword_context &Context)
{
    int Char = fgetc(Context.SourceFile);
    if(Char == EOF)
    {
        if(ferror(Context.SourceFile))
        {
            Context.ErrorOnRead = true;
        }
    }
    else
    {
        if(((Char >= 0) && (Char <= 9)) ||
           ((Char >= 16) && (Char <= 31)))
        {
            Context.SuspectedBinary = true;
        }
    }

    return(Char);
}

static void
Output(keyword_context &Context, int Char)
{
    if(Char != -1)
    {
        if(fputc(Char, Context.DestFile) == EOF)
        {
            Context.ErrorOnWrite = true;
        }
    }
}

static bool
EndOfFile(keyword_context &Context)
{
    return(feof(Context.SourceFile) != 0);
}

static void
SkipToNextDollarsign(keyword_context &Context, bool StopAtNewline)
{
    int Char = 0;
    while(!EndOfFile(Context) && (Char != '$'))
    {
        Char = Input(Context);
        if(StopAtNewline && ((Char == '\n') || (Char == '\r')))
        {
            Context.TypoSuspected = true;
            
            Output(Context, Char);
            break;
        }
    }

    if(Char != '$')
    {
        Context.TypoSuspected = true;
    }
}

static void
MatchAnchor(keyword_context &Context, bool StopAtNewline)
{
    long OriginalAt = ftell(Context.SourceFile);
    if(OriginalAt >= 0)
    {
        char Keyword[32];
        int MaxKeywordLength = (sizeof(Keyword) / sizeof(Keyword[0]));
        {for(int CharIndex = 0;
             CharIndex < MaxKeywordLength;
             ++CharIndex)
        {
            Keyword[CharIndex] = EndOfFile(Context) ? '\0' : (char)Input(Context);
        }}
        Keyword[MaxKeywordLength - 1] = '\0';
        if(fseek(Context.SourceFile, OriginalAt, SEEK_SET) != 0)
        {
            Context.ErrorOnRead = true;
        }
        
        if(Context.UseFileKeyword &&
           ((strncmp(Keyword, "File: ", 6) == 0) ||
            (strncmp(Keyword, "RCSFile: ", 9) == 0)))
        {
            Context.ReplacedSomething = true;

            fprintf(Context.DestFile, "$File: %s $", Context.SourceRepositoryRelativeName);
            SkipToNextDollarsign(Context, StopAtNewline);
        }
        else if(Context.UseNoticeKeyword &&
                (strncmp(Keyword, "Notice: ", 8) == 0))
        {
            Context.ReplacedSomething = true;

            fprintf(Context.DestFile, "$Notice: ");
            {for(char *I = Context.NoticeText;
                 *I;
                 ++I) 
            {
                if((*I == '\\') &&
                   (*(I + 1) == 'n'))
                {
                    fprintf(Context.DestFile, "\r\n");
                    ++I;
                }
                else
                {
                    fputc(*I, Context.DestFile);
                }
            }}
            fprintf(Context.DestFile, " $");
            SkipToNextDollarsign(Context, StopAtNewline);
        }
        else if(Context.UseDateKeyword &&
                (strncmp(Keyword, "Date: ", 6) == 0))
        {
            Context.ReplacedSomething = true;

            struct tm *TM = localtime(&Context.SourceStat.st_mtime);
            int Hour = 12;
            char *AMPM = "AM";
            if(TM->tm_hour >= 1)
            {
                Hour = TM->tm_hour;
            }
            if(TM->tm_hour >= 12)
            {
                AMPM = "PM";
            }
            if(TM->tm_hour >= 13)
            {
                Hour -= 12; 
            }    
            
            fprintf(Context.DestFile, "$Date: %04d/%02d/%02d %02d:%02d:%02d%s $",
                    TM->tm_year + 1900,
                    TM->tm_mon + 1,
                    TM->tm_mday,
                    Hour,
                    TM->tm_min,
                    TM->tm_sec,
                    AMPM);
            SkipToNextDollarsign(Context, StopAtNewline);
        }
        else if(Context.UseRevisionKeyword &&
                (strncmp(Keyword, "Revision: ", 10) == 0))
        {
            Context.ReplacedSomething = true;

            fprintf(Context.DestFile, "$Revision: %d $", Context.SourceRevisionIndex);
            SkipToNextDollarsign(Context, StopAtNewline);
        }
        else
        {
            Output(Context, '$');
        }
    }
    else
    {
        Context.ErrorOnRead = true;
    }
}

void
ProcessFile(keyword_context &Context)
{
    int InsideCComment = 0;
    bool InsideCPPComment = false;
    
    while(!EndOfFile(Context))
    {
        int Char = Input(Context);
        if((InsideCComment > 0) || InsideCPPComment)
        {
            if(Char == '$')
            {
                MatchAnchor(Context, InsideCComment <= 0);
            }
            else
            {
                if(Char == '*')
                {                
                    Output(Context, Char);
                    Char = Input(Context);
                    if(Char == '/')
                    {
                        --InsideCComment;
                    }
                }
                else if((Char == '\n') || (Char == '\r'))
                {
                    InsideCPPComment = false;
                }

                Output(Context, Char);
            }
        }
        else
        {
            if(Char == '/')
            {
                Output(Context, Char);
                Char = Input(Context);
                if(Char == '/')
                {
                    InsideCPPComment = true;
                }
                else if(Char == '*')
                {
                    ++InsideCComment;
                }
            }

            Output(Context, Char);
        }
    }
}

int
Go(keyword_context &Context)
{
    int Result = -1;

    char DeleteFileName[CMIRROR_MAX_PATH];
    sprintf(DeleteFileName, "%s_DELETE", Context.SourceFileName);

    char DestFileName[CMIRROR_MAX_PATH];
    sprintf(DestFileName, "%s_KEYWORDS", Context.SourceFileName);
    
    stat(Context.SourceFileName, &Context.SourceStat);
    Context.SourceFile = fopen(Context.SourceFileName, "rb");
    Context.DestFile = fopen(DestFileName, "wb");
    if(Context.SourceFile && Context.DestFile)
    {
        ProcessFile(Context);

        Result = 0;

        if(Context.SuspectedBinary)
        {
            printf("CKeyword: file appears to binary.\n");
            Result = -1;
        }

        if(Context.ErrorOnRead)
        {
            printf("CKeyword: error reading file %s.\n", Context.SourceFileName);
            Result = -1;
        }

        if(Context.ErrorOnWrite)
        {
            printf("CKeyword: error writing file.\n");
            Result = -1;
        }

        if(Context.TypoSuspected)
        {
            printf("CKeyword: typo suspected.\n");
            Result = -1;
        }
    }
    else
    {
        printf("CKeyword: Cannot open files\n");
    }
    fclose(Context.SourceFile);
    fclose(Context.DestFile);

    if((Result == 0) && Context.ReplacedSomething)
    {
        if(rename(Context.SourceFileName, DeleteFileName) == 0)
        {
            if(rename(DestFileName, Context.SourceFileName) == 0)
            {
                unlink(DeleteFileName);
            }
            else
            {
                printf("CKeyword: Unable to replace source file with keyword-stamped source file.\n");
                rename(DeleteFileName, Context.SourceFileName);
                unlink(DestFileName);
            }
        }
        else
        {
            printf("CKeyword: Unable to move source file for keyword replacement.\n");
            unlink(DestFileName);
        }
    }
    else
    {
        unlink(DestFileName);
    }

    return(Result);
}

int
main(int ArgCount, char **Args)
{
    int Result = -1;

    keyword_context Context = {0};
        
    if(ArgCount == 4)
    {
        Context.SourceFileName = Args[1];
        Context.SourceRepositoryRelativeName = Args[2];
        Context.SourceRevisionIndex = atoi(Args[3]);

        Context.UseFileKeyword = true;
        Context.UseDateKeyword = true;
        Context.UseRevisionKeyword = true;

        Result = Go(Context);
    }
    else if(ArgCount == 3)
    {
        Context.SourceFileName = Args[1];
        Context.NoticeText = Args[2];

        Context.UseNoticeKeyword = true;

        Result = Go(Context);
    }
    else
    {
        printf("CKeyword: Needs either 2 or 3 arguments\n");
    }

    return(Result);
}