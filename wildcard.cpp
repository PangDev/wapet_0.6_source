/*
               Copyright (c) 1998 - 2007 Conifer Software
                          All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Conifer Software nor the names of its contributors
      may be used to endorse or promote products derived from this software
      without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <windows.h>
#include <stdio.h>
#include <io.h>

static int is_second_byte (char *filespec, char *pos)
{
    //unsigned char *cp = pos;
	char *cp = pos;

    while (cp > filespec && ((cp [-1] >= 0x81 && cp [-1] <= 0x9f) ||
                             (cp [-1] >= 0xe0 && cp [-1] <= 0xfc)))
    cp--;

    return (int)(pos - cp) & 1;
}

char *filespec_wild (char *filespec)
{
    return strpbrk (filespec, "*?");
}

char *filespec_name (char *filespec)
{
    char *cp = filespec + strlen (filespec);
    LANGID langid = GetSystemDefaultLangID ();

    while (--cp >= filespec) {
    if (langid == 0x411 && is_second_byte (filespec, cp))
        --cp;

    if (*cp == '\\' || *cp == ':')
        break;
    }

    if (strlen (cp + 1))
    return cp + 1;
    else
    return NULL;
}

static FILE *wild_fopen (char *filename, const char *mode)
{
    struct _finddata_t _finddata_t;
    char *matchname = NULL;
    FILE *res = NULL;
    intptr_t file;

    if (!filespec_wild (filename) || !filespec_name (filename))
        return fopen (filename, mode);

    if ((file = _findfirst (filename, &_finddata_t)) != (intptr_t) -1) {
        do {
            if (!(_finddata_t.attrib & _A_SUBDIR)) {
                if (matchname) {
                    free (matchname);
                    matchname = NULL;
                    break;
                } else {
                    matchname = (char*) malloc (strlen (filename) + strlen (_finddata_t.name));
                    strcpy (matchname, filename);
                    strcpy (filespec_name (matchname), _finddata_t.name);
                }
            }
        } while (_findnext (file, &_finddata_t) == 0);

        _findclose (file);
    }

    if (matchname) {
        res = fopen (matchname, mode);
        free (matchname);
    }

    return res;
}
