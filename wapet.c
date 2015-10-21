#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <io.h>

#include "wildcard.cpp"

#define APPNAME             "wapet"
#define TVERSION            "0.6"
#define TDATE               "2007-05-11"
#define TCOPYRIGHT          "Copyright (c) 2002-2003 Case.  Minor additions by Neil Popham, 2004-2007"  // NTP 2004-08-19
#define TDESCRIPTION        "Post-Encoding APE Tagger"

// -------------------------------------

void usage ()
{
    printf ("\n"
            APPNAME " - " TDESCRIPTION "\n"
            TCOPYRIGHT "\n"
            "Version " TVERSION ", Compiled " TDATE "\n\n"
            "Usage: " APPNAME " <target> [tagging options] <encoder> <encoder options>\n"
            "\n"
            "target          : name of the file <encoder> creates\n"
            "tagging options : -t \"tag=value\" ; for example -t \"Artist=%%a\"\n"
            "                : -f \"tag=file\" ; set tag from contents of file <file>\n"
            "                : -ape1 ; use APEv1 instead of APEv2\n"
            "encoder         : name of the encoder\n"
            "encoder options : required parameters for encoder\n"
           );
}

struct APETagFooterStruct {
    unsigned char   ID       [8];
    unsigned char   Version  [4];
    unsigned char   Length   [4];
    unsigned char   TagCount [4];
    unsigned char   Flags    [4];
    unsigned char   Reserved [8];
};

unsigned long Read_LE_Uint32 ( const unsigned char* p )
{
    return ((unsigned long)p[0] <<  0) |
           ((unsigned long)p[1] <<  8) |
           ((unsigned long)p[2] << 16) |
           ((unsigned long)p[3] << 24);
}

void Write_LE_Uint32 ( unsigned char* p, const unsigned long value )
{
    p[0] = (unsigned char) (value >>  0);
    p[1] = (unsigned char) (value >>  8);
    p[2] = (unsigned char) (value >> 16);
    p[3] = (unsigned char) (value >> 24);
}

// Convert UNICODE to UTF-8
// Return number of bytes written
int unicodeToUtf8 ( const WCHAR* lpWideCharStr, char* lpMultiByteStr, int cwcChars )
{
    const unsigned short*   pwc = (unsigned short *)lpWideCharStr;
    unsigned char*          pmb = (unsigned char  *)lpMultiByteStr;
    const unsigned short*   pwce;
    size_t  cBytes = 0;

    if ( cwcChars >= 0 ) {
        pwce = pwc + cwcChars;
    } else {
        pwce = NULL;
    }

    while ( (pwce == NULL) || (pwc < pwce) ) {
        unsigned short  wc = *pwc++;

        if ( wc < 0x00000080 ) {
            *pmb++ = (char)wc;
            cBytes++;
        } else
        if ( wc < 0x00000800 ) {
            *pmb++ = (char)(0xC0 | ((wc >>  6) & 0x1F));
            cBytes++;
            *pmb++ = (char)(0x80 |  (wc        & 0x3F));
            cBytes++;
        } else
        if ( wc < 0x00010000 ) {
            *pmb++ = (char)(0xE0 | ((wc >> 12) & 0x0F));
            cBytes++;
            *pmb++ = (char)(0x80 | ((wc >>  6) & 0x3F));
            cBytes++;
            *pmb++ = (char)(0x80 |  (wc        & 0x3F));
            cBytes++;
        }
        if ( wc == L'\0' )
            return cBytes;
    }

    return cBytes;
}

int ansi_to_utf8 ( const char *ansi, char *utf8, int alen )
{
    WCHAR   *wszValue;
    int     len;

    if ( alen <= 0 ) alen = strlen ( ansi );
    if ( alen == 0 ) {
        *utf8 = '\0';
        return 0;
    }

    if ( (wszValue = (WCHAR *)malloc ((alen + 1) * 2)) == NULL ) {
        fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
        return 1;
    }

    // Convert ANSI value to Unicode
    if ( (len = MultiByteToWideChar (CP_ACP, 0, ansi, alen, wszValue, (alen + 1) * 2)) == 0 ) {
        fprintf ( stderr, APPNAME ": MultiByteToWideChar failed...\n" );
        return 1;
    }
    wszValue[len++] = L'\0';
    // Convert Unicode value to UTF-8
    if ( (len = unicodeToUtf8 (wszValue, utf8, len)) == 0 ) {
        fprintf ( stderr, APPNAME ": unicodeToUtf8 failed...\n" );
        return 1;
    }

    return 0;
}

int len_utf8 ( const char *ansi, int alen )
{
    WCHAR   *wszValue;
    char    *uszValue;
    int     len;

    if ( alen <= 0 ) alen = strlen ( ansi );
    if ( alen == 0 ) return 0;

    if ( (wszValue = (WCHAR *)malloc ((alen + 1) * 2)) == NULL ) {
        fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
        return 1;
    }
    if ( (uszValue = (char *) malloc ((alen + 1) * 3)) == NULL ) {
        fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
        return 1;
    }

    // Convert ANSI value to Unicode
    if ( (len = MultiByteToWideChar (CP_ACP, 0, ansi, alen, wszValue, (alen + 1) * 2)) == 0 ) {
        fprintf ( stderr, APPNAME ": MultiByteToWideChar failed...\n" );
        return 1;
    }
    wszValue[len++] = L'\0';
    // Convert Unicode value to UTF-8
    if ( (len = unicodeToUtf8 (wszValue, uszValue, len)) == 0 ) {
        fprintf ( stderr, APPNAME ": unicodeToUtf8 failed...\n" );
        return 1;
    }

    free ( uszValue );

    return len-1;
}

int proper_tag ( const char *tag )
{
    char *p;
    if ( (!tag) || (*tag == '\0') || (*tag == '=') ) return 0;
    if ( (p = strchr (tag, '=')) == NULL ) return 0;
    if ( *(p+1) == '\0' ) return 0;
    if ( !lstrcmpi (tag, "year=-1") ) return 0;
    return 1;
}

// Writes APEv1/v2 tag
int WriteAPETag ( FILE *fp, char **tags, size_t tag_count, int ape1 )
{
    unsigned char               temp[4];
    unsigned char               *buff;
    unsigned char               *p;
    struct APETagFooterStruct   T;
    unsigned int                flags;
    char                        *value;
    size_t                      itemlen;
    size_t                      valuelen;
    size_t                      TagCount;
    size_t                      TagSize;
    size_t                      i;

    if ( fseek ( fp, 0L, SEEK_END ) != 0 )
        return 1;

    TagCount = 0;
    TagSize = sizeof (T);                                               // calculate size of buffer needed

    for ( i = 0; i < tag_count; i++ ) {
        if ( proper_tag (tags[i]) ) {
            char *t = strchr ( tags[i], '=' ) + 1;
            itemlen = t - tags[i] - 1;
            if ( ape1 ) {
                valuelen = strlen ( t ) + 1;
            } else {
                valuelen = len_utf8 ( t, 0 );
            }
            TagCount++;
            TagSize += 8 + itemlen + 1 + valuelen;
        }
    }

    if ( TagCount == 0 ) return 0;

    if ( (buff = malloc (TagSize + sizeof (T))) == NULL ) {
        fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
        return 1;
    }

    p = buff;

    if ( !ape1 ) {
        flags  = 1<<31;                                     // contains header
        flags |= 1<<29;                                     // this is the header
        memcpy ( T.ID, "APETAGEX", sizeof (T.ID) );         // ID String
        Write_LE_Uint32 ( T.Version , 2000     );           // Version 2.000
        Write_LE_Uint32 ( T.Length  , TagSize  );           // Tag size
        Write_LE_Uint32 ( T.TagCount, TagCount );           // Number of fields
        Write_LE_Uint32 ( T.Flags   , flags    );           // Flags
        memset ( T.Reserved, 0, sizeof (T.Reserved) );      // Reserved
        memcpy ( p, &T, sizeof (T) );   p += sizeof (T);                        // insert header
    }

    for ( i = 0; i < tag_count; i++ ) {
        if ( proper_tag (tags[i]) ) {
            char *t = strchr ( tags[i], '=' ) + 1;
            itemlen = t - tags[i] - 1;
            if ( ape1 ) {
                valuelen = strlen ( t ) + 1;
                value = (char *)malloc ( valuelen+1 );
                if ( value == NULL ) {
                    fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
                    free ( buff );
                    return 1;
                }
                strcpy ( value, t );
            } else {
                valuelen = len_utf8 ( t, 0 );
                value = (char *)malloc ( valuelen+1 );
                if ( value == NULL ) {
                    fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
                    free ( buff );
                    return 1;
                }
                if ( ansi_to_utf8 (t, value, 0) != 0 ) {
                    free ( buff );
                    return 1;
                }
            }

            Write_LE_Uint32 ( temp, valuelen );
            memcpy ( p, temp, 4 );  p += 4;
            Write_LE_Uint32 ( temp, 0 );
            memcpy ( p, temp, 4 );  p += 4;

            memcpy ( p, tags[i], itemlen );
            if ( *p >= 'a' && *p <= 'z' ) *p -= 'a' - 'A';
            p += itemlen;
            *p++ = '\0';

            memcpy ( p, value, valuelen );
            p += valuelen;
            free ( value );
        }
    }

    if ( ape1 ) flags  = 0; else flags = 1<<31;         // contains header
    memcpy ( T.ID, "APETAGEX", sizeof (T.ID) );         // ID String
    if ( ape1 )
        Write_LE_Uint32 ( T.Version, 1000 );            // Version 1.000
    else
        Write_LE_Uint32 ( T.Version, 2000 );            // Version 2.000
    Write_LE_Uint32 ( T.Length  , TagSize  );           // Tag size - header
    Write_LE_Uint32 ( T.TagCount, TagCount );           // Number of fields
    Write_LE_Uint32 ( T.Flags   , flags    );           // Flags
    memset ( T.Reserved, 0, sizeof (T.Reserved) );      // Reserved
    memcpy ( p, &T, sizeof (T) );                                           // insert footer

    if ( ape1 ) i = 0; else i = sizeof (T);
    if ( fwrite (buff, 1, TagSize + i, fp) != TagSize + i ) {
        free ( buff );
        return 1;
    }

    free ( buff );

    return 0;
}

int write_tag ( const char *filename, char **tags, size_t tag_count, int ape1 )
{
    FILE *fp;
    int  error;

    if ( tag_count == 0 ) return 0;

    if ( (fp = fopen (filename, "rb+")) == NULL ) {
        fprintf ( stderr, APPNAME ": failed to open file...\n" );
        return 1;
    }

    if ( (error = WriteAPETag (fp, tags, tag_count, ape1)) != 0 ) {
        fprintf ( stderr, APPNAME ": tag writing failed...\n" );
    }

    fclose (fp);

    return error;
}

long get_filesize (FILE *f)
{
    long cur_pos, length;
    cur_pos = ftell(f);
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, cur_pos, SEEK_SET);
    return length;
}

int main ( int argc, char **argv )
{
    // proggie <encoded file> [tagging options] <encoder + options>
    //    0          1               2..n               n+1

    char **option = NULL;
    char *cmd = NULL;
    int arg_enc = 0;
    int opt_c = 0;
    int enc_c = 0;
    int ape1 = 0;
    int i;

    char * t;
    size_t itemlen;
    FILE*   stream;
    int        ch;
    int force_ape2 = 0;
    int j;
    char * filearg;
    long file_size;

    if ( argc < 3 ) {
        usage ();
        return 1;
    }

    // allocate memory for maximum possible number of tagging options
    option = (char **)malloc ( sizeof(char *) * (argc-2) );
    if ( option == NULL ) {
        fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
        return 1;
    }

    for ( i = 2; i < argc; i++ ) {

        if ( !lstrcmp (argv[i], "-t") ) {  // tagging option
            if ( (++i)+1 >= argc ) {
                fprintf ( stderr, APPNAME ": no encoding settings specified...\n" );
                free ( option );
                return 1;
            }

            option[opt_c++] = argv[i];

        } else if ( !lstrcmp (argv[i], "-f") ) {  // tag from file
            if ( (++i)+1 >= argc ) {
                fprintf ( stderr, APPNAME ": no encoding settings specified...\n" );
                free ( option );
                return 1;
            }
            // check format is <tag>=<value>
            if ( proper_tag (argv[i]) ) {
                t = strchr ( argv[i], '=' ) + 1;
                itemlen = t - argv[i] - 1;

                stream = wild_fopen ( t, "rb" );

                if (!stream && filespec_name (argv[0])) {
                    char *temp = malloc (strlen (argv[0]) + MAX_PATH);

                    strcpy (temp, argv[0]);
                    strcpy (filespec_name (temp), t);
                    stream = wild_fopen (temp, "rb");
                    free (temp);
                }

                if (!stream && filespec_name (argv[1])) {
                    char *temp = malloc (strlen (argv[i]) + MAX_PATH);

                    strcpy (temp, argv[1]);
                    strcpy (filespec_name (temp), t);
                    stream = wild_fopen (temp, "rb");
                    free (temp);
                }

                // check file exists
                if ( stream == NULL ) {
                    fprintf ( stderr, APPNAME ": Failed to open \"%s\" for for reading.\n", t );
                    return 1;
                }

                // force tag type to ape2, as we will be using unicode
                force_ape2 = 1;

                // get file size
                file_size = get_filesize(stream);
                // create dynamic array and set filearg to "<tag>="
                filearg = (char *)malloc(file_size + itemlen + 2);
                strncpy(filearg, argv[i], itemlen + 1);
                // start filling char array after "="
                j = itemlen + 1;
                // read text from file
                while ( ( ch = fgetc (stream) ) != EOF ) {
                    // add character to array
                    filearg[j] = (char)ch;
                    j++;
                }
                // terminate will null char
                filearg[j] = '\0';
                // close stream
                fclose (stream);
                // append to array
                option[opt_c++] = filearg;
            } else {
                fprintf ( stderr, APPNAME ": -f switch requires \"<tag>=<file>\" parameter.\n" );
                free ( option );
                return 1;
            }
        }
        else if ( !lstrcmp (argv[i], "-ape1" ) && !force_ape2 ) {  // use APEv1
            ape1 = 1;
        }
        else {
            arg_enc = i;
            enc_c = argc - arg_enc;
            break;
        }
    }

    if ( enc_c == 0 ) {
        free ( option );
        fprintf ( stderr, APPNAME ": no encoding settings specified...\n" );
        return 1;
    }

    {
        char *p;
        int cmd_len = _MAX_PATH;

        for ( i = 1; i < enc_c; i++ ) {
            cmd_len += strlen ( argv[arg_enc+i] ) + 3;
        }

        cmd = (char *)malloc ( cmd_len );
        if ( cmd == NULL ) {
            free ( option );
            fprintf ( stderr, APPNAME ": failed to allocate memory...\n" );
            return 1;
        }

        p = cmd;

        i = GetShortPathName ( argv[arg_enc], p, _MAX_PATH );
        if ( (i == 0) || (i > _MAX_PATH) ) {
            p += sprintf ( p, "%s", argv[arg_enc] );
        } else {
            p += i;
        }

        for ( i = 1; i < enc_c; i++ ) {
            p += sprintf ( p, " \"%s\"", argv[arg_enc+i] );
        }
    }

    // encode
    system ( cmd );

    i = write_tag ( argv[1], option, opt_c, ape1 );

    free (filearg );
    free ( option );
    free ( cmd );

    return i;
}
