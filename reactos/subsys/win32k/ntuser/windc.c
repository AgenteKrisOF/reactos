/*
 *  ReactOS W32 Subsystem
 *  Copyright (C) 1998, 1999, 2000, 2001, 2002, 2003 ReactOS Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
/* $Id: windc.c,v 1.66.8.1 2004/07/07 18:03:01 weiden Exp $
 *
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * PURPOSE:          Window classes
 * FILE:             subsys/win32k/ntuser/class.c
 * PROGRAMER:        Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISION HISTORY:
 *       06-06-2001  CSH  Created
 */

/* INCLUDES ******************************************************************/

#include <w32k.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

/* NOTE - I think we should store this per window station (including gdi objects) */

static FAST_MUTEX DceListLock;
static PDCE FirstDce = NULL;
static HDC defaultDCstate;

#define DCX_CACHECOMPAREMASK (DCX_CLIPSIBLINGS | DCX_CLIPCHILDREN | \
                              DCX_CACHE | DCX_WINDOW | DCX_PARENTCLIP)

/* FUNCTIONS *****************************************************************/

VOID FASTCALL
DceInit(VOID)
{
  ExInitializeFastMutex(&DceListLock);
}

HRGN FASTCALL
DceGetVisRgn(PWINDOW_OBJECT Window, ULONG Flags, PWINDOW_OBJECT Child, ULONG CFlags)
{
  HRGN VisRgn;

  ASSERT(Window);

  VisRgn = VIS_ComputeVisibleRegion(Window,
                                    0 == (Flags & DCX_WINDOW),
                                    0 != (Flags & DCX_CLIPCHILDREN),
                                    0 != (Flags & DCX_CLIPSIBLINGS));

  return VisRgn;
}

PDCE FASTCALL
DceAllocDCE(PWINDOW_OBJECT Window, DCE_TYPE Type)
{
  HDCE DceHandle;
  DCE* Dce;
  UNICODE_STRING DriverName;

  DceHandle = DCEOBJ_AllocDCE();
  if(!DceHandle)
    return NULL;
  
  RtlInitUnicodeString(&DriverName, L"DISPLAY");
  
  Dce = DCEOBJ_LockDCE(DceHandle);
  /* No real locking, just get the pointer */
  DCEOBJ_UnlockDCE(DceHandle);
  Dce->Handle = DceHandle;
  Dce->hDC = IntGdiCreateDC(&DriverName, NULL, NULL, NULL);
  if (NULL == defaultDCstate)
    {
      defaultDCstate = NtGdiGetDCState(Dce->hDC);
      GDIOBJ_SetOwnership(defaultDCstate, NULL);
    }
  Dce->CurrentWindow = Window;
  Dce->hClipRgn = NULL;
  
  Dce->next = FirstDce;
  FirstDce = Dce;

  if (Type != DCE_CACHE_DC)
    {
      Dce->DCXFlags = DCX_DCEBUSY;
      if (Window != NULL)
	{
	  if (Window->Style & WS_CLIPCHILDREN)
	    {
	      Dce->DCXFlags |= DCX_CLIPCHILDREN;
	    }
	  if (Window->Style & WS_CLIPSIBLINGS)
	    {
	      Dce->DCXFlags |= DCX_CLIPSIBLINGS;
	    }
	}
    }
  else
    {
      Dce->DCXFlags = DCX_CACHE | DCX_DCEEMPTY;
    }

  return(Dce);
}

VOID STATIC STDCALL
DceSetDrawable(PWINDOW_OBJECT WindowObject, HDC hDC, ULONG Flags,
	       BOOL SetClipOrigin)
{
  DC *dc = DC_LockDc(hDC);
  if(!dc)
    return;
  
  if (WindowObject == NULL)
    {
      dc->w.DCOrgX = 0;
      dc->w.DCOrgY = 0;
    }
  else
    {
      if (Flags & DCX_WINDOW)
	{
	  dc->w.DCOrgX = WindowObject->WindowRect.left;
	  dc->w.DCOrgY = WindowObject->WindowRect.top;
	}
      else
	{
	  dc->w.DCOrgX = WindowObject->ClientRect.left;
	  dc->w.DCOrgY = WindowObject->ClientRect.top;
	}
    }
  DC_UnlockDc(hDC);
}


STATIC VOID FASTCALL
DceDeleteClipRgn(DCE* Dce)
{
  Dce->DCXFlags &= ~(DCX_EXCLUDERGN | DCX_INTERSECTRGN | DCX_WINDOWPAINT);

  if (Dce->DCXFlags & DCX_KEEPCLIPRGN )
    {
      Dce->DCXFlags &= ~DCX_KEEPCLIPRGN;
    }
  else if (Dce->hClipRgn > (HRGN) 1)
    {
      NtGdiDeleteObject(Dce->hClipRgn);
    }

  Dce->hClipRgn = NULL;

  /* make it dirty so that the vis rgn gets recomputed next time */
  Dce->DCXFlags |= DCX_DCEDIRTY;
}

STATIC INT FASTCALL
DceReleaseDC(DCE* dce)
{
  if (DCX_DCEBUSY != (dce->DCXFlags & (DCX_DCEEMPTY | DCX_DCEBUSY)))
    {
      return 0;
    }

  /* restore previous visible region */

  if ((dce->DCXFlags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN)) &&
      (dce->DCXFlags & (DCX_CACHE | DCX_WINDOWPAINT)) )
    {
      DceDeleteClipRgn(dce);
    }

  if (dce->DCXFlags & DCX_CACHE)
    {
      /* make the DC clean so that SetDCState doesn't try to update the vis rgn */
      NtGdiSetHookFlags(dce->hDC, DCHF_VALIDATEVISRGN);
      NtGdiSetDCState(dce->hDC, defaultDCstate);
      dce->DCXFlags &= ~DCX_DCEBUSY;
      if (dce->DCXFlags & DCX_DCEDIRTY)
	{
	  /* don't keep around invalidated entries
	   * because SetDCState() disables hVisRgn updates
	   * by removing dirty bit. */
	  dce->CurrentWindow = NULL;
	  dce->DCXFlags &= DCX_CACHE;
	  dce->DCXFlags |= DCX_DCEEMPTY;
	}
    }

  return 1;
}

STATIC VOID FASTCALL
DceUpdateVisRgn(DCE *Dce, PWINDOW_OBJECT Window, ULONG Flags)
{
   HANDLE hRgnVisible = NULL;
   ULONG DcxFlags;
   PWINDOW_OBJECT DesktopWindow;

   if (Flags & DCX_PARENTCLIP)
   {
      PWINDOW_OBJECT Parent;

      Parent = IntGetParentObject(Window);
      if(!Parent)
      {
        hRgnVisible = NULL;
        goto noparent;
      }
      
      if (Parent->Style & WS_CLIPSIBLINGS)
      {
         DcxFlags = DCX_CLIPSIBLINGS | 
            (Flags & ~(DCX_CLIPCHILDREN | DCX_WINDOW));
      }
      else
      {
         DcxFlags = Flags & ~(DCX_CLIPSIBLINGS | DCX_CLIPCHILDREN | DCX_WINDOW);
      }
      hRgnVisible = DceGetVisRgn(Parent, DcxFlags, Window, Flags);
      if (hRgnVisible == NULL)
      {
         hRgnVisible = NtGdiCreateRectRgn(0, 0, 0, 0);
      }
      else
      {
         if (0 == (Flags & DCX_WINDOW))
         {
            NtGdiOffsetRgn(
               hRgnVisible,
               Parent->ClientRect.left - Window->ClientRect.left,
               Parent->ClientRect.top - Window->ClientRect.top);
         }
         else
         {
            NtGdiOffsetRgn(
               hRgnVisible,
               Parent->WindowRect.left - Window->WindowRect.left,
               Parent->WindowRect.top - Window->WindowRect.top);
         }
      }
   }
   else if (Window == NULL)
   {
      DesktopWindow = IntGetDesktopWindow();
      if (NULL != DesktopWindow)
      {
         hRgnVisible = UnsafeIntCreateRectRgnIndirect(&DesktopWindow->WindowRect);;
      }
      else
      {
         hRgnVisible = NULL;
      }
   }
   else
   {
      hRgnVisible = DceGetVisRgn(Window, Flags, 0, 0);
   }

noparent:
   if (Flags & DCX_INTERSECTRGN)
   {
      NtGdiCombineRgn(hRgnVisible, hRgnVisible, Dce->hClipRgn, RGN_AND);
   }

   if (Flags & DCX_EXCLUDERGN)
   {
      NtGdiCombineRgn(hRgnVisible, hRgnVisible, Dce->hClipRgn, RGN_DIFF);
   }

   Dce->DCXFlags &= ~DCX_DCEDIRTY;
   NtGdiSelectVisRgn(Dce->hDC, hRgnVisible);

   if (hRgnVisible != NULL)
   {
      NtGdiDeleteObject(hRgnVisible);
   }
}

HDC FASTCALL
IntGetDCEx(PWINDOW_OBJECT Window, HRGN ClipRegion, ULONG Flags)
{
  PWINDOW_OBJECT Parent;
  ULONG DcxFlags;
  DCE* Dce;
  BOOL UpdateVisRgn = TRUE;
  BOOL UpdateClipOrigin = FALSE;

  if (NULL == Window)
    {
      Flags &= ~DCX_USESTYLE;
    }

  if (NULL == Window || NULL == Window->Dce)
    {
      Flags |= DCX_CACHE;
    }


  if (Flags & DCX_USESTYLE)
    {
      /* Window is always != NULL here! */
      
      Flags &= ~(DCX_CLIPCHILDREN | DCX_CLIPSIBLINGS | DCX_PARENTCLIP);

      if (Window->Style & WS_CLIPSIBLINGS)
	{
	  Flags |= DCX_CLIPSIBLINGS;
	}

      if (!(Flags & DCX_WINDOW))
	{
	  if (Window->Class->style & CS_PARENTDC)
	    {
	      Flags |= DCX_PARENTCLIP;
	    }

	  if (Window->Style & WS_CLIPCHILDREN &&
	      !(Window->Style & WS_MINIMIZE))
	    {
	      Flags |= DCX_CLIPCHILDREN;
	    }
	}
      else
	{
	  Flags |= DCX_CACHE;
	}
    }

  if (Flags & DCX_NOCLIPCHILDREN)
    {
      Flags |= DCX_CACHE;
      Flags |= ~(DCX_PARENTCLIP | DCX_CLIPCHILDREN);
    }

  if (Flags & DCX_WINDOW)
    {
      Flags = (Flags & ~DCX_CLIPCHILDREN) | DCX_CACHE;
    }

  Parent = (Window ? Window->Parent : NULL);
  
  if (NULL == Window || !(Window->Style & WS_CHILD) || NULL == Parent)
    {
      Flags &= ~DCX_PARENTCLIP;
    }
  else if (Flags & DCX_PARENTCLIP)
    {
      Flags |= DCX_CACHE;
      if ((Window->Style & WS_VISIBLE) &&
          (Parent->Style & WS_VISIBLE))
        {
          Flags &= ~DCX_CLIPCHILDREN;
          if (Parent->Style & WS_CLIPSIBLINGS)
            {
              Flags |= DCX_CLIPSIBLINGS;
            }
        }
    }
  
  DcxFlags = Flags & DCX_CACHECOMPAREMASK;

  if (Flags & DCX_CACHE)
    {
      DCE* DceEmpty = NULL;
      DCE* DceUnused = NULL;

      for (Dce = FirstDce; Dce != NULL; Dce = Dce->next)
	{
	  if ((Dce->DCXFlags & (DCX_CACHE | DCX_DCEBUSY)) == DCX_CACHE)
	    {
	      DceUnused = Dce;
	      if (Dce->DCXFlags & DCX_DCEEMPTY)
		{
		  DceEmpty = Dce;
		}
	      else if (Dce->CurrentWindow == Window &&
		       ((Dce->DCXFlags & DCX_CACHECOMPAREMASK) == DcxFlags))
		{
#if 0 /* FIXME */
		  UpdateVisRgn = FALSE;
#endif
		  UpdateClipOrigin = TRUE;
		  break;
		}
	    }
	}
      
      if (Dce == NULL)
	{
	  Dce = (DceEmpty == NULL) ? DceEmpty : DceUnused;
	}

      if (Dce == NULL)
	{
	  Dce = DceAllocDCE(NULL, DCE_CACHE_DC);
	}
      else if (! GDIOBJ_OwnedByCurrentProcess(Dce-> Handle))
        {
          GDIOBJ_SetOwnership(Dce->Handle, PsGetCurrentProcess());
          DC_SetOwnership(Dce->hDC, PsGetCurrentProcess());
        }
    }
  else
    {
      Dce = Window->Dce;
      if (NULL != Dce && Dce->CurrentWindow == Window)
        {
          UpdateVisRgn = FALSE; /* updated automatically, via DCHook() */
        }
#if 1 /* FIXME */
      UpdateVisRgn = TRUE;
#endif
    }

  if (NULL == Dce)
    {
      return(NULL);
    }

  Dce->CurrentWindow = Window;
  Dce->DCXFlags = DcxFlags | (Flags & DCX_WINDOWPAINT) | DCX_DCEBUSY;

  if (0 == (Flags & (DCX_EXCLUDERGN | DCX_INTERSECTRGN)) && NULL != ClipRegion)
    {
      NtGdiDeleteObject(ClipRegion);
      ClipRegion = NULL;
    }

  if (NULL != Dce->hClipRgn)
    {
      DceDeleteClipRgn(Dce);
      Dce->hClipRgn = NULL;
    }

  if (0 != (Flags & DCX_INTERSECTUPDATE) && NULL == ClipRegion)
    {
      Dce->hClipRgn = NtGdiCreateRectRgn(0, 0, 0, 0);
      if (Dce->hClipRgn && Window->UpdateRegion)
        {
          NtGdiCombineRgn(Dce->hClipRgn, Window->UpdateRegion, NULL, RGN_COPY);
          if(Window->WindowRegion && !(Window->Style & WS_MINIMIZE))
            NtGdiCombineRgn(Dce->hClipRgn, Dce->hClipRgn, Window->WindowRegion, RGN_AND);
          if (!(Flags & DCX_WINDOW))
            {
              NtGdiOffsetRgn(Dce->hClipRgn,
                Window->WindowRect.left - Window->ClientRect.left,
                Window->WindowRect.top - Window->ClientRect.top);
            }
        }
      Flags |= DCX_INTERSECTRGN;
    }

  if (ClipRegion == (HRGN) 1)
    {
      if (!(Flags & DCX_WINDOW))
        {
          Dce->hClipRgn = UnsafeIntCreateRectRgnIndirect(&Window->ClientRect);
          if(!Window->WindowRegion || (Window->Style & WS_MINIMIZE))
          {
            NtGdiOffsetRgn(Dce->hClipRgn, -Window->ClientRect.left, -Window->ClientRect.top);
          }
          else
          {
            NtGdiOffsetRgn(Dce->hClipRgn, -Window->WindowRect.left, -Window->WindowRect.top);
            NtGdiCombineRgn(Dce->hClipRgn, Dce->hClipRgn, Window->WindowRegion, RGN_AND);
            NtGdiOffsetRgn(Dce->hClipRgn, -(Window->ClientRect.left - Window->WindowRect.left), 
                                          -(Window->ClientRect.top - Window->WindowRect.top));
          }
        }
      else
        {
          Dce->hClipRgn = UnsafeIntCreateRectRgnIndirect(&Window->WindowRect);
          NtGdiOffsetRgn(Dce->hClipRgn, -Window->WindowRect.left,
             -Window->WindowRect.top);
          if(Window->WindowRegion && !(Window->Style & WS_MINIMIZE))
            NtGdiCombineRgn(Dce->hClipRgn, Dce->hClipRgn, Window->WindowRegion, RGN_AND);
        }
    }
  else if (NULL != ClipRegion)
    {
      Dce->hClipRgn = NtGdiCreateRectRgn(0, 0, 0, 0);
      if (Dce->hClipRgn)
        {
          if(!Window->WindowRegion || (Window->Style & WS_MINIMIZE))
            NtGdiCombineRgn(Dce->hClipRgn, ClipRegion, NULL, RGN_COPY);
          else
            NtGdiCombineRgn(Dce->hClipRgn, ClipRegion, Window->WindowRegion, RGN_AND);
        }
      NtGdiDeleteObject(ClipRegion);
    }

  DceSetDrawable(Window, Dce->hDC, Flags, UpdateClipOrigin);

//  if (UpdateVisRgn)
    {
      DceUpdateVisRgn(Dce, Window, Flags);
    }

  return(Dce->hDC);
}

BOOL FASTCALL
DCE_InternalDelete(PDCE Dce)
{
  PDCE PrevInList;
  
  /* FIXME - check global lock */
  
  if (Dce == FirstDce)
    {
      FirstDce = Dce->next;
      PrevInList = Dce;
    }
  else
    {
      for (PrevInList = FirstDce; NULL != PrevInList; PrevInList = PrevInList->next)
	{
	  if (Dce == PrevInList->next)
	    {
	      PrevInList->next = Dce->next;
	      break;
	    }
	}
      assert(NULL != PrevInList);
    }
  
  return NULL != PrevInList;
}

PWINDOW_OBJECT FASTCALL
IntWindowFromDC(HDC hDc)
{
  DCE *Dce;
  
  for (Dce = FirstDce; Dce != NULL; Dce = Dce->next)
  {
    if(Dce->hDC == hDc)
    {
      return Dce->CurrentWindow;
    }
  }
  return 0;
}

INT FASTCALL
IntReleaseDC(PWINDOW_OBJECT Window, HDC hDc)
{
  DCE *dce;
  INT nRet = 0;
  
  ASSERT(Window);
  
  dce = FirstDce;

  DPRINT("%p %p\n", Window->Handle, hDc);

  while (dce && (dce->hDC != hDc))
    {
      dce = dce->next;
    }

  if (dce && (dce->DCXFlags & DCX_DCEBUSY))
    {
      nRet = DceReleaseDC(dce);
    }

  return nRet;
}

/***********************************************************************
 *           DceFreeDCE
 */
PDCE FASTCALL
DceFreeDCE(PDCE dce)
{
  DCE *ret;
  HANDLE hDce;

  if (NULL == dce)
    {
      return NULL;
    }

  ret = dce->next;

#if 0 /* FIXME */
  SetDCHook(dce->hDC, NULL, 0L);
#endif

  NtGdiDeleteDC(dce->hDC);
  if (dce->hClipRgn && ! (dce->DCXFlags & DCX_KEEPCLIPRGN))
    {
      NtGdiDeleteObject(dce->hClipRgn);
    }

  hDce = dce->Handle;
  DCEOBJ_FreeDCE(hDce);

  return ret;
}
  

/***********************************************************************
 *           DceFreeWindowDCE
 *
 * Remove owned DCE and reset unreleased cache DCEs.
 */
void FASTCALL
DceFreeWindowDCE(PWINDOW_OBJECT Window)
{
  DCE *pDCE;
  
  pDCE = FirstDce;
  while (pDCE)
    {
      if (pDCE->CurrentWindow == Window)
        {
          if (pDCE == Window->Dce) /* owned or Class DCE*/
            {
              if (Window->Class->style & CS_OWNDC) /* owned DCE*/
                {
                  pDCE = DceFreeDCE(pDCE);
                  Window->Dce = NULL;
                  continue;
                }
              else if (pDCE->DCXFlags & (DCX_INTERSECTRGN | DCX_EXCLUDERGN))	/* Class DCE*/
		{
                  DceDeleteClipRgn(pDCE);
                  pDCE->CurrentWindow = NULL;
                }
            }
          else
            {
              if (pDCE->DCXFlags & DCX_DCEBUSY) /* shared cache DCE */
                {
                  /* FIXME: AFAICS we are doing the right thing here so
                   * this should be a DPRINT. But this is best left as an ERR
                   * because the 'application error' is likely to come from
                   * another part of Wine (i.e. it's our fault after all).
                   * We should change this to DPRINT when ReactOS is more stable
                   * (for 1.0?).
                   */
                  DPRINT1("[%p] GetDC() without ReleaseDC()!\n", Window->Handle);
                  DceReleaseDC(pDCE);
                }

              pDCE->DCXFlags &= DCX_CACHE;
              pDCE->DCXFlags |= DCX_DCEEMPTY;
              pDCE->CurrentWindow = NULL;
            }
        }
      pDCE = pDCE->next;
    }
}

void FASTCALL
DceEmptyCache()
{
  while (FirstDce != NULL)
    {
      DceFreeDCE(FirstDce);
    }
}

VOID FASTCALL 
DceResetActiveDCEs(PWINDOW_OBJECT Window, int DeltaX, int DeltaY)
{
  DCE *pDCE;
  PDC dc;
  PWINDOW_OBJECT CurrentWindow;

  if (NULL == Window)
    {
      return;
    }
  
  pDCE = FirstDce;
  while (pDCE)
    {
      if (0 == (pDCE->DCXFlags & DCX_DCEEMPTY))
        {
          CurrentWindow = pDCE->CurrentWindow;
          
	  dc = DC_LockDc(pDCE->hDC);
          if (dc == NULL)
            {
              pDCE = pDCE->next;
              continue;
            }
          if ((0 != DeltaX || 0 != DeltaY)
              && IntIsChildWindow(Window, CurrentWindow))
            {
              dc->w.DCOrgX += DeltaX;
              dc->w.DCOrgY += DeltaY;
              if (NULL != dc->w.hClipRgn)
                {
                  NtGdiOffsetRgn(dc->w.hClipRgn, DeltaX, DeltaY);
                }
              if (NULL != pDCE->hClipRgn)
                {
                  NtGdiOffsetRgn(pDCE->hClipRgn, DeltaX, DeltaY);
                }
            }
          DC_UnlockDc(pDCE->hDC);

          DceUpdateVisRgn(pDCE, CurrentWindow, pDCE->DCXFlags);
        }
      
      pDCE = pDCE->next;
    }
}

/* EOF */
