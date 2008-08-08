/*
 * PROJECT:         ReactOS Win32K
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            subsystems/win32/win32k/ntuser/remote.c
 * PURPOSE:         Remote Desktop Routines
 * PROGRAMMERS:     Stefan Ginsberg (stefan__100__@hotmail.com)
 */

/* INCLUDES ******************************************************************/

#include <win32k.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

DWORD
APIENTRY
NtUserRemoteConnect(DWORD dwUnknown1,
                    DWORD dwUnknown2,
                    DWORD dwUnknown3)
{
    UNIMPLEMENTED;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteRedrawRectangle(DWORD dwUnknown1,
                            DWORD dwUnknown2,
                            DWORD dwUnknown3,
                            DWORD dwUnknown4)
{
    UNIMPLEMENTED;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteRedrawScreen(VOID)
{
    UNIMPLEMENTED;
    return 0;
}

DWORD
APIENTRY
NtUserRemoteStopScreenUpdates(VOID)
{
    UNIMPLEMENTED;
    return 0;
}
