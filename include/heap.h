/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#ifndef __HEAP_H
#define __HEAP_H

#include <stdlib.h>

inline void *  __CRTDECL  operator new( size_t cb )
{
    void *res;

    res = malloc(cb);

    return res;
}

inline void *  __CRTDECL  operator new[]( size_t cb )
{
    void *res;

    res = malloc(cb);

    return res;
}

inline void  __CRTDECL operator delete( void * p )
{
    free(p);
}

inline void  __CRTDECL operator delete[]( void * p )
{
    free(p);
}

#endif // __HEAP_H 
