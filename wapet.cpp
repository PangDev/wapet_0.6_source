#include <windows.h>
#include <tchar.h>
#include <atlstr.h>
#include <direct.h>
#include <stdio.h>
#include <io.h>

#define APPNAME             _T("wapet")
#define TVERSION            _T("0.6")
#define TDATE               _T("2007-05-11")
#define TCOPYRIGHT          _T("Copyright (c) 2002-2003 Case.  Minor additions by Neil Popham, 2004-2007")  // NTP 2004-08-19
#define TDESCRIPTION        _T("Post-Encoding APE Tagger")

// -------------------------------------

void usage ()
{
    _tprintf (_T("\n")
            APPNAME _T(" - ") TDESCRIPTION _T("\n")
            TCOPYRIGHT _T("\n")
            _T("Version ") TVERSION _T(", Compiled ") TDATE _T("\n\n")
            _T("Usage: ") APPNAME _T(" <target> [tagging options] <encoder> <encoder options>\n")
            _T("\n")
            _T("target          : name of the file <encoder> creates\n")
            _T("tagging options : -t \"tag=value\" ; for example -t \"Artist=%%a\"\n")
            _T("                : -f \"tag=file\" ; set tag from contents of file <file>\n")
            _T("                : -ape1 ; use APEv1 instead of APEv2\n")
            _T("encoder         : name of the encoder\n")
            _T("encoder options : required parameters for encoder\n")
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
        if ( wc == _T('\0') )
            return cBytes;
    }

    return cBytes;
}

int ansi_to_utf8 ( const TCHAR *ansi, char *utf8 )
{
	CT2CA utf8str(ansi, CP_UTF8);
	size_t outlen = ::strlen(utf8str);
	if (outlen > 0) {
		strcpy(utf8, utf8str);
	}
	else {
		*utf8 = '\0';
	}
	return 0;
}

int len_utf8 ( const TCHAR *ansi )
{
	CT2CA utf8str(ansi, CP_UTF8);
	return ::strlen(utf8str);
}

int proper_tag ( TCHAR *tag )
{
    TCHAR *p;
    if ( (!tag) || (*tag == _T('\0')) || (*tag == _T('=')) ) return 0;
    if ( (p = _tcschr(tag, _T('='))) == NULL ) return 0;
    if ( *(p+1) == _T('\0') ) return 0;
    if ( !_tcscmp (tag, _T("year=-1")) ) return 0;
    return 1;
}

// Writes APEv1/v2 tag
int WriteAPETag ( FILE *fp, TCHAR **tags, size_t tag_count, int ape1 )
{
    unsigned char               temp[4];
    unsigned char               *buff;
    unsigned char               *p;
    struct APETagFooterStruct   T;
    unsigned int                flags;
	char                        *tag;
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
            TCHAR *t = _tcschr ( tags[i], _T('=') ) + 1;
			TCHAR *equal = t - 1;
			*equal = _T('\0');
            itemlen = t - tags[i] - 1;
            if ( ape1 ) {
                itemlen = _tcsclen ( tags[i] );
                valuelen = _tcsclen ( t ) + 1;
            } else {
                itemlen = len_utf8 ( tags[i] );
                valuelen = len_utf8 ( t );
            }
			*equal = _T('=');
            TagCount++;
            TagSize += 8 + itemlen + 1 + valuelen;
        }
    }

    if ( TagCount == 0 ) return 0;

    if ( (buff = (unsigned char *)malloc (TagSize + sizeof (T))) == NULL ) {
        _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
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
            TCHAR *t = _tcschr ( tags[i], _T('=') ) + 1;
			TCHAR *equal = t - 1;
            itemlen = t - tags[i] - 1;
			*equal = _T('\0');
            if ( ape1 ) {
				tag = (char *)malloc( sizeof(TCHAR) * (itemlen+1) );
                if ( tag == NULL ) {
                    _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
                    free ( buff );
                    return 1;
                }
                valuelen = _tcsclen ( t ) + 1;
                value = (char *)malloc ( sizeof(TCHAR) * (valuelen+1) );
                if ( value == NULL ) {
                    _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
					free ( tag );
                    free ( buff );
                    return 1;
                }
				_tcscpy ( (TCHAR*)tag, tags[i] );
                if ( *(TCHAR*)tag >= _T('a') && *(TCHAR*)tag <= _T('z') ) *(TCHAR*)tag -= _T('a') - _T('A');
				itemlen *= sizeof(TCHAR);
				_tcscpy ( (TCHAR*)value, t );
            } else {
				itemlen = len_utf8 (tags[i]);
				tag = (char*)malloc ( itemlen+1 );
				if (tag == NULL) {
                    _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
                    free ( buff );
                    return 1;
				}
                valuelen = len_utf8 ( t );
                value = (char *)malloc ( valuelen+1 );
                if ( value == NULL ) {
                    _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
					free ( tag );
                    free ( buff );
                    return 1;
                }
				if ( ansi_to_utf8 (tags[i], tag) != 0 ) {
					free ( tag );
					free ( value );
					free ( buff );
					return 1;
				}
                if ( ansi_to_utf8 (t, value) != 0 ) {
					free ( tag );
					free ( value );
                    free ( buff );
                    return 1;
                }
            }
			*equal = _T('=');

            Write_LE_Uint32 ( temp, valuelen );
            memcpy ( p, temp, 4 );  p += 4;
            Write_LE_Uint32 ( temp, 0 );
            memcpy ( p, temp, 4 );  p += 4;

            memcpy ( p, tag, itemlen );
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

int write_tag ( const TCHAR *filename, TCHAR **tags, size_t tag_count, int ape1 )
{
    FILE *fp;
    int  error;

    if ( tag_count == 0 ) return 0;

    if ( (fp = _tfopen (filename, _T("rb+"))) == NULL ) {
        _ftprintf ( stderr, APPNAME _T(": failed to open file...\n") );
        return 1;
    }

    if ( (error = WriteAPETag (fp, tags, tag_count, ape1)) != 0 ) {
        _ftprintf ( stderr, APPNAME _T(": tag writing failed...\n") );
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

int _tmain ( int argc, TCHAR **argv )
{
    // proggie <encoded file> [tagging options] <encoder + options>
    //    0          1               2..n               n+1

    TCHAR **option = NULL;
    TCHAR *cmd = NULL;
    int arg_enc = 0;
    int opt_c = 0;
    int enc_c = 0;
    int ape1 = 0;
    int i;

    TCHAR * t;
    size_t itemlen;
    FILE*   stream;
    int        ch;
    int force_ape2 = 0;
    int j;
    TCHAR * filearg = NULL;
    long file_size;

    if ( argc < 3 ) {
        usage ();
        return 1;
    }

    // allocate memory for maximum possible number of tagging options
    option = (TCHAR **)malloc ( sizeof(TCHAR *) * (argc-2) );
    if ( option == NULL ) {
        _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
        return 1;
    }

    for ( i = 2; i < argc; i++ ) {

        if ( !_tcscmp (argv[i], _T("-t")) ) {  // tagging option
            if ( (++i)+1 >= argc ) {
                _ftprintf ( stderr, APPNAME _T(": no encoding settings specified...\n") );
                free ( option );
                return 1;
            }

            option[opt_c++] = argv[i];

        } else if ( !_tcscmp (argv[i], _T("-f")) ) {  // tag from file
            if ( (++i)+1 >= argc ) {
                _ftprintf ( stderr, APPNAME _T(": no encoding settings specified...\n") );
                free ( option );
                return 1;
            }
            // check format is <tag>=<value>
            if ( proper_tag (argv[i]) ) {
                t = _tcschr( argv[i], _T('=') ) + 1;
                itemlen = t - argv[i] - 1;

                stream = _tfopen ( t, _T("rb") );
                // check file exists
                if ( stream == NULL ) {
                    _ftprintf ( stderr, APPNAME _T(": Failed to open \"%s\" for for reading.\n"), t );
                    return 1;
                }

                // force tag type to ape2, as we will be using unicode
                force_ape2 = 1;

                // get file size
                file_size = get_filesize(stream);
                // create dynamic array and set filearg to "<tag>="
                filearg = (TCHAR *)malloc(sizeof(TCHAR) * (file_size + itemlen + 2));
                _tcsnccpy(filearg, argv[i], itemlen + 1);
                // start filling char array after "="
                j = itemlen + 1;
                // read text from file
                while ( ( ch = _fgettc(stream) ) != EOF ) {
                    // add character to array
                    filearg[j] = (TCHAR)ch;
                    j++;
                }
                // terminate will null char
                filearg[j] = _T('\0');
                // close stream
                fclose (stream);
                // append to array
                option[opt_c++] = filearg;
            } else {
                _ftprintf ( stderr, APPNAME _T(": -f switch requires \"<tag>=<file>\" parameter.\n") );
                free ( option );
                return 1;
            }
        }
        else if ( !_tcscmp (argv[i], _T("-ape1") ) && !force_ape2 ) {  // use APEv1
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
        _ftprintf ( stderr, APPNAME _T(": no encoding settings specified...\n") );
        return 1;
    }

    {
        TCHAR *p;
        int cmd_len = _MAX_PATH;

        for ( i = 1; i < enc_c; i++ ) {
            cmd_len += _tcsclen ( argv[arg_enc+i] ) + 3;
        }

        cmd = (TCHAR *)malloc ( sizeof(TCHAR) * cmd_len );
        if ( cmd == NULL ) {
            free ( option );
            _ftprintf ( stderr, APPNAME _T(": failed to allocate memory...\n") );
            return 1;
        }

        p = cmd;

        i = GetShortPathName ( argv[arg_enc], p, _MAX_PATH );
        if ( (i == 0) || (i > _MAX_PATH) ) {
            p += _stprintf ( p, _T("%s"), argv[arg_enc] );
        } else {
            p += i;
        }

        for ( i = 1; i < enc_c; i++ ) {
            p += _stprintf ( p, _T(" \"%s\""), argv[arg_enc+i] );
        }
    }

    // encode
    _tsystem ( cmd );

    i = write_tag ( argv[1], option, opt_c, ape1 );

    free ( filearg );
    free ( option );
    free ( cmd );

    return i;
}
