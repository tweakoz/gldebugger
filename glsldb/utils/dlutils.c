/******************************************************************************

Copyright (C) 2006-2009 Institute for Visualization and Interactive Systems
(VIS), Universität Stuttgart.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice, this
	list of conditions and the following disclaimer in the documentation and/or
	other materials provided with the distribution.

  * Neither the name of the name of VIS, Universität Stuttgart nor the names
	of its contributors may be used to endorse or promote products derived from
	this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*******************************************************************************/

#include <dlfcn.h>

#include <stdio.h>
#include <stdlib.h>

#include "dlutils.h"
#include "dbgprint.h"

#include <assert.h>

LibraryHandle openLibrary(const char *library)
{
	LibraryHandle handle;

	printf( "Opening Library<%s>\n", library );
		
	if (! (handle = dlopen(library, RTLD_LAZY | RTLD_GLOBAL))) {
		dbgPrint(DBGLVL_WARNING, "%s: %s\n", library, dlerror());
		assert(0);
		return NULL;
	}
	return handle;
}

void closeLibrary(LibraryHandle handle)
{
	dlclose(handle);
}

void *resolveSymbol(LibraryHandle handle, const char *symbol) 
{
	void *ret;
	char *error;

	ret = dlsym(handle, symbol);
	if ((error = dlerror())) {
		dbgPrint(DBGLVL_WARNING, "Error resolving symbol %s: %s\n",
		         symbol, error);
		return NULL;
	}
	return ret;
}

void *resolveSymbolNoCheck(LibraryHandle handle, const char *symbol) 
{
	return dlsym(handle, symbol);
}

