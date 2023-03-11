/*++

 Copyright (c) 2006 Microsoft Corporation.  All rights reserved.

 The use and distribution terms for this software are contained in the file
 named license.rtf, which can be found in the root of this distribution.
 By using this software in any fashion, you are agreeing to be bound by the
 terms of this license.

 You must not remove this notice, or any other, from this software.


--*/

#if !defined(__XML_READER_H__)
#define __XML_READER_H__

#pragma once

#include "emulator.h"
#include "Config.h"

#include "SAXContentHandlerImpl.h"
#include "SAXErrorHandlerImpl.h"
#include <string>
#pragma warning (disable:4702)  // There is unreachable code in vector
#include <vector>
#pragma warning (default:4702)

typedef int		XMLReaderFatalError;

// XML Tag info...
struct TagInfo
{
	wchar_t*	name;								// Reference to name of hte tag
	void		(*Handler)(void*, ISAXAttributes*);	// Handler to process attributes for tag
};

// XML Tag attribute info...
struct AttributeInfo
{
	wchar_t*	name;
	void		(*Handler)(void*, __in_z const wchar_t*, __in_opt wchar_t*);
};


class XMLReader : public SAXContentHandlerImpl, public SAXErrorHandlerImpl
{
public:
    XMLReader(const wchar_t * inFileName, TagInfo* inTags, int inNumTags, void* obj);
    virtual ~XMLReader();

    void Parse( void );

    HRESULT GetError( void )
    {
    	return m_error;
    }

	HRESULT STDMETHODCALLTYPE startElement(
            /* [in] */ const wchar_t __RPC_FAR *nameSpace,
            /* [in] */ int nameSpaceLength,
            /* [in] */ const wchar_t __RPC_FAR *tag,
            /* [in] */ int tagLength,
            /* [in] */ const wchar_t __RPC_FAR *raw,
            /* [in] */ int rawLength,
            /* [in] */ ISAXAttributes __RPC_FAR *attributes);

	HRESULT STDMETHODCALLTYPE fatalError(
            /* [in] */ ISAXLocator __RPC_FAR *pLocator,
            /* [in] */ unsigned int messageID,
            /* [in] */ HRESULT errCode,
            /* [in] */ __in_z_opt const wchar_t * inStr);


	HRESULT STDMETHODCALLTYPE ignorableWarning(
            /* [in] */ ISAXLocator __RPC_FAR *pLocator,
            /* [in] */ const wchar_t * pwchErrorMessage,
			/* [in] */ HRESULT errCode);

	void*	builtObject;

	std::vector< std::wstring >		mInvalidTags;
	std::vector< std::wstring >		mParseWarnings;
	std::wstring					mFatalErrorMessage;
	
	TagInfo*				m_Tags;
	int						m_NumTags;
	
	ISAXXMLReader* 			m_xmlReader;
	wchar_t* 				m_URL;
	
	HRESULT					m_error;
};

#endif // !defined(__XML_READER_H__)

