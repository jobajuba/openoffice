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



// MyEDITDATA, wegen exportiertem EditData
#ifndef _MyEDITDATA_HXX
#define _MyEDITDATA_HXX

#include <tools/string.hxx>
#include "editeng/editengdllapi.h"

#include <svl/svarray.hxx>

class SfxItemSet;
class SfxPoolItem;
class SvParser;
class SvxFieldItem;

enum EETextFormat		{ EE_FORMAT_TEXT = 0x20, EE_FORMAT_RTF, EE_FORMAT_BIN = 0x31, EE_FORMAT_HTML, EE_FORMAT_XML };
enum EEHorizontalTextDirection { EE_HTEXTDIR_DEFAULT, EE_HTEXTDIR_L2R, EE_HTEXTDIR_R2L };
enum EESelectionMode	{ EE_SELMODE_STD, EE_SELMODE_TXTONLY, EE_SELMODE_HIDDEN };
    // EE_SELMODE_HIDDEN can be used to completely hide the selection. This is useful e.g. when you want show the selection
    // only as long as your window (which the edit view works on) has the focus
enum EESpellState		{ EE_SPELL_OK, EE_SPELL_NOLANGUAGE, EE_SPELL_LANGUAGENOTINSTALLED, EE_SPELL_NOSPELLER, EE_SPELL_ERRORFOUND };
enum EVAnchorMode		{
			ANCHOR_TOP_LEFT, 	ANCHOR_VCENTER_LEFT, 	ANCHOR_BOTTOM_LEFT,
			ANCHOR_TOP_HCENTER,	ANCHOR_VCENTER_HCENTER,	ANCHOR_BOTTOM_HCENTER,
			ANCHOR_TOP_RIGHT,	ANCHOR_VCENTER_RIGHT,	ANCHOR_BOTTOM_RIGHT };

#define EE_PARA_NOT_FOUND		0xFFFFFFFF
#define EE_PARA_MAX				0xFFFFFFFF
#define EE_PARA_APPEND			0xFFFFFFFF
#define EE_PARA_ALL				0xFFFFFFFF
#define EE_APPEND				0xFFFF
#define EE_INDEX_NOT_FOUND		0xFFFF
#define EE_INDEX_MAX			0xFFFF

// Fehlermeldungen fuer Read/Write-Methode
#define EE_READWRITE_OK				 (SVSTREAM_OK)
#define EE_READWRITE_WRONGFORMAT	 (SVSTREAM_ERRBASE_USER+1)
#define EE_READWRITE_GENERALERROR	 (SVSTREAM_ERRBASE_USER+2)

#define EDITUNDO_START				100
#define EDITUNDO_REMOVECHARS		100
#define EDITUNDO_CONNECTPARAS		101
#define EDITUNDO_REMOVEFEATURE		102
#define EDITUNDO_MOVEPARAGRAPHS		103
#define EDITUNDO_INSERTFEATURE		104
#define EDITUNDO_SPLITPARA			105
#define EDITUNDO_INSERTCHARS		106
#define EDITUNDO_DELCONTENT			107
#define EDITUNDO_DELETE				108
#define EDITUNDO_CUT				109
#define EDITUNDO_PASTE				110
#define EDITUNDO_INSERT				111
#define EDITUNDO_SRCHANDREPL		112
#define EDITUNDO_MOVEPARAS			113
#define EDITUNDO_PARAATTRIBS		114
#define EDITUNDO_ATTRIBS			115
#define EDITUNDO_DRAGANDDROP		116
#define EDITUNDO_READ				117
#define EDITUNDO_STYLESHEET			118
#define EDITUNDO_REPLACEALL			119
#define EDITUNDO_STRETCH			120
#define EDITUNDO_RESETATTRIBS		121
#define EDITUNDO_INDENTBLOCK		122
#define EDITUNDO_UNINDENTBLOCK		123
#define EDITUNDO_MARKSELECTION		124
#define EDITUNDO_TRANSLITERATE		125
#define EDITUNDO_END				125

#define EDITUNDO_USER				200


#define EE_COMPATIBLEMODE_PARAGRAPHSPACING_SUMMATION			0x0001
#define EE_COMPATIBLEMODE_PARAGRAPHSPACING_BEFOREFIRSTPARAGRAPH	0x0002

class EditView;
class EditEngine;
class ImpEditView;
class ImpEditEngine;
class EditTextObject;
class SfxStyleSheet;

#define RGCHK_NONE			0	// Keine Korrektur der ViusArea beim Scrollen
#define RGCHK_NEG			1	// Keine neg. ViusArea beim Scrollen
#define RGCHK_PAPERSZ1		2	// VisArea muss in Papierbreite,Texthoehe liegen

struct EPosition
{
	sal_uInt32		nPara;
	xub_StrLen	nIndex;

    EPosition() :
        nPara( EE_PARA_NOT_FOUND ),
        nIndex( EE_INDEX_NOT_FOUND )
	{
	}

    EPosition( sal_uInt32 nPara_, xub_StrLen nPos_ ) :
        nPara( nPara_ ),
        nIndex( nPos_ )
	{
	}
};

struct ESelection
{
	sal_uInt32		nStartPara;
	xub_StrLen	nStartPos;
	sal_uInt32		nEndPara;
	xub_StrLen	nEndPos;

    ESelection() : nStartPara( 0 ), nStartPos( 0 ), nEndPara( 0 ), nEndPos( 0 ) {}

    ESelection( sal_uInt32 nStPara, xub_StrLen nStPos, sal_uInt32 nEPara, xub_StrLen nEPos ) :
        nStartPara( nStPara ),
        nStartPos( nStPos ),
        nEndPara( nEPara ),
        nEndPos( nEPos )
	{
	}

    ESelection( sal_uInt32 nPara, xub_StrLen nPos ) :
        nStartPara( nPara ),
        nStartPos( nPos ),
        nEndPara( nPara ),
        nEndPos( nPos )
	{
	}

	void	Adjust();
    sal_Bool    IsEqual( const ESelection& rS ) const;
    sal_Bool    IsLess( const ESelection& rS ) const;
    sal_Bool    IsGreater( const ESelection& rS ) const;
    sal_Bool    IsZero() const;
	sal_Bool	HasRange() const;
};

inline sal_Bool ESelection::HasRange() const
{
	return ( nStartPara != nEndPara ) || ( nStartPos != nEndPos );
}

inline sal_Bool ESelection::IsZero() const
{
	return ( ( nStartPara == 0 ) && ( nStartPos == 0 ) &&
			 ( nEndPara == 0 ) && ( nEndPos == 0 ) );
}

inline sal_Bool ESelection::IsEqual( const ESelection& rS ) const
{
	return ( ( nStartPara == rS.nStartPara ) && ( nStartPos == rS.nStartPos ) &&
			 ( nEndPara == rS.nEndPara ) && ( nEndPos == rS.nEndPos ) );
}

inline sal_Bool ESelection::IsLess( const ESelection& rS ) const
{
	// Selektion muss justiert sein.
	// => Nur pueffen, ob Ende von 'this' < Start von rS

	if ( ( nEndPara < rS.nStartPara ) ||
		 ( ( nEndPara == rS.nStartPara ) && ( nEndPos < rS.nStartPos ) && !IsEqual( rS ) ) )
	{
		return sal_True;
	}
	return sal_False;
}

inline sal_Bool ESelection::IsGreater( const ESelection& rS ) const
{
	// Selektion muss justiert sein.
	// => Nur pueffen, ob Ende von 'this' > Start von rS

	if ( ( nStartPara > rS.nEndPara ) ||
		 ( ( nStartPara == rS.nEndPara ) && ( nStartPos > rS.nEndPos ) && !IsEqual( rS ) ) )
	{
		return sal_True;
	}
	return sal_False;
}

inline void ESelection::Adjust()
{
	sal_Bool bSwap = sal_False;
	if ( nStartPara > nEndPara )
		bSwap = sal_True;
	else if ( ( nStartPara == nEndPara ) && ( nStartPos > nEndPos ) )
		bSwap = sal_True;

	if ( bSwap )
	{
		sal_uInt32 nSPar = nStartPara; sal_uInt16 nSPos = nStartPos;
		nStartPara = nEndPara; nStartPos = nEndPos;
		nEndPara = nSPar; nEndPos = nSPos;
	}
}

struct EDITENG_DLLPUBLIC EFieldInfo
{
    SvxFieldItem*   pFieldItem;
    String          aCurrentText;
    EPosition       aPosition;

    EFieldInfo();
    EFieldInfo( const SvxFieldItem& rFieldItem, sal_uInt32 nPara, sal_uInt16 nPos );
    ~EFieldInfo();

    EFieldInfo( const EFieldInfo& );
    EFieldInfo& operator= ( const EFieldInfo& );
};

// -----------------------------------------------------------------------

enum ImportState {
					RTFIMP_START, RTFIMP_END, 				// nur pParser, nPara, nIndex
					RTFIMP_NEXTTOKEN, RTFIMP_UNKNOWNATTR,	// nToken+nTokenValue
					RTFIMP_SETATTR, 						// pAttrs
					RTFIMP_INSERTTEXT, 						// aText
					RTFIMP_INSERTPARA,						// -
					HTMLIMP_START, HTMLIMP_END, 			// nur pParser, nPara, nIndex
					HTMLIMP_NEXTTOKEN, HTMLIMP_UNKNOWNATTR,	// nToken
					HTMLIMP_SETATTR, 						// pAttrs
					HTMLIMP_INSERTTEXT, 					// aText
					HTMLIMP_INSERTPARA, HTMLIMP_INSERTFIELD	// -
					};

struct ImportInfo
{
	SvParser*				pParser;
	ESelection				aSelection;
	ImportState				eState;

	int 					nToken;
	short 					nTokenValue;

	String					aText;

	void*					pAttrs;	// RTF: SvxRTFItemStackType*, HTML: SfxItemSet*

	ImportInfo( ImportState eState, SvParser* pPrsrs, const ESelection& rSel );
	~ImportInfo();
};

#define EE_SEARCH_WORDONLY		0x0001
#define EE_SEARCH_EXACT			0x0002
#define EE_SEARCH_BACKWARD		0x0004
#define EE_SEARCH_INSELECTION	0x0008
#define EE_SEARCH_REGEXPR		0x0010
#define EE_SEARCH_PATTERN		0x0020

struct ParagraphInfos
{
	ParagraphInfos()
		: nParaHeight( 0 )
		, nLines( 0 )
		, nFirstLineStartX( 0 )
		, nFirstLineOffset( 0 )
		, nFirstLineHeight( 0 )
		, nFirstLineTextHeight ( 0 )
		, nFirstLineMaxAscent( 0 )
		, bValid( 0 )
		{}
	sal_uInt16	nParaHeight;
	sal_uInt16	nLines;

	sal_uInt16	nFirstLineStartX;

	sal_uInt16	nFirstLineOffset;
	sal_uInt16	nFirstLineHeight;
	sal_uInt16	nFirstLineTextHeight;
	sal_uInt16	nFirstLineMaxAscent;

	sal_Bool	bValid;	// Bei einer Abfrage waehrend der Formatierung ungueltig!
};

struct EECharAttrib
{
	const SfxPoolItem*	pAttr;

	sal_uInt32				nPara;
	xub_StrLen			nStart;
	xub_StrLen			nEnd;
};

SV_DECL_VARARR_VISIBILITY( EECharAttribArray, EECharAttrib, 0, 4, EDITENG_DLLPUBLIC )

struct MoveParagraphsInfo
{
    sal_uInt32  nStartPara;
    sal_uInt32  nEndPara;
    sal_uInt32  nDestPara;

    MoveParagraphsInfo( sal_uInt32 nS, sal_uInt32 nE, sal_uInt32 nD )
        { nStartPara = nS; nEndPara = nE; nDestPara = nD; }
};

#define EE_ACTION_PASTE 1
#define EE_ACTION_DROP  2

struct PasteOrDropInfos
{
    sal_uInt16  nAction;
    sal_uInt32  nStartPara;
    sal_uInt32  nEndPara;

    PasteOrDropInfos() : nAction(0), nStartPara(EE_PARA_MAX), nEndPara(EE_PARA_MAX)  {}
};

enum EENotifyType
{
    /// EditEngine text was modified
    EE_NOTIFY_TEXTMODIFIED,

    /// A paragraph was inserted into the EditEngine
    EE_NOTIFY_PARAGRAPHINSERTED,

    /// A paragraph was removed from the EditEngine
    EE_NOTIFY_PARAGRAPHREMOVED,

    /// Multiple paragraphs have been removed from the EditEngine
    EE_NOTIFY_PARAGRAPHSMOVED,

    /// The height of at least one paragraph has changed
    EE_NOTIFY_TEXTHEIGHTCHANGED,

    /// The view area of the EditEngine scrolled
    EE_NOTIFY_TEXTVIEWSCROLLED,

    /// The selection and/or the cursor position has changed
    EE_NOTIFY_TEXTVIEWSELECTIONCHANGED,

    /** Denotes the beginning of a collected amount of EditEngine
        notification events. This event itself is not queued, but sent
        immediately
     */
    EE_NOTIFY_BLOCKNOTIFICATION_START,

    /** Denotes the end of a collected amount of EditEngine
        notification events. After this event, the queue is empty, and
        a high-level operation such as "insert paragraph" is finished
     */
    EE_NOTIFY_BLOCKNOTIFICATION_END,

    /// Denotes the beginning of a high-level action triggered by a key press
    EE_NOTIFY_INPUT_START,

    /// Denotes the end of a high-level action triggered by a key press
    EE_NOTIFY_INPUT_END,
	EE_NOTIFY_TEXTVIEWSELECTIONCHANGED_ENDD_PARA
};

struct EENotify
{
    EENotifyType    eNotificationType;
    EditEngine*     pEditEngine;
    EditView*       pEditView;

    sal_uInt32          nParagraph; // only valid in PARAGRAPHINSERTED/EE_NOTIFY_PARAGRAPHREMOVED

    sal_uInt32          nParam1;
    sal_uInt32          nParam2;

    EENotify( EENotifyType eType )
        { eNotificationType = eType; pEditEngine = NULL; pEditView = NULL; nParagraph = EE_PARA_NOT_FOUND; nParam1 = 0; nParam2 = 0; }
};

#endif // _MyEDITDATA_HXX
