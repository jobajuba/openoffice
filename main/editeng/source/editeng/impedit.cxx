/**************************************************************
 * 
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 * 
 *************************************************************/



// MARKER(update_precomp.py): autogen include statement, do not remove
#include "precompiled_editeng.hxx"

#include <eeng_pch.hxx>

#include <impedit.hxx>
#include <editeng/editeng.hxx>
#include <editeng/editview.hxx>
#include <tools/poly.hxx>
#include <editeng/unolingu.hxx>
#include <com/sun/star/linguistic2/XDictionaryEntry.hpp>
#include <com/sun/star/linguistic2/DictionaryType.hpp>
#include <com/sun/star/linguistic2/DictionaryEvent.hpp>
#include <com/sun/star/linguistic2/XDictionaryEventListener.hpp>
#include <com/sun/star/linguistic2/DictionaryEventFlags.hpp>
#include <com/sun/star/linguistic2/XDictionary.hpp>
#include <com/sun/star/datatransfer/dnd/DNDConstants.hpp>
#include <com/sun/star/datatransfer/dnd/XDragGestureRecognizer.hpp>
#include <com/sun/star/datatransfer/dnd/XDropTarget.hpp>
#include <com/sun/star/datatransfer/clipboard/XClipboard.hpp>
#include <com/sun/star/datatransfer/clipboard/XFlushableClipboard.hpp>
#include <vos/mutex.hxx>
#include <editeng/flditem.hxx>
#include <svl/intitem.hxx>
#include <svtools/transfer.hxx>
#include <sot/exchange.hxx>
#include <sot/formats.hxx>

using namespace ::com::sun::star;
using namespace ::com::sun::star::uno;
using namespace ::com::sun::star::linguistic2;

#define SCRLRANGE	20		// 1/20 der Breite/Hoehe scrollen, wenn im QueryDrop

inline void lcl_AllignToPixel( Point& rPoint, OutputDevice* pOutDev, short nDiffX, short nDiffY )
{
	rPoint = pOutDev->LogicToPixel( rPoint );

	if ( nDiffX )
		rPoint.X() += nDiffX;
	if ( nDiffY )
		rPoint.Y() += nDiffY;

	rPoint = pOutDev->PixelToLogic( rPoint );
}

//	----------------------------------------------------------------------
//	class ImpEditView
//	----------------------------------------------------------------------
ImpEditView::ImpEditView( EditView* pView, EditEngine* pEng, Window* pWindow ) :
	aOutArea( Point(), pEng->GetPaperSize() )
{
    pEditView           = pView;
	pEditEngine			= pEng;
	pOutWin 			= pWindow;
	pPointer			= NULL;
	pBackgroundColor	= NULL;
	nScrollDiffX		= 0;
    nExtraCursorFlags   = 0;
    nCursorBidiLevel    = CURSOR_BIDILEVEL_DONTKNOW;
	pCursor				= NULL;
   	pDragAndDropInfo	= NULL;
	bReadOnly			= sal_False;
    bClickedInSelection = sal_False;
	eSelectionMode		= EE_SELMODE_TXTONLY;
	eAnchorMode			= ANCHOR_TOP_LEFT;
	nInvMore			= 1;
	nTravelXPos			= TRAVEL_X_DONTKNOW;
	nControl 			= EV_CNTRL_AUTOSCROLL | EV_CNTRL_ENABLEPASTE;
    bActiveDragAndDropListener = sal_False;

	aEditSelection.Min() = pEng->pImpEditEngine->GetEditDoc().GetStartPaM();
	aEditSelection.Max() = pEng->pImpEditEngine->GetEditDoc().GetEndPaM();
}

ImpEditView::~ImpEditView()
{
    RemoveDragAndDropListeners();

    if ( pOutWin && ( pOutWin->GetCursor() == pCursor ) )
		pOutWin->SetCursor( NULL );

	delete pCursor;
	delete pBackgroundColor;
	delete pPointer;
	delete pDragAndDropInfo;
}

void ImpEditView::SetBackgroundColor( const Color& rColor )
{
	delete pBackgroundColor;
	pBackgroundColor = new Color( rColor );
}

void ImpEditView::SetEditSelection( const EditSelection& rEditSelection )
{
	// #100856# set state before notification
    aEditSelection = rEditSelection;

    if ( pEditEngine->pImpEditEngine->GetNotifyHdl().IsSet() )
    {
		const EditDoc& rDoc = pEditEngine->pImpEditEngine->GetEditDoc();
		const EditPaM pmEnd = rDoc.GetEndPaM();
		EENotifyType eNotifyType;
		if (rDoc.Count() > 1 && 
			pmEnd == rEditSelection.Min() && 
			pmEnd == rEditSelection.Max())//if move cursor to the last para.
		{
			eNotifyType = EE_NOTIFY_TEXTVIEWSELECTIONCHANGED_ENDD_PARA;
		}
		else 
		{
			eNotifyType = EE_NOTIFY_TEXTVIEWSELECTIONCHANGED;
		}
        //EENotify aNotify( EE_NOTIFY_TEXTVIEWSELECTIONCHANGED );
        EENotify aNotify( eNotifyType );
        aNotify.pEditEngine = pEditEngine;
        aNotify.pEditView = GetEditViewPtr();
        pEditEngine->pImpEditEngine->CallNotify( aNotify );
    }
}


void ImpEditView::DrawSelection( EditSelection aTmpSel, Region* pRegion, OutputDevice* pTargetDevice )
{
    if ( GetSelectionMode() == EE_SELMODE_HIDDEN )
        return;

	// Vor dem Zeichnen der Selektion muss sichergestellt werden,
	// das der Fensterinhalt komplett gueltig ist!
	// Muss hier stehen, damit auf jeden Fall weg wenn lerr, nicht spaeter
	// zwei Paint-Events!
	// 19.10: Muss sogar vor Abfrage von bUpdate, falls nach Invalidate
	// noch Paints in der Queue, aber jemand schaltet den UpdateMode um!

	// pRegion: Wenn nicht NULL, dann nur Region berechnen.
	PolyPolygon* pPolyPoly = NULL;
	if ( pRegion )
		pPolyPoly = new PolyPolygon;

	OutputDevice* pTarget = pTargetDevice ? pTargetDevice : pOutWin;
	sal_Bool bClipRegion = pTarget->IsClipRegion();
	Region aOldRegion = pTarget->GetClipRegion();

	if ( !pRegion )
	{
		if ( pEditEngine->pImpEditEngine->GetUpdateMode() == sal_False )
			return;
		if ( pEditEngine->pImpEditEngine->IsInUndo() )
			return;

		if ( !aTmpSel.HasRange() )
			return;

	    // aTmpOutArea: Falls OutputArea > Papierbreite und
	    // Text > Papierbreite ( uebergrosse Felder )
	    Rectangle aTmpOutArea( aOutArea );
	    if ( aTmpOutArea.GetWidth() > pEditEngine->pImpEditEngine->GetPaperSize().Width() )
		    aTmpOutArea.Right() = aTmpOutArea.Left() + pEditEngine->pImpEditEngine->GetPaperSize().Width();
		pTarget->IntersectClipRegion( aTmpOutArea );

		if ( pOutWin->GetCursor() )
			pOutWin->GetCursor()->Hide();
	}

	DBG_ASSERT( !pEditEngine->pImpEditEngine->aIdleFormatter.IsActive(), "DrawSelection: Not formatted!" );
	aTmpSel.Adjust( pEditEngine->pImpEditEngine->GetEditDoc() );

	ContentNode* pStartNode = aTmpSel.Min().GetNode();
	ContentNode* pEndNode = aTmpSel.Max().GetNode();
	sal_uInt32 nStartPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( pStartNode );
	sal_uInt32 nEndPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( pEndNode );
	// ueber die Absaetze iterieren....
	for ( sal_uInt32 nPara = nStartPara; nPara <= nEndPara; nPara++ )
	{
		ParaPortion* pTmpPortion = pEditEngine->pImpEditEngine->GetParaPortions().SaveGetObject( nPara );
		DBG_ASSERT( pTmpPortion, "Portion in Selektion nicht gefunden!" );
		DBG_ASSERT( !pTmpPortion->IsInvalid(), "Portion in Selektion nicht formatiert!" );

		if ( !pTmpPortion->IsVisible() || pTmpPortion->IsInvalid() )
			continue;

		long nParaStart = pEditEngine->pImpEditEngine->GetParaPortions().GetYOffset( pTmpPortion );
		if ( ( nParaStart + pTmpPortion->GetHeight() ) < GetVisDocTop() )
			continue;
		if ( nParaStart > GetVisDocBottom() )
			break;

		sal_uInt16 nStartLine = 0;
		sal_uInt16 nEndLine = pTmpPortion->GetLines().Count() -1;
		if ( nPara == nStartPara )
			nStartLine = pTmpPortion->GetLines().FindLine( aTmpSel.Min().GetIndex(), sal_False );
		if ( nPara == nEndPara )
			nEndLine = pTmpPortion->GetLines().FindLine( aTmpSel.Max().GetIndex(), sal_True );

		// ueber die Zeilen iterieren....
		for ( sal_uInt16 nLine = nStartLine; nLine <= nEndLine; nLine++ )
		{
			EditLine* pLine = pTmpPortion->GetLines().GetObject( nLine );
			DBG_ASSERT( pLine, "Zeile nicht gefunden: DrawSelection()" );

            sal_Bool bPartOfLine = sal_False;
			sal_uInt16 nStartIndex = pLine->GetStart();
			sal_uInt16 nEndIndex = pLine->GetEnd();
			if ( ( nPara == nStartPara ) && ( nLine == nStartLine ) && ( nStartIndex != aTmpSel.Min().GetIndex() ) )
            {
				nStartIndex = aTmpSel.Min().GetIndex();
                bPartOfLine = sal_True;
            }
			if ( ( nPara == nEndPara ) && ( nLine == nEndLine ) && ( nEndIndex != aTmpSel.Max().GetIndex() ) )
            {
				nEndIndex = aTmpSel.Max().GetIndex();
                bPartOfLine = sal_True;
            }

			// Kann passieren, wenn am Anfang einer umgebrochenen Zeile.
			if ( nEndIndex < nStartIndex )
				nEndIndex = nStartIndex;

			Rectangle aTmpRec( pEditEngine->pImpEditEngine->GetEditCursor( pTmpPortion, nStartIndex ) );
			Point aTopLeft( aTmpRec.TopLeft() );
			Point aBottomRight( aTmpRec.BottomRight() );

            aTopLeft.Y() += nParaStart;
			aBottomRight.Y() += nParaStart;

            // Nur Painten, wenn im sichtbaren Bereich...
			if ( aTopLeft.Y() > GetVisDocBottom() )
				break;

            if ( aBottomRight.Y() < GetVisDocTop() )
                continue;

            // Now that we have Bidi, the first/last index doesn't have to be the 'most outside' position
            if ( !bPartOfLine )
            {
                Range aLineXPosStartEnd = pEditEngine->pImpEditEngine->GetLineXPosStartEnd( pTmpPortion, pLine );
                aTopLeft.X() = aLineXPosStartEnd.Min();
                aBottomRight.X() = aLineXPosStartEnd.Max();
                ImplDrawHighlightRect( pTarget, aTopLeft, aBottomRight, pPolyPoly );
            }
            else
            {
                sal_uInt16 nTmpStartIndex = nStartIndex;
                sal_uInt16 nWritingDirStart, nTmpEndIndex;

                while ( nTmpStartIndex < nEndIndex )
                {
                    pEditEngine->pImpEditEngine->GetRightToLeft( nPara, nTmpStartIndex+1, &nWritingDirStart, &nTmpEndIndex );
                    if ( nTmpEndIndex > nEndIndex )
                        nTmpEndIndex = nEndIndex;

                    DBG_ASSERT( nTmpEndIndex > nTmpStartIndex, "DrawSelection, Start >= End?" );

                    long nX1 = pEditEngine->pImpEditEngine->GetXPos( pTmpPortion, pLine, nTmpStartIndex, sal_True );
                    long nX2 = pEditEngine->pImpEditEngine->GetXPos( pTmpPortion, pLine, nTmpEndIndex );

                    Point aPt1( Min( nX1, nX2 ), aTopLeft.Y() );
                    Point aPt2( Max( nX1, nX2 ), aBottomRight.Y() );

                    ImplDrawHighlightRect( pTarget, aPt1, aPt2, pPolyPoly );

                    nTmpStartIndex = nTmpEndIndex;
                }
            }

		}
	}

	if ( pRegion )
	{
		*pRegion = Region( *pPolyPoly );
		delete pPolyPoly;
	}
	else
	{
		if ( pOutWin->GetCursor() )
			pOutWin->GetCursor()->Show();

		if ( bClipRegion )
			pTarget->SetClipRegion( aOldRegion );
		else
			pTarget->SetClipRegion();
	}
}

void ImpEditView::ImplDrawHighlightRect( OutputDevice* _pTarget, const Point& rDocPosTopLeft, const Point& rDocPosBottomRight, PolyPolygon* pPolyPoly )
{
	if ( rDocPosTopLeft.X() != rDocPosBottomRight.X() )
	{
	    sal_Bool bPixelMode = _pTarget->GetMapMode() == MAP_PIXEL;

        Point aPnt1( GetWindowPos( rDocPosTopLeft ) );
		Point aPnt2( GetWindowPos( rDocPosBottomRight ) );

		if ( !IsVertical() )
		{
			lcl_AllignToPixel( aPnt1, _pTarget, +1, 0 );
			lcl_AllignToPixel( aPnt2, _pTarget, 0, ( bPixelMode ? 0 : -1 ) );
		}
		else
		{
			lcl_AllignToPixel( aPnt1, _pTarget, 0, +1 );
			lcl_AllignToPixel( aPnt2, _pTarget, ( bPixelMode ? 0 : +1 ), 0 );
		}

		Rectangle aRect( aPnt1, aPnt2 );
		if ( pPolyPoly )
		{
			Polygon aTmpPoly( 4 );
			aTmpPoly[0] = aRect.TopLeft();
			aTmpPoly[1] = aRect.TopRight();
			aTmpPoly[2] = aRect.BottomRight();
			aTmpPoly[3] = aRect.BottomLeft();
			pPolyPoly->Insert( aTmpPoly );
		}
		else
		{
			Window* pWindow = dynamic_cast< Window* >(_pTarget);

			if(pWindow)
			{
				pWindow->Invert( aRect );
			}
			else
			{
				_pTarget->Push(PUSH_LINECOLOR|PUSH_FILLCOLOR|PUSH_RASTEROP);
				_pTarget->SetLineColor();
				_pTarget->SetFillColor(COL_BLACK);
				_pTarget->SetRasterOp(ROP_INVERT);
				_pTarget->DrawRect(aRect);
				_pTarget->Pop();
			}
		}
	}
}


sal_Bool ImpEditView::IsVertical() const
{
	return pEditEngine->pImpEditEngine->IsVertical();
}

Rectangle ImpEditView::GetVisDocArea() const
{
	return Rectangle( GetVisDocLeft(), GetVisDocTop(), GetVisDocRight(), GetVisDocBottom() );
}

Point ImpEditView::GetDocPos( const Point& rWindowPos ) const
{
	// Fensterposition => Dokumentposition
	Point aPoint;

	if ( !pEditEngine->pImpEditEngine->IsVertical() )
	{
		aPoint.X() = rWindowPos.X() - aOutArea.Left() + GetVisDocLeft();
		aPoint.Y() = rWindowPos.Y() - aOutArea.Top() + GetVisDocTop();
	}
	else
	{
		aPoint.X() = rWindowPos.Y() - aOutArea.Top() + GetVisDocLeft();
		aPoint.Y() = aOutArea.Right() - rWindowPos.X() + GetVisDocTop();
	}

	return aPoint;
}

Point ImpEditView::GetWindowPos( const Point& rDocPos ) const
{
	// Dokumentposition => Fensterposition
	Point aPoint;

	if ( !pEditEngine->pImpEditEngine->IsVertical() )
	{
		aPoint.X() = rDocPos.X() + aOutArea.Left() - GetVisDocLeft();
		aPoint.Y() = rDocPos.Y() + aOutArea.Top() - GetVisDocTop();
	}
	else
	{
		aPoint.X() = aOutArea.Right() - rDocPos.Y() + GetVisDocTop();
		aPoint.Y() = rDocPos.X() + aOutArea.Top() - GetVisDocLeft();
	}

	return aPoint;
}

Rectangle ImpEditView::GetWindowPos( const Rectangle& rDocRect ) const
{
	// Dokumentposition => Fensterposition
	Point aPos( GetWindowPos( rDocRect.TopLeft() ) );
	Size aSz = rDocRect.GetSize();
	Rectangle aRect;
	if ( !pEditEngine->pImpEditEngine->IsVertical() )
	{
		aRect = Rectangle( aPos, aSz );
	}
	else
	{
		Point aNewPos( aPos.X()-aSz.Height(), aPos.Y() );
		aRect = Rectangle( aNewPos, Size( aSz.Height(), aSz.Width() ) );
	}
	return aRect;
}


Region* ImpEditView::CalcSelectedRegion()
{
	Region* pRegion = new Region;
	DrawSelection( GetEditSelection(), pRegion );
	return pRegion;
}

void ImpEditView::SetSelectionMode( EESelectionMode	eNewMode )
{
	if ( eSelectionMode != eNewMode )
	{
		DrawSelection();	// 'Wegmalen' ...
		eSelectionMode = eNewMode;
		DrawSelection();	// und neu zeichnen.
	}
}

void ImpEditView::SetOutputArea( const Rectangle& rRec )
{
	// sollte besser auf Pixel allignt sein!
	Rectangle aNewRec( pOutWin->LogicToPixel( rRec ) );
	aNewRec = pOutWin->PixelToLogic( aNewRec );
	aOutArea = aNewRec;
	if ( aOutArea.Right() < aOutArea.Left() )
		aOutArea.Right() = aOutArea.Left();
	if ( aOutArea.Bottom() < aOutArea.Top() )
		aOutArea.Bottom() = aOutArea.Top();

	if ( DoBigScroll() )
		SetScrollDiffX( (sal_uInt16)aOutArea.GetWidth() * 3 / 10 );
	else
		SetScrollDiffX( (sal_uInt16)aOutArea.GetWidth() * 2 / 10 );
}

void ImpEditView::ResetOutputArea( const Rectangle& rRec )
{
    // remember old out area
    const Rectangle aOldArea(aOutArea);

    // apply new one
    SetOutputArea(rRec);

    // invalidate surrounding areas if update is true
    if(!aOldArea.IsEmpty() && pEditEngine->pImpEditEngine->GetUpdateMode())
    {
        // #119885# use grown area if needed; do when getting bigger OR smaller
        const sal_Int32 nMore(DoInvalidateMore() ? GetWindow()->PixelToLogic(Size(nInvMore, 0)).Width() : 0);

        if(aOldArea.Left() > aOutArea.Left())
        {
            GetWindow()->Invalidate(Rectangle(aOutArea.Left() - nMore, aOldArea.Top() - nMore, aOldArea.Left(), aOldArea.Bottom() + nMore));
        }
        else if(aOldArea.Left() < aOutArea.Left())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Left() - nMore, aOldArea.Top() - nMore, aOutArea.Left(), aOldArea.Bottom() + nMore));
        }

        if(aOldArea.Right() > aOutArea.Right())
        {
            GetWindow()->Invalidate(Rectangle(aOutArea.Right(), aOldArea.Top() - nMore, aOldArea.Right() + nMore, aOldArea.Bottom() + nMore));
        }
        else if(aOldArea.Right() < aOutArea.Right())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Right(), aOldArea.Top() - nMore, aOutArea.Right() + nMore, aOldArea.Bottom() + nMore));
        }

        if(aOldArea.Top() > aOutArea.Top())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Left() - nMore, aOutArea.Top() - nMore, aOldArea.Right() + nMore, aOldArea.Top()));
        }
        else if(aOldArea.Top() < aOutArea.Top())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Left() - nMore, aOldArea.Top() - nMore, aOldArea.Right() + nMore, aOutArea.Top()));
        }

        if(aOldArea.Bottom() > aOutArea.Bottom())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Left() - nMore, aOutArea.Bottom(), aOldArea.Right() + nMore, aOldArea.Bottom() + nMore));
        }
        else if(aOldArea.Bottom() < aOutArea.Bottom())
        {
            GetWindow()->Invalidate(Rectangle(aOldArea.Left() - nMore, aOldArea.Bottom(), aOldArea.Right() + nMore, aOutArea.Bottom() + nMore));
        }
    }
}

void ImpEditView::RecalcOutputArea()
{
	Rectangle aOldArea( aOutArea );
	Point aNewTopLeft( aOutArea.TopLeft() );
	Size aNewSz( aOutArea.GetSize() );

	// X:
	if ( DoAutoWidth() )
	{
		if ( pEditEngine->pImpEditEngine->GetStatus().AutoPageWidth() )
			aNewSz.Width() = pEditEngine->pImpEditEngine->GetPaperSize().Width();
		switch ( eAnchorMode )
		{
			case ANCHOR_TOP_LEFT:
			case ANCHOR_VCENTER_LEFT:
			case ANCHOR_BOTTOM_LEFT:
			{
				aNewTopLeft.X() = aAnchorPoint.X();
			}
			break;
			case ANCHOR_TOP_HCENTER:
			case ANCHOR_VCENTER_HCENTER:
			case ANCHOR_BOTTOM_HCENTER:
			{
				aNewTopLeft.X() = aAnchorPoint.X() - aNewSz.Width() / 2;
			}
			break;
			case ANCHOR_TOP_RIGHT:
			case ANCHOR_VCENTER_RIGHT:
			case ANCHOR_BOTTOM_RIGHT:
			{
				aNewTopLeft.X() = aAnchorPoint.X() - aNewSz.Width() - 1;
			}
			break;
		}
	}

	// Y:
	if ( DoAutoHeight() )
	{
		if ( pEditEngine->pImpEditEngine->GetStatus().AutoPageHeight() )
			aNewSz.Height() = pEditEngine->pImpEditEngine->GetPaperSize().Height();
		switch ( eAnchorMode )
		{
			case ANCHOR_TOP_LEFT:
			case ANCHOR_TOP_HCENTER:
			case ANCHOR_TOP_RIGHT:
			{
				aNewTopLeft.Y() = aAnchorPoint.Y();
			}
			break;
			case ANCHOR_VCENTER_LEFT:
			case ANCHOR_VCENTER_HCENTER:
			case ANCHOR_VCENTER_RIGHT:
			{
				aNewTopLeft.Y() = aAnchorPoint.Y() - aNewSz.Height() / 2;
			}
			break;
			case ANCHOR_BOTTOM_LEFT:
			case ANCHOR_BOTTOM_HCENTER:
			case ANCHOR_BOTTOM_RIGHT:
			{
				aNewTopLeft.Y() = aAnchorPoint.Y() - aNewSz.Height() - 1;
			}
			break;
		}
	}
	ResetOutputArea( Rectangle( aNewTopLeft, aNewSz ) );
}

void ImpEditView::SetAnchorMode( EVAnchorMode eMode )
{
	eAnchorMode = eMode;
	CalcAnchorPoint();
}

void ImpEditView::CalcAnchorPoint()
{
	// GetHeight() und GetWidth() -1, da Rectangle-Berechnung nicht erwuenscht.

	// X:
	switch ( eAnchorMode )
	{
		case ANCHOR_TOP_LEFT:
		case ANCHOR_VCENTER_LEFT:
		case ANCHOR_BOTTOM_LEFT:
		{
			aAnchorPoint.X() = aOutArea.Left();
		}
		break;
		case ANCHOR_TOP_HCENTER:
		case ANCHOR_VCENTER_HCENTER:
		case ANCHOR_BOTTOM_HCENTER:
		{
			aAnchorPoint.X() = aOutArea.Left() + (aOutArea.GetWidth()-1) / 2;
		}
		break;
		case ANCHOR_TOP_RIGHT:
		case ANCHOR_VCENTER_RIGHT:
		case ANCHOR_BOTTOM_RIGHT:
		{
			aAnchorPoint.X() = aOutArea.Right();
		}
		break;
	}

	// Y:
	switch ( eAnchorMode )
	{
		case ANCHOR_TOP_LEFT:
		case ANCHOR_TOP_HCENTER:
		case ANCHOR_TOP_RIGHT:
		{
			aAnchorPoint.Y() = aOutArea.Top();
		}
		break;
		case ANCHOR_VCENTER_LEFT:
		case ANCHOR_VCENTER_HCENTER:
		case ANCHOR_VCENTER_RIGHT:
		{
			aAnchorPoint.Y() = aOutArea.Top() + (aOutArea.GetHeight()-1) / 2;
		}
		break;
		case ANCHOR_BOTTOM_LEFT:
		case ANCHOR_BOTTOM_HCENTER:
		case ANCHOR_BOTTOM_RIGHT:
		{
			aAnchorPoint.Y() = aOutArea.Bottom() - 1;
		}
		break;
	}
}

void ImpEditView::ShowCursor( sal_Bool bGotoCursor, sal_Bool bForceVisCursor, sal_uInt16 nShowCursorFlags )
{
	// Kein ShowCursor bei einer leeren View...
	if ( ( aOutArea.Left() >= aOutArea.Right() ) && ( aOutArea.Top() >= aOutArea.Bottom() ) )
		return;

	pEditEngine->pImpEditEngine->CheckIdleFormatter();
	if ( !pEditEngine->pImpEditEngine->IsFormatted() )
		pEditEngine->pImpEditEngine->FormatDoc();

	// Aus irgendwelchen Gruenden lande ich waehrend der Formatierung hier,
	// wenn sich der Outiner im Paint initialisiert, weil kein SetPool();
	if ( pEditEngine->pImpEditEngine->IsFormatting() )
		return;
	if ( pEditEngine->pImpEditEngine->GetUpdateMode() == sal_False )
		return;
	if ( pEditEngine->pImpEditEngine->IsInUndo() )
		return;

	if ( pOutWin->GetCursor() != GetCursor() )
		pOutWin->SetCursor( GetCursor() );

	EditPaM aPaM( aEditSelection.Max() );

    sal_uInt16 nTextPortionStart = 0;
    sal_uInt32 nPara = pEditEngine->pImpEditEngine->aEditDoc.GetPos( aPaM.GetNode() );
    if (nPara == EE_PARA_MAX) // #i94322
        return;
    ParaPortion* pParaPortion = pEditEngine->pImpEditEngine->GetParaPortions().GetObject( nPara );

    nShowCursorFlags |= nExtraCursorFlags;

    nShowCursorFlags |= GETCRSR_TXTONLY;

    // Use CursorBidiLevel 0/1 in meaning of
    // 0: prefer portion end, normal mode
    // 1: prefer portion start

    if ( ( GetCursorBidiLevel() != CURSOR_BIDILEVEL_DONTKNOW ) && GetCursorBidiLevel() )
    {
        nShowCursorFlags |= GETCRSR_PREFERPORTIONSTART;
    }

    Rectangle aEditCursor = pEditEngine->pImpEditEngine->PaMtoEditCursor( aPaM, nShowCursorFlags );
	if ( !IsInsertMode() && !aEditSelection.HasRange() )
	{
		if ( aPaM.GetNode()->Len() && ( aPaM.GetIndex() < aPaM.GetNode()->Len() ) )
		{
            // If we are behind a portion, and the next portion has other direction, we must change position...
			aEditCursor.Left() = aEditCursor.Right() = pEditEngine->pImpEditEngine->PaMtoEditCursor( aPaM, GETCRSR_TXTONLY|GETCRSR_PREFERPORTIONSTART ).Left();

            sal_uInt16 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nTextPortionStart, sal_True );
            TextPortion* pTextPortion = pParaPortion->GetTextPortions().GetObject( nTextPortion );
            if ( pTextPortion->GetKind() == PORTIONKIND_TAB )
            {
		        aEditCursor.Right() += pTextPortion->GetSize().Width();
            }
            else
            {
                EditPaM aNext = pEditEngine->pImpEditEngine->CursorRight( aPaM, (sal_uInt16)i18n::CharacterIteratorMode::SKIPCELL );
			    Rectangle aTmpRect = pEditEngine->pImpEditEngine->PaMtoEditCursor( aNext, GETCRSR_TXTONLY );
                if ( aTmpRect.Top() != aEditCursor.Top() )
                    aTmpRect = pEditEngine->pImpEditEngine->PaMtoEditCursor( aNext, GETCRSR_TXTONLY|GETCRSR_ENDOFLINE );
			    aEditCursor.Right() = aTmpRect.Left();
            }
		}
	}
	long nMaxHeight = !IsVertical() ? aOutArea.GetHeight() : aOutArea.GetWidth();
	if ( aEditCursor.GetHeight() > nMaxHeight )
	{
		aEditCursor.Bottom() = aEditCursor.Top() + nMaxHeight - 1;
	}
	if ( bGotoCursor  ) // && (!pEditEngine->pImpEditEngine->GetStatus().AutoPageSize() ) )
	{
		// pruefen, ob scrollen notwendig...
		// wenn scrollen, dann Update() und Scroll() !
		long nDocDiffX = 0;
		long nDocDiffY = 0;

		Rectangle aTmpVisArea( GetVisDocArea() );
		// aTmpOutArea: Falls OutputArea > Papierbreite und
		// Text > Papierbreite ( uebergrosse Felder )
		long nMaxTextWidth = !IsVertical() ? pEditEngine->pImpEditEngine->GetPaperSize().Width() : pEditEngine->pImpEditEngine->GetPaperSize().Height();
		if ( aTmpVisArea.GetWidth() > nMaxTextWidth )
			aTmpVisArea.Right() = aTmpVisArea.Left() + nMaxTextWidth;

		if ( aEditCursor.Bottom() > aTmpVisArea.Bottom() )
		{   // hochscrollen, hier positiv
			nDocDiffY = aEditCursor.Bottom() - aTmpVisArea.Bottom();
		}
		else if ( aEditCursor.Top() < aTmpVisArea.Top() )
		{	// runterscrollen, negativ
			nDocDiffY = aEditCursor.Top() - aTmpVisArea.Top();
		}

		if ( aEditCursor.Right() > aTmpVisArea.Right() )
		{
			// linksscrollen, positiv
			nDocDiffX = aEditCursor.Right() - aTmpVisArea.Right();
			// Darfs ein bischen mehr sein?
			if ( aEditCursor.Right() < ( nMaxTextWidth - GetScrollDiffX() ) )
				nDocDiffX += GetScrollDiffX();
			else
			{
				long n = nMaxTextWidth - aEditCursor.Right();
				// Bei einem MapMode != RefMapMode kann der EditCursor auch mal ueber
				// die Papierbreite Wandern!
				nDocDiffX += ( n > 0 ? n : -n );
			}
		}
		else if ( aEditCursor.Left() < aTmpVisArea.Left() )
		{	// rechtsscrollen
			// negativ:
			nDocDiffX = aEditCursor.Left() - aTmpVisArea.Left();
			// Darfs ein bischen mehr sein?
			if ( aEditCursor.Left() > ( - (long)GetScrollDiffX() ) )
				nDocDiffX -= GetScrollDiffX();
			else
				nDocDiffX -= aEditCursor.Left();
		}
		if ( aPaM.GetIndex() == 0 ) 	// braucht Olli fuer den Outliner
		{
			// Aber sicherstellen, dass dadurch der Cursor nicht den
			// sichtbaren bereich verlaesst!
			if ( aEditCursor.Left() < aTmpVisArea.GetWidth() )
			{
				nDocDiffX = -aTmpVisArea.Left();
			}
		}

		if ( nDocDiffX | nDocDiffY )
		{
			long nDiffX = !IsVertical() ? nDocDiffX : -nDocDiffY;
			long nDiffY = !IsVertical() ? nDocDiffY : nDocDiffX;

			// Negativ: Zum Anfang bzw. linken Rand
			if ( ( Abs( nDiffY ) > pEditEngine->pImpEditEngine->nOnePixelInRef ) && DoBigScroll() )
			{
				long nH = aOutArea.GetHeight() / 4;
				if ( ( nH > aEditCursor.GetHeight() ) && ( Abs( nDiffY ) < nH ) )
				{
					if ( nDiffY < 0 )
						nDiffY -= nH;
					else
						nDiffY += nH;
				}
			}

			if ( ( Abs( nDiffX ) > pEditEngine->pImpEditEngine->nOnePixelInRef ) && DoBigScroll() )
			{
				long nW = aOutArea.GetWidth() / 4;
				if ( Abs( nDiffX ) < nW )
				{
					if ( nDiffY < 0 )
						nDiffY -= nW;
					else
						nDiffY += nW;
				}
			}

			if ( nDiffX )
				pEditEngine->pImpEditEngine->aStatus.GetStatusWord() = pEditEngine->pImpEditEngine->aStatus.GetStatusWord() | EE_STAT_HSCROLL;
			if ( nDiffY )
				pEditEngine->pImpEditEngine->aStatus.GetStatusWord() = pEditEngine->pImpEditEngine->aStatus.GetStatusWord() | EE_STAT_VSCROLL;
			Scroll( -nDiffX, -nDiffY );
			pEditEngine->pImpEditEngine->DelayedCallStatusHdl();
		}
	}

	// Cursor evtl. etwas stutzen...
	if ( ( aEditCursor.Bottom() > GetVisDocTop() ) &&
		 ( aEditCursor.Top() < GetVisDocBottom() ) )
	{
		if ( aEditCursor.Bottom() > GetVisDocBottom() )
			aEditCursor.Bottom() = GetVisDocBottom();
		if ( aEditCursor.Top() < GetVisDocTop() )
			aEditCursor.Top() = GetVisDocTop();
	}

	long nOnePixel = pOutWin->PixelToLogic( Size( 1, 0 ) ).Width();

	if ( /* pEditEngine->pImpEditEngine->GetStatus().AutoPageSize() || */
		 ( ( aEditCursor.Top() + nOnePixel >= GetVisDocTop() ) &&
		 ( aEditCursor.Bottom() - nOnePixel <= GetVisDocBottom() ) &&
		 ( aEditCursor.Left() + nOnePixel >= GetVisDocLeft() ) &&
		 ( aEditCursor.Right() - nOnePixel <= GetVisDocRight() ) ) )
	{
		Rectangle aCursorRect = GetWindowPos( aEditCursor );
		GetCursor()->SetPos( aCursorRect.TopLeft() );
		Size aCursorSz( aCursorRect.GetSize() );
		// Rectangle is inclusive
		aCursorSz.Width()--;
		aCursorSz.Height()--;
		if ( !aCursorSz.Width() || !aCursorSz.Height() )
		{
			long nCursorSz = pOutWin->GetSettings().GetStyleSettings().GetCursorSize();
			nCursorSz = pOutWin->PixelToLogic( Size( nCursorSz, 0 ) ).Width();
			if ( !aCursorSz.Width() )
				aCursorSz.Width() = nCursorSz;
			if ( !aCursorSz.Height() )
				aCursorSz.Height() = nCursorSz;
		}
		// #111036# Let VCL do orientation for cursor, otherwise problem when cursor has direction flag
		if ( IsVertical() )
		{
			Size aOldSz( aCursorSz );
			aCursorSz.Width() = aOldSz.Height();
			aCursorSz.Height() = aOldSz.Width();
			GetCursor()->SetPos( aCursorRect.TopRight() );
			GetCursor()->SetOrientation( 2700 );
		}
        else
            // --> FME 2004-10-18 #i32593#
            // Reset correct orientation in horizontal layout
            GetCursor()->SetOrientation( 0 );
            // <--

		GetCursor()->SetSize( aCursorSz );

        unsigned char nCursorDir = CURSOR_DIRECTION_NONE;
        if ( IsInsertMode() && !aEditSelection.HasRange() && ( pEditEngine->pImpEditEngine->HasDifferentRTLLevels( aPaM.GetNode() ) ) )
        {
            sal_uInt16 nTextPortion = pParaPortion->GetTextPortions().FindPortion( aPaM.GetIndex(), nTextPortionStart, nShowCursorFlags & GETCRSR_PREFERPORTIONSTART ? sal_True : sal_False );
            TextPortion* pTextPortion = pParaPortion->GetTextPortions().GetObject( nTextPortion );
            sal_uInt16 nRTLLevel = pTextPortion->GetRightToLeft();
            if ( nRTLLevel%2 )
                nCursorDir = CURSOR_DIRECTION_RTL;
            else
                nCursorDir = CURSOR_DIRECTION_LTR;

        }
        GetCursor()->SetDirection( nCursorDir );

        if ( bForceVisCursor )
			GetCursor()->Show();

        // #102936# Call SetInputContext every time, otherwise we may have the wrong font
		// if ( !pEditEngine->pImpEditEngine->mpIMEInfos )
		{
			SvxFont aFont;
			pEditEngine->pImpEditEngine->SeekCursor( aPaM.GetNode(), aPaM.GetIndex()+1, aFont );
			sal_uLong nContextFlags = INPUTCONTEXT_TEXT|INPUTCONTEXT_EXTTEXTINPUT;
			GetWindow()->SetInputContext( InputContext( aFont, nContextFlags ) );
		}
	}
	else
	{
		pEditEngine->pImpEditEngine->GetStatus().GetStatusWord() = pEditEngine->pImpEditEngine->GetStatus().GetStatusWord() | EE_STAT_CURSOROUT;
		GetCursor()->Hide();
		GetCursor()->SetPos( Point( -1, -1 ) );
		GetCursor()->SetSize( Size( 0, 0 ) );
	}
}

Pair ImpEditView::Scroll( long ndX, long ndY, sal_uInt8 nRangeCheck )
{
	DBG_ASSERT( pEditEngine->pImpEditEngine->IsFormatted(), "Scroll: Nicht formatiert!" );
	if ( !ndX && !ndY )
		return Range( 0, 0 );

#ifdef DBG_UTIL
	Rectangle aR( aOutArea );
	aR = pOutWin->LogicToPixel( aR );
	aR = pOutWin->PixelToLogic( aR );
	DBG_ASSERTWARNING( aR == aOutArea, "OutArea vor Scroll nicht aligned" );
#endif

	Rectangle aNewVisArea( GetVisDocArea() );
	Size aPaperSz( pEditEngine->pImpEditEngine->GetPaperSize() );

	// Vertical:
	if ( !IsVertical() )
	{
		aNewVisArea.Top() -= ndY;
		aNewVisArea.Bottom() -= ndY;
	}
	else
	{
		aNewVisArea.Top() += ndX;
		aNewVisArea.Bottom() += ndX;
	}
	if ( ( nRangeCheck == RGCHK_PAPERSZ1 ) && ( aNewVisArea.Bottom() > (long)pEditEngine->pImpEditEngine->GetTextHeight() ) )
	{
		// GetTextHeight noch optimieren!
		long nDiff = pEditEngine->pImpEditEngine->GetTextHeight() - aNewVisArea.Bottom(); // negativ
		aNewVisArea.Move( 0, nDiff );	// koennte im neg. Bereich landen...
	}
	if ( ( aNewVisArea.Top() < 0 ) && ( nRangeCheck != RGCHK_NONE ) )
		aNewVisArea.Move( 0, -aNewVisArea.Top() );

	// Horizontal:
	if ( !IsVertical() )
	{
		aNewVisArea.Left() -= ndX;
		aNewVisArea.Right() -= ndX;
	}
	else
	{
		aNewVisArea.Left() -= ndY;
		aNewVisArea.Right() -= ndY;
	}
	if ( ( nRangeCheck == RGCHK_PAPERSZ1 ) && ( aNewVisArea.Right() > (long)pEditEngine->pImpEditEngine->CalcTextWidth( sal_False ) ) )
	{
		long nDiff = pEditEngine->pImpEditEngine->CalcTextWidth( sal_False ) - aNewVisArea.Right(); 	// negativ
		aNewVisArea.Move( nDiff, 0 );	// koennte im neg. Bereich landen...
	}
	if ( ( aNewVisArea.Left() < 0 ) && ( nRangeCheck != RGCHK_NONE ) )
		aNewVisArea.Move( -aNewVisArea.Left(), 0 );

	// Die Differenz muss auf Pixel alignt sein (wegen Scroll!)
	long nDiffX = !IsVertical() ? ( GetVisDocLeft() - aNewVisArea.Left() ) : -( GetVisDocTop() - aNewVisArea.Top() );
	long nDiffY = !IsVertical() ? ( GetVisDocTop() - aNewVisArea.Top() ) : ( GetVisDocLeft() - aNewVisArea.Left() );

	Size aDiffs( nDiffX, nDiffY );
	aDiffs = pOutWin->LogicToPixel( aDiffs );
	aDiffs = pOutWin->PixelToLogic( aDiffs );

	long nRealDiffX = aDiffs.Width();
	long nRealDiffY = aDiffs.Height();


	if ( nRealDiffX || nRealDiffY )
	{
		Cursor* pCrsr = GetCursor();
		sal_Bool bVisCursor = pCrsr->IsVisible();
		pCrsr->Hide();
		pOutWin->Update();
		if ( !IsVertical() )
			aVisDocStartPos.Move( -nRealDiffX, -nRealDiffY );
		else
			aVisDocStartPos.Move( -nRealDiffY, nRealDiffX );
		// Das Move um den allignten Wert ergibt nicht unbedingt ein
		// alligntes Rechteck...
		// MT 11/00: Align VisArea???
		aVisDocStartPos = pOutWin->LogicToPixel( aVisDocStartPos );
		aVisDocStartPos = pOutWin->PixelToLogic( aVisDocStartPos );
		Rectangle aRec( aOutArea );
		pOutWin->Scroll( nRealDiffX, nRealDiffY, aRec, sal_True );
		pOutWin->Update();
		pCrsr->SetPos( pCrsr->GetPos() + Point( nRealDiffX, nRealDiffY ) );
		if ( bVisCursor )
		{
			Rectangle aCursorRec( pCrsr->GetPos(), pCrsr->GetSize() );
			if ( aOutArea.IsInside( aCursorRec ) )
				pCrsr->Show();
		}

        if ( pEditEngine->pImpEditEngine->GetNotifyHdl().IsSet() )
        {
            EENotify aNotify( EE_NOTIFY_TEXTVIEWSCROLLED );
            aNotify.pEditEngine = pEditEngine;
            aNotify.pEditView = GetEditViewPtr();
            pEditEngine->pImpEditEngine->CallNotify( aNotify );
        }
	}

	return Pair( nRealDiffX, nRealDiffY );
}

sal_Bool ImpEditView::PostKeyEvent( const KeyEvent& rKeyEvent )
{
    sal_Bool bDone = sal_False;

	KeyFuncType eFunc = rKeyEvent.GetKeyCode().GetFunction();
	if ( eFunc != KEYFUNC_DONTKNOW )
	{
		switch ( eFunc )
		{
			case KEYFUNC_CUT:
			{
				if ( !bReadOnly )
				{
                    Reference<com::sun::star::datatransfer::clipboard::XClipboard> aClipBoard(GetWindow()->GetClipboard());
					CutCopy( aClipBoard, sal_True );
					bDone = sal_True;
				}
			}
			break;
			case KEYFUNC_COPY:
			{
                Reference<com::sun::star::datatransfer::clipboard::XClipboard> aClipBoard(GetWindow()->GetClipboard());
				CutCopy( aClipBoard, sal_False );
                bDone = sal_True;
			}
			break;
			case KEYFUNC_PASTE:
			{
				if ( !bReadOnly && IsPasteEnabled() )
				{
					pEditEngine->pImpEditEngine->UndoActionStart( EDITUNDO_PASTE );
                    Reference<com::sun::star::datatransfer::clipboard::XClipboard> aClipBoard(GetWindow()->GetClipboard());
					Paste( aClipBoard, pEditEngine->pImpEditEngine->GetStatus().AllowPasteSpecial() );
					pEditEngine->pImpEditEngine->UndoActionEnd( EDITUNDO_PASTE );
					bDone = sal_True;
				}
			}
			break;
			default:
				break;
        }
    }

    if( !bDone )
	    bDone = pEditEngine->PostKeyEvent( rKeyEvent, GetEditViewPtr() );

    return bDone;
}

sal_Bool ImpEditView::MouseButtonUp( const MouseEvent& rMouseEvent )
{
	if ( pEditEngine->pImpEditEngine->aStatus.NotifyCursorMovements() )
	{
		if ( pEditEngine->pImpEditEngine->aStatus.GetPrevParagraph() != pEditEngine->pImpEditEngine->GetEditDoc().GetPos( GetEditSelection().Max().GetNode() ) )
		{
			pEditEngine->pImpEditEngine->aStatus.GetStatusWord() = pEditEngine->pImpEditEngine->aStatus.GetStatusWord() | EE_STAT_CRSRLEFTPARA;
			pEditEngine->pImpEditEngine->CallStatusHdl();
		}
	}
	nTravelXPos = TRAVEL_X_DONTKNOW;
    nCursorBidiLevel = CURSOR_BIDILEVEL_DONTKNOW;
    nExtraCursorFlags = 0;
    bClickedInSelection = sal_False;

    if ( rMouseEvent.IsMiddle() && !bReadOnly &&
         ( GetWindow()->GetSettings().GetMouseSettings().GetMiddleButtonAction() == MOUSE_MIDDLE_PASTESELECTION ) )
    {
        Reference<com::sun::star::datatransfer::clipboard::XClipboard> aClipBoard(GetWindow()->GetPrimarySelection());
        Paste( aClipBoard );
    }
    else if ( rMouseEvent.IsLeft() && GetEditSelection().HasRange() )
    {
        Reference<com::sun::star::datatransfer::clipboard::XClipboard> aClipBoard(GetWindow()->GetPrimarySelection());
        CutCopy( aClipBoard, sal_False );
    }

	return pEditEngine->pImpEditEngine->MouseButtonUp( rMouseEvent, GetEditViewPtr() );
}

sal_Bool ImpEditView::MouseButtonDown( const MouseEvent& rMouseEvent )
{
	pEditEngine->pImpEditEngine->CheckIdleFormatter();	// Falls schnelles Tippen und MouseButtonDown
	if ( pEditEngine->pImpEditEngine->aStatus.NotifyCursorMovements() )
		pEditEngine->pImpEditEngine->aStatus.GetPrevParagraph() = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( GetEditSelection().Max().GetNode() );
	nTravelXPos = TRAVEL_X_DONTKNOW;
    nExtraCursorFlags = 0;
    nCursorBidiLevel    = CURSOR_BIDILEVEL_DONTKNOW;
    bClickedInSelection = IsSelectionAtPoint( rMouseEvent.GetPosPixel() );
	return pEditEngine->pImpEditEngine->MouseButtonDown( rMouseEvent, GetEditViewPtr() );
}

sal_Bool ImpEditView::MouseMove( const MouseEvent& rMouseEvent )
{
	return pEditEngine->pImpEditEngine->MouseMove( rMouseEvent, GetEditViewPtr() );
}

void ImpEditView::Command( const CommandEvent& rCEvt )
{
	pEditEngine->pImpEditEngine->CheckIdleFormatter();	// Falls schnelles Tippen und MouseButtonDown
	pEditEngine->pImpEditEngine->Command( rCEvt, GetEditViewPtr() );
}


void ImpEditView::SetInsertMode( sal_Bool bInsert )
{
	if ( bInsert != IsInsertMode() )
	{
		SetFlags( nControl, EV_CNTRL_OVERWRITE, !bInsert );
		ShowCursor( DoAutoScroll(), sal_False );
	}
}

sal_Bool ImpEditView::IsWrongSpelledWord( const EditPaM& rPaM, sal_Bool bMarkIfWrong )
{
	sal_Bool bIsWrong = sal_False;
#ifndef SVX_LIGHT
	if ( rPaM.GetNode()->GetWrongList() )
	{
		EditSelection aSel = pEditEngine->pImpEditEngine->SelectWord( rPaM, ::com::sun::star::i18n::WordType::DICTIONARY_WORD );
		bIsWrong = rPaM.GetNode()->GetWrongList()->HasWrong( aSel.Min().GetIndex(), aSel.Max().GetIndex() );
		if ( bIsWrong && bMarkIfWrong )
		{
			DrawSelection();	// alte Selektion 'weg-zeichnen'
			SetEditSelection( aSel );
			DrawSelection();
		}
	}
#endif // !SVX_LIGHT
	return bIsWrong;
}

String ImpEditView::SpellIgnoreOrAddWord( sal_Bool bAdd )
{
	String aWord;
#ifndef SVX_LIGHT
	if ( pEditEngine->pImpEditEngine->GetSpeller().is() )
	{
		EditPaM aPaM = GetEditSelection().Max();
		if ( !HasSelection() )
		{
			EditSelection aSel = pEditEngine->pImpEditEngine->SelectWord( aPaM );
			aWord = pEditEngine->pImpEditEngine->GetSelected( aSel );
		}
		else
		{
			aWord = pEditEngine->pImpEditEngine->GetSelected( GetEditSelection() );
			// Und deselektieren
			DrawSelection();	// alte Selektion 'weg-zeichnen'
			SetEditSelection( EditSelection( aPaM, aPaM ) );
			DrawSelection();
		}

		if ( aWord.Len() )
		{
			if ( bAdd )
			{
				DBG_ERROR( "Sorry, AddWord not implemented" );
			}
			else // Ignore
			{
                Reference< XDictionary >  xDic( SvxGetIgnoreAllList(), UNO_QUERY );
				if (xDic.is())
					xDic->add( aWord, sal_False, String() );
			}
			const EditDoc& rDoc = pEditEngine->pImpEditEngine->GetEditDoc();
			sal_uInt32 nNodes = rDoc.Count();
			for ( sal_uInt32 n = 0; n < nNodes; n++ )
			{
				ContentNode* pNode = rDoc.GetObject( n );
				pNode->GetWrongList()->MarkWrongsInvalid();
			}
			pEditEngine->pImpEditEngine->DoOnlineSpelling( aPaM.GetNode() );
			pEditEngine->pImpEditEngine->StartOnlineSpellTimer();
		}
	}
#endif // !SVX_LIGHT
	return aWord;
}

void ImpEditView::DeleteSelected()
{
	DrawSelection();

	pEditEngine->pImpEditEngine->UndoActionStart( EDITUNDO_DELETE );

	EditPaM aPaM = pEditEngine->pImpEditEngine->DeleteSelected( GetEditSelection() );

	pEditEngine->pImpEditEngine->UndoActionEnd( EDITUNDO_DELETE );

	SetEditSelection( EditSelection( aPaM, aPaM ) );
	pEditEngine->pImpEditEngine->FormatAndUpdate( GetEditViewPtr() );
	ShowCursor( DoAutoScroll(), sal_True );
}

const SvxFieldItem* ImpEditView::GetField( const Point& rPos, sal_uInt32* pPara, sal_uInt16* pPos ) const
{
	if( !GetOutputArea().IsInside( rPos ) )
		return 0;

	Point aDocPos( GetDocPos( rPos ) );
	EditPaM aPaM = pEditEngine->pImpEditEngine->GetPaM( aDocPos, sal_False );

	if ( aPaM.GetIndex() == aPaM.GetNode()->Len() )
	{
		// Sonst immer, wenn Feld ganz am Schluss und Mouse unter Text
		return 0;
	}

	const CharAttribArray& rAttrs = aPaM.GetNode()->GetCharAttribs().GetAttribs();
	sal_uInt16 nXPos = aPaM.GetIndex();
	for ( sal_uInt16 nAttr = rAttrs.Count(); nAttr; )
	{
		EditCharAttrib* pAttr = rAttrs[--nAttr];
		if ( pAttr->GetStart() == nXPos )
			if ( pAttr->Which() == EE_FEATURE_FIELD )
			{
				DBG_ASSERT( pAttr->GetItem()->ISA( SvxFieldItem ), "Kein FeldItem..." );
				if ( pPara )
					*pPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aPaM.GetNode() );
				if ( pPos )
					*pPos = pAttr->GetStart();
				return (const SvxFieldItem*)pAttr->GetItem();
			}
	}
	return NULL;
}

sal_Bool ImpEditView::IsBulletArea( const Point& rPos, sal_uInt32* pPara )
{
    if ( pPara )
        *pPara = EE_PARA_NOT_FOUND;

    if( !GetOutputArea().IsInside( rPos ) )
		return sal_False;

	Point aDocPos( GetDocPos( rPos ) );
	EditPaM aPaM = pEditEngine->pImpEditEngine->GetPaM( aDocPos, sal_False );

	if ( aPaM.GetIndex() == 0 )
	{
        sal_uInt32 nPara = pEditEngine->pImpEditEngine->aEditDoc.GetPos( aPaM.GetNode() );
		Rectangle aBulletArea = pEditEngine->GetBulletArea( nPara );
        long nY = pEditEngine->GetDocPosTopLeft( nPara ).Y();
        ParaPortion* pParaPortion = pEditEngine->pImpEditEngine->GetParaPortions().GetObject( nPara );
        nY += pParaPortion->GetFirstLineOffset();
        if ( ( aDocPos.Y() > ( nY + aBulletArea.Top() ) ) &&
             ( aDocPos.Y() < ( nY + aBulletArea.Bottom() ) ) &&
             ( aDocPos.X() > ( aBulletArea.Left() ) ) &&
             ( aDocPos.X() < ( aBulletArea.Right() ) ) )
        {
            if ( pPara )
                *pPara = nPara;
		    return sal_True;
        }
	}

    return sal_False;
}

void ImpEditView::CutCopy( ::com::sun::star::uno::Reference< ::com::sun::star::datatransfer::clipboard::XClipboard >& rxClipboard, sal_Bool bCut )
{
	if ( rxClipboard.is() && GetEditSelection().HasRange() )
    {
		uno::Reference< datatransfer::XTransferable > xData = pEditEngine->pImpEditEngine->CreateTransferable( GetEditSelection() );

        const sal_uInt32 nRef = Application::ReleaseSolarMutex();

        try
		{
            rxClipboard->setContents( xData, NULL );

            // #87756# FlushClipboard, but it would be better to become a TerminateListener to the Desktop and flush on demand...
            uno::Reference< datatransfer::clipboard::XFlushableClipboard > xFlushableClipboard( rxClipboard, uno::UNO_QUERY );
		    if( xFlushableClipboard.is() )
			    xFlushableClipboard->flushClipboard();
		}
		catch( const ::com::sun::star::uno::Exception& )
		{
		}

        Application::AcquireSolarMutex( nRef );

        if ( bCut )
	    {
            pEditEngine->pImpEditEngine->UndoActionStart( EDITUNDO_CUT );
		    DeleteSelected();
            pEditEngine->pImpEditEngine->UndoActionEnd( EDITUNDO_CUT );

	    }
    }
}

void ImpEditView::Paste( ::com::sun::star::uno::Reference< ::com::sun::star::datatransfer::clipboard::XClipboard >& rxClipboard, sal_Bool bUseSpecial )
{
	if ( rxClipboard.is() )
	{
        uno::Reference< datatransfer::XTransferable > xDataObj;

        const sal_uInt32 nRef = Application::ReleaseSolarMutex();

        try
		{
		    xDataObj = rxClipboard->getContents();
		}
		catch( const ::com::sun::star::uno::Exception& )
		{
		}

        Application::AcquireSolarMutex( nRef );

        if ( xDataObj.is() && EditEngine::HasValidData( xDataObj ) )
	    {
	        pEditEngine->pImpEditEngine->UndoActionStart( EDITUNDO_PASTE );

            EditSelection aSel( GetEditSelection() );
            if ( aSel.HasRange() )
	        {
		        DrawSelection();
		        aSel = pEditEngine->pImpEditEngine->ImpDeleteSelection( aSel );
            }

            PasteOrDropInfos aPasteOrDropInfos;
            aPasteOrDropInfos.nAction = EE_ACTION_PASTE;
            aPasteOrDropInfos.nStartPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aSel.Min().GetNode() );
            pEditEngine->pImpEditEngine->aBeginPasteOrDropHdl.Call( &aPasteOrDropInfos );

            if ( DoSingleLinePaste() )
	        {
			    datatransfer::DataFlavor aFlavor;
			    SotExchange::GetFormatDataFlavor( SOT_FORMAT_STRING, aFlavor );
			    if ( xDataObj->isDataFlavorSupported( aFlavor ) )
			    {
					try
        			{
						uno::Any aData = xDataObj->getTransferData( aFlavor );
						::rtl::OUString aTmpText;
						aData >>= aTmpText;
						String aText( aTmpText );
						aText.ConvertLineEnd( LINEEND_LF );
						aText.SearchAndReplaceAll( LINE_SEP, ' ' );
						aSel = pEditEngine->pImpEditEngine->ImpInsertText( aSel, aText );
					}
					catch( ... )
					{
						; // #i9286# can happen, even if isDataFlavorSupported returns true...
					}
			    }
	        }
	        else
	        {
                aSel = pEditEngine->pImpEditEngine->InsertText( xDataObj, String(), aSel.Min(), bUseSpecial && pEditEngine->pImpEditEngine->GetStatus().AllowPasteSpecial() );
	        }

            aPasteOrDropInfos.nEndPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aSel.Max().GetNode() );
            pEditEngine->pImpEditEngine->aEndPasteOrDropHdl.Call( &aPasteOrDropInfos );

	        pEditEngine->pImpEditEngine->UndoActionEnd( EDITUNDO_PASTE );
	        SetEditSelection( aSel );
	        pEditEngine->pImpEditEngine->UpdateSelections();
	        pEditEngine->pImpEditEngine->FormatAndUpdate( GetEditViewPtr() );
            ShowCursor( DoAutoScroll(), sal_True );
        }
	}
}


sal_Bool ImpEditView::IsInSelection( const EditPaM& rPaM )
{
	EditSelection aSel = GetEditSelection();
	if ( !aSel.HasRange() )
		return sal_False;

	aSel.Adjust( pEditEngine->pImpEditEngine->GetEditDoc() );

	sal_uInt32 nStartNode = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aSel.Min().GetNode() );
	sal_uInt32 nEndNode = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aSel.Max().GetNode() );
	sal_uInt32 nCurNode = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( rPaM.GetNode() );

	if ( ( nCurNode > nStartNode ) && ( nCurNode < nEndNode ) )
		return sal_True;

	if ( nStartNode == nEndNode )
	{
		if ( nCurNode == nStartNode )
			if ( ( rPaM.GetIndex() >= aSel.Min().GetIndex() ) && ( rPaM.GetIndex() < aSel.Max().GetIndex() ) )
				return sal_True;
	}
	else if ( ( nCurNode == nStartNode ) && ( rPaM.GetIndex() >= aSel.Min().GetIndex() ) )
		return sal_True;
	else if ( ( nCurNode == nEndNode ) && ( rPaM.GetIndex() < aSel.Max().GetIndex() ) )
		return sal_True;

	return sal_False;
}

void ImpEditView::CreateAnchor()
{
	pEditEngine->pImpEditEngine->bInSelection = sal_True;
	GetEditSelection().Min() = GetEditSelection().Max();
}

void ImpEditView::DeselectAll()
{
	pEditEngine->pImpEditEngine->bInSelection = sal_False;
	DrawSelection();
	GetEditSelection().Min() = GetEditSelection().Max();
}

sal_Bool ImpEditView::IsSelectionAtPoint( const Point& rPosPixel )
{
	if ( pDragAndDropInfo && pDragAndDropInfo->pField )
		return sal_True;

	Point aMousePos( rPosPixel );

	// Logische Einheiten...
	aMousePos = GetWindow()->PixelToLogic( aMousePos );

	if ( ( !GetOutputArea().IsInside( aMousePos ) ) && !pEditEngine->pImpEditEngine->IsInSelectionMode() )
	{
		return sal_False;
	}

	Point aDocPos( GetDocPos( aMousePos ) );
	EditPaM aPaM = pEditEngine->pImpEditEngine->GetPaM( aDocPos, sal_False );
	return IsInSelection( aPaM );
}

sal_Bool ImpEditView::SetCursorAtPoint( const Point& rPointPixel )
{
	pEditEngine->pImpEditEngine->CheckIdleFormatter();

	Point aMousePos( rPointPixel );

	// Logische Einheiten...
	aMousePos = GetWindow()->PixelToLogic( aMousePos );

	if ( ( !GetOutputArea().IsInside( aMousePos ) ) && !pEditEngine->pImpEditEngine->IsInSelectionMode() )
	{
		return sal_False;
	}

	Point aDocPos( GetDocPos( aMousePos ) );

	// Kann optimiert werden: Erst innerhalb eines Absatzes die Zeilen
	// fuer den PaM durchwuehlen, dann nochmal mit dem PaM fuer das Rect,
	// obwohl die Zeile schon bekannt ist....
	// Das muss doch nicht sein !

	EditPaM aPaM = pEditEngine->pImpEditEngine->GetPaM( aDocPos );
	sal_Bool bGotoCursor = DoAutoScroll();

	// aTmpNewSel: Diff zwischen alt und neu, nicht die neue Selektion
	EditSelection aTmpNewSel( GetEditSelection().Max(), aPaM );

    // --> OD 2005-12-16 #i27299#
    // work on copy of current selection and set new selection, if it has changed.
    EditSelection aNewEditSelection( GetEditSelection() );

    aNewEditSelection.Max() = aPaM;
	if ( !pEditEngine->pImpEditEngine->aSelEngine.HasAnchor() )
	{
        if ( aNewEditSelection.Min() != aPaM )
            pEditEngine->pImpEditEngine->CursorMoved( aNewEditSelection.Min().GetNode() );
        aNewEditSelection.Min() = aPaM;
	}
	else
	{
		DrawSelection( aTmpNewSel );
	}

    // set changed text selection
    if ( GetEditSelection() != aNewEditSelection )
    {
        SetEditSelection( aNewEditSelection );
    }
    // <--

	sal_Bool bForceCursor = ( pDragAndDropInfo ? sal_False : sal_True ) && !pEditEngine->pImpEditEngine->IsInSelectionMode();
	ShowCursor( bGotoCursor, bForceCursor );
	return sal_True;
}


void ImpEditView::HideDDCursor()
{
	if ( pDragAndDropInfo && pDragAndDropInfo->bVisCursor )
	{
		GetWindow()->DrawOutDev( pDragAndDropInfo->aCurSavedCursor.TopLeft(), pDragAndDropInfo->aCurSavedCursor.GetSize(),
							Point(0,0), pDragAndDropInfo->aCurSavedCursor.GetSize(),*pDragAndDropInfo->pBackground );
		pDragAndDropInfo->bVisCursor = sal_False;
	}
}

void ImpEditView::ShowDDCursor( const Rectangle& rRect )
{
	if ( pDragAndDropInfo && !pDragAndDropInfo->bVisCursor )
	{
		if ( pOutWin->GetCursor() )
			pOutWin->GetCursor()->Hide();

		Color aOldFillColor = GetWindow()->GetFillColor();
		GetWindow()->SetFillColor( Color(4210752) );	// GRAY BRUSH_50, OLDSV, change to DDCursor!

		// Hintergrund sichern...
		Rectangle aSaveRec( GetWindow()->LogicToPixel( rRect ) );
		// lieber etwas mehr sichern...
		aSaveRec.Right() += 1;
		aSaveRec.Bottom() += 1;

		Size aNewSzPx( aSaveRec.GetSize() );
		if ( !pDragAndDropInfo->pBackground )
		{
			pDragAndDropInfo->pBackground = new VirtualDevice( *GetWindow() );
	    	MapMode aMapMode( GetWindow()->GetMapMode() );
	    	aMapMode.SetOrigin( Point( 0, 0 ) );
	    	pDragAndDropInfo->pBackground->SetMapMode( aMapMode );

		}

#ifdef DBG_UTIL
		Size aCurSzPx( pDragAndDropInfo->pBackground->GetOutputSizePixel() );
		if ( ( aCurSzPx.Width() < aNewSzPx.Width() ) ||( aCurSzPx.Height() < aNewSzPx.Height() ) )
		{
			sal_Bool bDone = pDragAndDropInfo->pBackground->SetOutputSizePixel( aNewSzPx );
			DBG_ASSERT( bDone, "Virtuelles Device kaputt?" );
		}
#endif

		aSaveRec = GetWindow()->PixelToLogic( aSaveRec );

		pDragAndDropInfo->pBackground->DrawOutDev( Point(0,0), aSaveRec.GetSize(),
									aSaveRec.TopLeft(), aSaveRec.GetSize(), *GetWindow() );
		pDragAndDropInfo->aCurSavedCursor = aSaveRec;

		// Cursor malen...
		GetWindow()->DrawRect( rRect );

		pDragAndDropInfo->bVisCursor = sal_True;
		pDragAndDropInfo->aCurCursor = rRect;

		GetWindow()->SetFillColor( aOldFillColor );
	}
}

void ImpEditView::dragGestureRecognized( const ::com::sun::star::datatransfer::dnd::DragGestureEvent& rDGE ) throw (::com::sun::star::uno::RuntimeException)
{
    DBG_ASSERT( !pDragAndDropInfo, "dragGestureRecognized - DragAndDropInfo exist!" );

    vos::OGuard aVclGuard( Application::GetSolarMutex() );

    pDragAndDropInfo = NULL;

    Point aMousePosPixel( rDGE.DragOriginX, rDGE.DragOriginY );

    EditSelection aCopySel( GetEditSelection() );
    aCopySel.Adjust( pEditEngine->pImpEditEngine->GetEditDoc() );

    if ( GetEditSelection().HasRange() && bClickedInSelection )
    {
        pDragAndDropInfo = new DragAndDropInfo();
    }
    else
    {
	    // Field?!
		sal_uInt32 nPara;
		sal_uInt16 nPos;
	    Point aMousePos = GetWindow()->PixelToLogic( aMousePosPixel );
		const SvxFieldItem* pField = GetField( aMousePos, &nPara, &nPos );
		if ( pField )
		{
			pDragAndDropInfo = new DragAndDropInfo();
			pDragAndDropInfo->pField = pField;
            ContentNode* pNode = pEditEngine->pImpEditEngine->GetEditDoc().GetObject( nPara );
            aCopySel = EditSelection( EditPaM( pNode, nPos ), EditPaM( pNode, nPos+1 ) );
			GetEditSelection() = aCopySel;
			DrawSelection();
			sal_Bool bGotoCursor = DoAutoScroll();
			sal_Bool bForceCursor = ( pDragAndDropInfo ? sal_False : sal_True ) && !pEditEngine->pImpEditEngine->IsInSelectionMode();
			ShowCursor( bGotoCursor, bForceCursor );
		}
        else if ( IsBulletArea( aMousePos, &nPara ) )
        {
            pDragAndDropInfo = new DragAndDropInfo();
            pDragAndDropInfo->bOutlinerMode = sal_True;
            EditPaM aStartPaM( pEditEngine->pImpEditEngine->GetEditDoc().GetObject( nPara ), 0 );
            EditPaM aEndPaM( aStartPaM );
            const SfxInt16Item& rLevel = (const SfxInt16Item&) pEditEngine->GetParaAttrib( nPara, EE_PARA_OUTLLEVEL );
            for ( sal_uInt32 n = nPara +1; n < pEditEngine->pImpEditEngine->GetEditDoc().Count(); n++ )
            {
                const SfxInt16Item& rL = (const SfxInt16Item&) pEditEngine->GetParaAttrib( n, EE_PARA_OUTLLEVEL );
                if ( rL.GetValue() > rLevel.GetValue() )
                {
                    aEndPaM.SetNode( pEditEngine->pImpEditEngine->GetEditDoc().GetObject( n ) );
                }
                else
                {
                    break;
                }
            }
            aEndPaM.GetIndex() = aEndPaM.GetNode()->Len();
            SetEditSelection( EditSelection( aStartPaM, aEndPaM ) );
        }
    }

    if ( pDragAndDropInfo )
    {

	    pDragAndDropInfo->bStarterOfDD = sal_True;

	    // Sensibler Bereich, wo gescrollt werden soll.
	    Size aSz( 5, 0 );
	    aSz = GetWindow()->PixelToLogic( aSz );
	    pDragAndDropInfo->nSensibleRange = (sal_uInt16) aSz.Width();
	    pDragAndDropInfo->nCursorWidth = (sal_uInt16) aSz.Width() / 2;
	    pDragAndDropInfo->aBeginDragSel = pEditEngine->pImpEditEngine->CreateESel( aCopySel );

        uno::Reference< datatransfer::XTransferable > xData = pEditEngine->pImpEditEngine->CreateTransferable( aCopySel );

        sal_Int8 nActions = bReadOnly ? datatransfer::dnd::DNDConstants::ACTION_COPY : datatransfer::dnd::DNDConstants::ACTION_COPY_OR_MOVE;

        rDGE.DragSource->startDrag( rDGE, nActions, 0 /*cursor*/, 0 /*image*/, xData, mxDnDListener );
		// Falls Drag&Move in einer Engine, muessen Copy&Del geklammert sein!
	    GetCursor()->Hide();

    }
}

void ImpEditView::dragDropEnd( const ::com::sun::star::datatransfer::dnd::DragSourceDropEvent& rDSDE ) throw (::com::sun::star::uno::RuntimeException)
{
	vos::OGuard aVclGuard( Application::GetSolarMutex() );

	DBG_ASSERT( pDragAndDropInfo, "ImpEditView::dragDropEnd: pDragAndDropInfo is NULL!" );

	// #123688# Shouldn't happen, but seems to happen...
	if ( pDragAndDropInfo )
	{
		if ( !bReadOnly && rDSDE.DropSuccess && !pDragAndDropInfo->bOutlinerMode && ( rDSDE.DropAction & datatransfer::dnd::DNDConstants::ACTION_MOVE ) )
		{
			if ( pDragAndDropInfo->bStarterOfDD && pDragAndDropInfo->bDroppedInMe )
			{
				// DropPos: Wohin wurde gedroppt, unabhaengig von laenge.
				ESelection aDropPos( pDragAndDropInfo->aDropSel.nStartPara, pDragAndDropInfo->aDropSel.nStartPos, pDragAndDropInfo->aDropSel.nStartPara, pDragAndDropInfo->aDropSel.nStartPos );
				ESelection aToBeDelSel = pDragAndDropInfo->aBeginDragSel;
				ESelection aNewSel( pDragAndDropInfo->aDropSel.nEndPara, pDragAndDropInfo->aDropSel.nEndPos,
									pDragAndDropInfo->aDropSel.nEndPara, pDragAndDropInfo->aDropSel.nEndPos );
				sal_Bool bBeforeSelection = aDropPos.IsLess( pDragAndDropInfo->aBeginDragSel );
				sal_uInt32 nParaDiff = pDragAndDropInfo->aBeginDragSel.nEndPara - pDragAndDropInfo->aBeginDragSel.nStartPara;
				if ( bBeforeSelection )
				{
					// aToBeDelSel anpassen.
					DBG_ASSERT( pDragAndDropInfo->aBeginDragSel.nStartPara >= pDragAndDropInfo->aDropSel.nStartPara, "Doch nicht davor?" );
					aToBeDelSel.nStartPara = aToBeDelSel.nStartPara + nParaDiff;
					aToBeDelSel.nEndPara = aToBeDelSel.nEndPara + nParaDiff;
					// Zeichen korrigieren?
					if ( aToBeDelSel.nStartPara == pDragAndDropInfo->aDropSel.nEndPara )
					{
						sal_uInt16 nMoreChars;
						if ( pDragAndDropInfo->aDropSel.nStartPara == pDragAndDropInfo->aDropSel.nEndPara )
							nMoreChars = pDragAndDropInfo->aDropSel.nEndPos - pDragAndDropInfo->aDropSel.nStartPos;
						else
							nMoreChars = pDragAndDropInfo->aDropSel.nEndPos;
						aToBeDelSel.nStartPos =
                            aToBeDelSel.nStartPos + nMoreChars;
						if ( aToBeDelSel.nStartPara == aToBeDelSel.nEndPara )
							aToBeDelSel.nEndPos =
                                aToBeDelSel.nEndPos + nMoreChars;
					}
				}
				else
				{
					// aToBeDelSel ist ok, aber Selektion der View
					// muss angepasst werden, wenn davor geloescht wird!
					DBG_ASSERT( pDragAndDropInfo->aBeginDragSel.nStartPara <= pDragAndDropInfo->aDropSel.nStartPara, "Doch nicht davor?" );
					aNewSel.nStartPara = aNewSel.nStartPara - nParaDiff;
					aNewSel.nEndPara = aNewSel.nEndPara - nParaDiff;
					// Zeichen korrigieren?
					if ( pDragAndDropInfo->aBeginDragSel.nEndPara == pDragAndDropInfo->aDropSel.nStartPara )
					{
						sal_uInt16 nLessChars;
						if ( pDragAndDropInfo->aBeginDragSel.nStartPara == pDragAndDropInfo->aBeginDragSel.nEndPara )
							nLessChars = pDragAndDropInfo->aBeginDragSel.nEndPos - pDragAndDropInfo->aBeginDragSel.nStartPos;
						else
							nLessChars = pDragAndDropInfo->aBeginDragSel.nEndPos;
						aNewSel.nStartPos = aNewSel.nStartPos - nLessChars;
						if ( aNewSel.nStartPara == aNewSel.nEndPara )
							aNewSel.nEndPos = aNewSel.nEndPos - nLessChars;
					}
				}

				DrawSelection();
				EditSelection aDelSel( pEditEngine->pImpEditEngine->CreateSel( aToBeDelSel ) );
				DBG_ASSERT( !aDelSel.DbgIsBuggy( pEditEngine->pImpEditEngine->aEditDoc ), "ToBeDel ist buggy!" );
				pEditEngine->pImpEditEngine->ImpDeleteSelection( aDelSel );
				if ( !bBeforeSelection )
				{
					DBG_ASSERT( !pEditEngine->pImpEditEngine->CreateSel( aNewSel ).DbgIsBuggy(pEditEngine->pImpEditEngine->aEditDoc), "Bad" );
					SetEditSelection( pEditEngine->pImpEditEngine->CreateSel( aNewSel ) );
				}
				pEditEngine->pImpEditEngine->FormatAndUpdate( pEditEngine->pImpEditEngine->GetActiveView() );
				DrawSelection();
			}
			else
			{
				// andere EditEngine...
				if ( pEditEngine->pImpEditEngine->ImplHasText() )   // #88630# SC ist removing the content when switching the task
					DeleteSelected();
			}
		}

		if ( pDragAndDropInfo->bUndoAction )
			pEditEngine->pImpEditEngine->UndoActionEnd( EDITUNDO_DRAGANDDROP );

		HideDDCursor();
		ShowCursor( DoAutoScroll(), sal_True );
		delete pDragAndDropInfo;
		pDragAndDropInfo = NULL;
		pEditEngine->GetEndDropHdl().Call(GetEditViewPtr());
	}
}

void ImpEditView::drop( const ::com::sun::star::datatransfer::dnd::DropTargetDropEvent& rDTDE ) throw (::com::sun::star::uno::RuntimeException)
{
	vos::OGuard aVclGuard( Application::GetSolarMutex() );

    DBG_ASSERT( pDragAndDropInfo, "Drop - No Drag&Drop info?!" );

    if ( pDragAndDropInfo && pDragAndDropInfo->bDragAccepted )
    {
		pEditEngine->GetBeginDropHdl().Call(GetEditViewPtr());
	    sal_Bool bChanges = sal_False;

        HideDDCursor();

        if ( pDragAndDropInfo->bStarterOfDD )
        {
            pEditEngine->pImpEditEngine->UndoActionStart( EDITUNDO_DRAGANDDROP );
            pDragAndDropInfo->bUndoAction = sal_True;
        }

        if ( pDragAndDropInfo->bOutlinerMode )
        {
            bChanges = sal_True;
            GetEditViewPtr()->MoveParagraphs( Range( pDragAndDropInfo->aBeginDragSel.nStartPara, pDragAndDropInfo->aBeginDragSel.nEndPara ), pDragAndDropInfo->nOutlinerDropDest );
        }
        else
        {
	        uno::Reference< datatransfer::XTransferable > xDataObj = rDTDE.Transferable;
	        if ( xDataObj.is() )
	        {
                bChanges = sal_True;
	            // Selektion wegmalen...
	            DrawSelection();
                EditPaM aPaM( pDragAndDropInfo->aDropDest );

                PasteOrDropInfos aPasteOrDropInfos;
                aPasteOrDropInfos.nAction = EE_ACTION_DROP;
                aPasteOrDropInfos.nStartPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aPaM.GetNode() );
                pEditEngine->pImpEditEngine->aBeginPasteOrDropHdl.Call( &aPasteOrDropInfos );

                EditSelection aNewSel = pEditEngine->pImpEditEngine->InsertText( xDataObj, String(), aPaM, pEditEngine->pImpEditEngine->GetStatus().AllowPasteSpecial() );

                aPasteOrDropInfos.nEndPara = pEditEngine->pImpEditEngine->GetEditDoc().GetPos( aNewSel.Max().GetNode() );
                pEditEngine->pImpEditEngine->aEndPasteOrDropHdl.Call( &aPasteOrDropInfos );

                SetEditSelection( aNewSel );
	            pEditEngine->pImpEditEngine->FormatAndUpdate( pEditEngine->pImpEditEngine->GetActiveView() );
	            if ( pDragAndDropInfo->bStarterOfDD )
	            {
		            // Nur dann setzen, wenn in gleicher Engine!
		            pDragAndDropInfo->aDropSel.nStartPara = pEditEngine->pImpEditEngine->aEditDoc.GetPos( aPaM.GetNode() );
		            pDragAndDropInfo->aDropSel.nStartPos = aPaM.GetIndex();
		            pDragAndDropInfo->aDropSel.nEndPara = pEditEngine->pImpEditEngine->aEditDoc.GetPos( aNewSel.Max().GetNode() );
		            pDragAndDropInfo->aDropSel.nEndPos = aNewSel.Max().GetIndex();
		            pDragAndDropInfo->bDroppedInMe = sal_True;
	            }
            }
        }

        if ( bChanges )
        {
            rDTDE.Context->acceptDrop( rDTDE.DropAction );
        }

	    if ( !pDragAndDropInfo->bStarterOfDD )
	    {
		    delete pDragAndDropInfo;
		    pDragAndDropInfo = NULL;
	    }

        rDTDE.Context->dropComplete( bChanges );
    }
}

void ImpEditView::dragEnter( const ::com::sun::star::datatransfer::dnd::DropTargetDragEnterEvent& rDTDEE ) throw (::com::sun::star::uno::RuntimeException)
{
	vos::OGuard aVclGuard( Application::GetSolarMutex() );

    if ( !pDragAndDropInfo )
        pDragAndDropInfo = new DragAndDropInfo( );

    pDragAndDropInfo->bHasValidData = sal_False;

    // Check for supported format...
    // Only check for text, will also be there if bin or rtf
    datatransfer::DataFlavor aTextFlavor;
	SotExchange::GetFormatDataFlavor( SOT_FORMAT_STRING, aTextFlavor );
    const ::com::sun::star::datatransfer::DataFlavor* pFlavors = rDTDEE.SupportedDataFlavors.getConstArray();
    int nFlavors = rDTDEE.SupportedDataFlavors.getLength();
    for ( int n = 0; n < nFlavors; n++ )
    {
        if( TransferableDataHelper::IsEqual( pFlavors[n], aTextFlavor ) )
        {
            pDragAndDropInfo->bHasValidData = sal_True;
            break;
        }
    }

    dragOver( rDTDEE );
}

void ImpEditView::dragExit( const ::com::sun::star::datatransfer::dnd::DropTargetEvent& ) throw (::com::sun::star::uno::RuntimeException)
{
	vos::OGuard aVclGuard( Application::GetSolarMutex() );

    HideDDCursor();

	if ( pDragAndDropInfo && !pDragAndDropInfo->bStarterOfDD )
	{
		delete pDragAndDropInfo;
		pDragAndDropInfo = NULL;
	}
}

void ImpEditView::dragOver( const ::com::sun::star::datatransfer::dnd::DropTargetDragEvent& rDTDE ) throw (::com::sun::star::uno::RuntimeException)
{
    vos::OGuard aVclGuard( Application::GetSolarMutex() );

    Point aMousePos( rDTDE.LocationX, rDTDE.LocationY );
	aMousePos = GetWindow()->PixelToLogic( aMousePos );

    sal_Bool bAccept = sal_False;

    if ( GetOutputArea().IsInside( aMousePos ) && !bReadOnly )
    {
//        sal_Int8 nSupportedActions = bReadOnly ? datatransfer::dnd::DNDConstants::ACTION_COPY : datatransfer::dnd::DNDConstants::ACTION_COPY_OR_MOVE;

        if ( pDragAndDropInfo && pDragAndDropInfo->bHasValidData /* && ( nSupportedActions & rDTDE.DropAction ) MT: Default = 0x80 ?! */ )
        {
            bAccept = sal_True;

            sal_Bool bAllowScroll = DoAutoScroll();
	        if ( bAllowScroll )
	        {
		        long nScrollX = 0;
		        long nScrollY = 0;
		        // pruefen, ob im sensitiven Bereich
		        if ( ( (aMousePos.X()-pDragAndDropInfo->nSensibleRange) < GetOutputArea().Left() ) && ( ( aMousePos.X() + pDragAndDropInfo->nSensibleRange ) > GetOutputArea().Left() ) )
				        nScrollX = GetOutputArea().GetWidth() / SCRLRANGE;
		        else if ( ( (aMousePos.X()+pDragAndDropInfo->nSensibleRange) > GetOutputArea().Right() ) && ( ( aMousePos.X() - pDragAndDropInfo->nSensibleRange ) < GetOutputArea().Right() ) )
				        nScrollX = -( GetOutputArea().GetWidth() / SCRLRANGE );

		        if ( ( (aMousePos.Y()-pDragAndDropInfo->nSensibleRange) < GetOutputArea().Top() ) && ( ( aMousePos.Y() + pDragAndDropInfo->nSensibleRange ) > GetOutputArea().Top() ) )
				        nScrollY = GetOutputArea().GetHeight() / SCRLRANGE;
		        else if ( ( (aMousePos.Y()+pDragAndDropInfo->nSensibleRange) > GetOutputArea().Bottom() ) && ( ( aMousePos.Y() - pDragAndDropInfo->nSensibleRange ) < GetOutputArea().Bottom() ) )
				        nScrollY = -( GetOutputArea().GetHeight() / SCRLRANGE );

		        if ( nScrollX || nScrollY )
		        {
			        HideDDCursor();
			        Scroll( nScrollX, nScrollY, RGCHK_PAPERSZ1 );
		        }
	        }

	        Point aDocPos( GetDocPos( aMousePos ) );
	        EditPaM aPaM = pEditEngine->pImpEditEngine->GetPaM( aDocPos );
	        pDragAndDropInfo->aDropDest = aPaM;
            if ( pDragAndDropInfo->bOutlinerMode )
            {
                sal_uInt32 nPara = pEditEngine->pImpEditEngine->aEditDoc.GetPos( aPaM.GetNode() );
		        ParaPortion* pPPortion = pEditEngine->pImpEditEngine->GetParaPortions().SaveGetObject( nPara );
                long nDestParaStartY = pEditEngine->pImpEditEngine->GetParaPortions().GetYOffset( pPPortion );
                long nRel = aDocPos.Y() - nDestParaStartY;
                if ( nRel < ( pPPortion->GetHeight() / 2 ) )
                {
                    pDragAndDropInfo->nOutlinerDropDest = nPara;
                }
                else
                {
                    pDragAndDropInfo->nOutlinerDropDest = nPara+1;
                }

                if( ( pDragAndDropInfo->nOutlinerDropDest >= pDragAndDropInfo->aBeginDragSel.nStartPara ) &&
                    ( pDragAndDropInfo->nOutlinerDropDest <= (pDragAndDropInfo->aBeginDragSel.nEndPara+1) ) )
                {
                    bAccept = sal_False;
                }
            }
	        else if ( HasSelection() )
	        {
		        // es darf nicht in eine Selektion gedroppt werden
		        EPaM aP = pEditEngine->pImpEditEngine->CreateEPaM( aPaM );
		        ESelection aDestSel( aP.nPara, aP.nIndex, aP.nPara, aP.nIndex);
		        ESelection aCurSel = pEditEngine->pImpEditEngine->CreateESel( GetEditSelection() );
		        aCurSel.Adjust();
		        if ( !aDestSel.IsLess( aCurSel ) && !aDestSel.IsGreater( aCurSel ) )
		        {
                    bAccept = sal_False;
		        }
	        }
            if ( bAccept )
            {
	            Rectangle aEditCursor;
                if ( pDragAndDropInfo->bOutlinerMode )
                {
                    long nDDYPos;
                    if ( pDragAndDropInfo->nOutlinerDropDest < pEditEngine->pImpEditEngine->GetEditDoc().Count() )
                    {
		                ParaPortion* pPPortion = pEditEngine->pImpEditEngine->GetParaPortions().SaveGetObject( pDragAndDropInfo->nOutlinerDropDest );
                        nDDYPos = pEditEngine->pImpEditEngine->GetParaPortions().GetYOffset( pPPortion );
                    }
                    else
                    {
                        nDDYPos = pEditEngine->pImpEditEngine->GetTextHeight();
                    }
                    Point aStartPos( 0, nDDYPos );
                    aStartPos = GetWindowPos( aStartPos );
                    Point aEndPos( GetOutputArea().GetWidth(), nDDYPos );
                    aEndPos = GetWindowPos( aEndPos );
	                aEditCursor = GetWindow()->LogicToPixel( Rectangle( aStartPos, aEndPos ) );
                    if ( !pEditEngine->IsVertical() )
                    {
                        aEditCursor.Top()--;
                        aEditCursor.Bottom()++;
                    }
                    else
                    {
                        aEditCursor.Left()--;
                        aEditCursor.Right()++;
                    }
	                aEditCursor = GetWindow()->PixelToLogic( aEditCursor );
                }
                else
                {
	                aEditCursor = pEditEngine->pImpEditEngine->PaMtoEditCursor( aPaM );
	                Point aTopLeft( GetWindowPos( aEditCursor.TopLeft() ) );
	                aEditCursor.SetPos( aTopLeft );
	                aEditCursor.Right() = aEditCursor.Left() + pDragAndDropInfo->nCursorWidth;
	                aEditCursor = GetWindow()->LogicToPixel( aEditCursor );
	                aEditCursor = GetWindow()->PixelToLogic( aEditCursor );
                }

                sal_Bool bCursorChanged = !pDragAndDropInfo->bVisCursor || ( pDragAndDropInfo->aCurCursor != aEditCursor );
	            if ( bCursorChanged )
	            {
		            HideDDCursor();
		            ShowDDCursor(aEditCursor );
	            }
                pDragAndDropInfo->bDragAccepted = sal_True;
                rDTDE.Context->acceptDrag( rDTDE.DropAction );
            }
        }
    }

    if ( !bAccept )
    {
        HideDDCursor();
        if (pDragAndDropInfo)
            pDragAndDropInfo->bDragAccepted = sal_False;
        rDTDE.Context->rejectDrag();
    }
}

void ImpEditView::AddDragAndDropListeners()
{
    Window* pWindow = GetWindow();
    if ( !bActiveDragAndDropListener && pWindow && pWindow->GetDragGestureRecognizer().is() )
    {
        vcl::unohelper::DragAndDropWrapper* pDnDWrapper = new vcl::unohelper::DragAndDropWrapper( this );
        mxDnDListener = pDnDWrapper;

        uno::Reference< datatransfer::dnd::XDragGestureListener> xDGL( mxDnDListener, uno::UNO_QUERY );
        pWindow->GetDragGestureRecognizer()->addDragGestureListener( xDGL );
        uno::Reference< datatransfer::dnd::XDropTargetListener> xDTL( xDGL, uno::UNO_QUERY );
        pWindow->GetDropTarget()->addDropTargetListener( xDTL );
        pWindow->GetDropTarget()->setActive( sal_True );
        pWindow->GetDropTarget()->setDefaultActions( datatransfer::dnd::DNDConstants::ACTION_COPY_OR_MOVE );

        bActiveDragAndDropListener = sal_True;
    }
}

void ImpEditView::RemoveDragAndDropListeners()
{
    if ( bActiveDragAndDropListener && GetWindow() && GetWindow()->GetDragGestureRecognizer().is() )
    {
        uno::Reference< datatransfer::dnd::XDragGestureListener> xDGL( mxDnDListener, uno::UNO_QUERY );
        GetWindow()->GetDragGestureRecognizer()->removeDragGestureListener( xDGL );
        uno::Reference< datatransfer::dnd::XDropTargetListener> xDTL( xDGL, uno::UNO_QUERY );
        GetWindow()->GetDropTarget()->removeDropTargetListener( xDTL );

        if ( mxDnDListener.is() )
        {
            uno::Reference< lang::XEventListener> xEL( mxDnDListener, uno::UNO_QUERY );
            xEL->disposing( lang::EventObject() );  // #95154# Empty Source means it's the Client
            mxDnDListener.clear();
        }

        bActiveDragAndDropListener = sal_False;
    }
}
