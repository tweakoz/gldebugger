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

#ifndef READBACK_H
#define READBACK_H

#include <gl_interpose/debuglibExport.h>

#ifdef __cplusplus
extern "C" {
#endif /* _CPP */

DBGLIBLOCAL void setDbgOutputTarget(void);

DBGLIBLOCAL void restoreOutputTarget(void);

DBGLIBLOCAL void readRenderBuffer(void);

DBGLIBLOCAL int readBackRenderBuffer(int numComponents, int format,
                                     int *width, int *height,
                                     void **buffer);

DBGLIBLOCAL void clearRenderBuffer(void);

/* FIXME CHECK AGAIN!!!
DBGLIBLOCAL int setDbgRenderState(int target);
*/

DBGLIBLOCAL int endTransformFeedback(int primitiveType, int numFloatsPerVertex,
                                     float **data, int *numPrimitives,
                                     int *numVertices);

DBGLIBLOCAL int beginTransformFeedback(int primitiveType);

#ifdef __cplusplus
} //extern "C" {
#endif /* _CPP */

#endif
