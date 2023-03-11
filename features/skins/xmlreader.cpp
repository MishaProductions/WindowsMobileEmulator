/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.

--*/

#include <string>
#include <sstream>

#include "XMLReader.h"
#include "WideUtils.h"
#include "resource.h"

using namespace std;

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

XMLReader::XMLReader(const wchar_t * inFileName, TagInfo* inTags, int inNumTags, void* obj) : m_xmlReader( NULL )
{
	m_error = NO_ERROR;

	if (obj == NULL || inTags == NULL || inNumTags == 0 )
	{
		fatalError(	NULL, NULL, 0xFFFF, NULL);
		m_error = E_FAIL;
		return;
	}
	
	m_NumTags = inNumTags;
	m_Tags = inTags;

	builtObject = obj;

	// Convert filename to a wide character string
	int fileNameLength = (int)wcslen(inFileName);
	m_URL = new wchar_t[fileNameLength + 1];
	if ( m_URL == NULL )
	{
		fatalError(	NULL, ID_MESSAGE_RESOURCE_EXHAUSTED, 0, NULL);
		m_error = E_FAIL;
	}
	if (FAILED(StringCchCopyW(m_URL, fileNameLength + 1, inFileName))) {
		ASSERT(FALSE);
		m_error = E_FAIL;
	}
	
	// Initialize COM stuff
	if (FAILED(CoInitializeEx(NULL, COINIT_MULTITHREADED|COINIT_DISABLE_OLE1DDE)))
	{
		fatalError(	NULL, sSkinEngineFailedToOpenCOMObjectError, 0, NULL);
		m_error = E_FAIL;
	}


	HRESULT hr = ::CoCreateInstance(
								CLSID_SAXXMLReader,
								NULL,
								CLSCTX_ALL,
								IID_ISAXXMLReader,
								(void **)&m_xmlReader);

	if(FAILED(hr))
	{
		fatalError(	NULL, sSkinEngineFailedToOpenCOMObjectError, 0, NULL);
		m_error = E_FAIL;
	}
	else
	{
		m_xmlReader->putContentHandler(this);
		m_xmlReader->putErrorHandler(this);
	}
}


/*------------------------------------------------------------------
	Destructor
------------------------------------------------------------------*/

XMLReader::~XMLReader()
{
	if( m_xmlReader != NULL )
		m_xmlReader->Release();
	
	if( m_URL != NULL )
		::delete m_URL;

	CoUninitialize();
}


/*------------------------------------------------------------------
	Parse
------------------------------------------------------------------*/

void XMLReader::Parse( void )
{
	HRESULT hr = m_xmlReader->parseURL( m_URL );
	// Check if the file is missing or not accessible to provide a more informative error message
	if( hr == E_ACCESSDENIED || hr == INET_E_OBJECT_NOT_FOUND || hr == INET_E_DOWNLOAD_FAILURE)
	{
		fatalError( NULL, sSkinEngineXMLFileNotAccessible, 0xFFFF, m_URL);
	}
	else if (FAILED(hr))
		m_error = hr;
}


/*------------------------------------------------------------------
	startElement
------------------------------------------------------------------*/

HRESULT STDMETHODCALLTYPE XMLReader::startElement(
	/* [in] */ const wchar_t * /*nameSpace*/,
	/* [in] */ int /*nameSpaceLength*/,
	/* [in] */ const wchar_t * tag,
	/* [in] */ int /*tagLength*/,
	/* [in] */ const wchar_t * /*raw*/,
	/* [in] */ int /*rawLength*/,
	/* [in] */ ISAXAttributes * attributes)
{
	int		index;
	for (index = 0;  index < m_NumTags   &&  _wcsicmp(tag, m_Tags[index].name);  index++)	{}		// Finds the index of the correct handler

	if ( index < m_NumTags )
	{
		if( m_Tags[index].Handler != NULL )
			m_Tags[index].Handler(builtObject, attributes);
	}
	else
	{
		fatalError(	NULL, sSkinEngineInvalidTag, 0, (wchar_t *)tag);
		return S_FALSE;
	}

	return S_OK;
}


/*------------------------------------------------------------------
	fatalError
------------------------------------------------------------------*/

HRESULT STDMETHODCALLTYPE XMLReader::fatalError(
	ISAXLocator*           pLocator,
	unsigned int           messageID,
	HRESULT                errno_t,
	__in_z_opt const wchar_t * inStr
)
{
	if( messageID ) // NULL specifies an internal error. Not to be reported to user.
	{
		// Add line number if we can
		int lineNumber;
		wchar_t lineNumberBuffer[10]= L"";
		wchar_t lineLabelBuffer[MAX_LOADSTRING]= L"";

		if ( pLocator != NULL &&
			pLocator->getLineNumber( &lineNumber ) == NO_ERROR )
		{
			Configuration.Resources.getString(sSkinEngineLineLabel, lineLabelBuffer );
			if (FAILED(StringCchPrintfW(lineNumberBuffer, ARRAY_SIZE(lineNumberBuffer), L"%d", lineNumber)))
			{
				ShowDialog(ID_MESSAGE_INTERNAL_ERROR);
				m_error = errno_t;
				return errno_t;
			}
		}

		if (inStr)
			ShowDialog(messageID, m_URL, lineLabelBuffer, lineNumberBuffer, inStr);
		else
			ShowDialog(messageID, m_URL, lineLabelBuffer, lineNumberBuffer );
	}
	else
		ShowDialog(ID_MESSAGE_INTERNAL_ERROR);

	m_error = errno_t;
	return errno_t;
}


/*------------------------------------------------------------------
	ignorableWarning
------------------------------------------------------------------*/

HRESULT STDMETHODCALLTYPE XMLReader::ignorableWarning(
            /* [in] */ ISAXLocator * /*pLocator*/,
            /* [in] */ const wchar_t * /*pwchErrorMessage*/,
			/* [in] */ HRESULT /*errno_t*/)
{
	// Ignore ignorable warning
	return S_OK;
}
