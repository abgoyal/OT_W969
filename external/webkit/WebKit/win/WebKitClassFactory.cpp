

#include "config.h"
#include "WebKitDLL.h"
#include "WebKitClassFactory.h"

#include "CFDictionaryPropertyBag.h"
#include "ForEachCoClass.h"
#include "WebArchive.h"
#include "WebCache.h"
#include "WebCookieManager.h"
#include "WebCoreStatistics.h"
#include "WebDatabaseManager.h"
#include "WebDownload.h"
#include "WebError.h"
#include "WebFrame.h"
#include "WebGeolocationPosition.h"
#include "WebHistory.h"
#include "WebHistoryItem.h"
#include "WebIconDatabase.h"
#include "WebJavaScriptCollector.h"
#include "WebKit.h"
#include "WebKitStatistics.h"
#include "WebMutableURLRequest.h"
#include "WebNotificationCenter.h"
#include "WebPreferences.h"
#include "WebScriptWorld.h"
#include "WebScrollBar.h"
#include "WebSerializedJSValue.h"
#include "WebTextRenderer.h"
#include "WebURLCredential.h"
#include "WebURLProtectionSpace.h"
#include "WebURLResponse.h"
#include "WebView.h"
#include "WebWorkersPrivate.h"
#pragma warning(push, 0)
#include <JavaScriptCore/InitializeThreading.h>
#include <WebCore/FontDatabase.h>
#include <WebCore/SoftLinking.h>
#pragma warning(pop)

// WebKitClassFactory ---------------------------------------------------------
#if USE(SAFARI_THEME)
#ifdef DEBUG_ALL
SOFT_LINK_DEBUG_LIBRARY(SafariTheme)
#else
SOFT_LINK_LIBRARY(SafariTheme)
#endif

SOFT_LINK(SafariTheme, STInitialize, void, APIENTRY, (), ())
#endif

WebKitClassFactory::WebKitClassFactory(CLSID targetClass)
: m_targetClass(targetClass)
, m_refCount(0)
{
#if USE(SAFARI_THEME)
    static bool didInitializeSafariTheme;
    if (!didInitializeSafariTheme) {
        if (SafariThemeLibrary())
            STInitialize();
        didInitializeSafariTheme = true;
    }
#endif

    JSC::initializeThreading();
    WebCore::populateFontDatabase();

    gClassCount++;
    gClassNameCount.add("WebKitClassFactory");
}

WebKitClassFactory::~WebKitClassFactory()
{
    gClassCount--;
    gClassNameCount.remove("WebKitClassFactory");
}

// IUnknown -------------------------------------------------------------------

HRESULT STDMETHODCALLTYPE WebKitClassFactory::QueryInterface(REFIID riid, void** ppvObject)
{
    *ppvObject = 0;
    if (IsEqualGUID(riid, IID_IUnknown))
        *ppvObject = static_cast<IUnknown*>(this);
    else if (IsEqualGUID(riid, IID_IClassFactory))
        *ppvObject = static_cast<IClassFactory*>(this);
    else
        return E_NOINTERFACE;

    AddRef();
    return S_OK;
}

ULONG STDMETHODCALLTYPE WebKitClassFactory::AddRef(void)
{
    return ++m_refCount;
}

ULONG STDMETHODCALLTYPE WebKitClassFactory::Release(void)
{
    ULONG newRef = --m_refCount;
    if (!newRef && !gLockCount)
        delete(this);

    return newRef;
}

// FIXME: Remove these functions once all createInstance() functions return COMPtr.
template <typename T>
static T* releaseRefFromCreateInstance(T* object)
{
    return object;
}

template <typename T>
static T* releaseRefFromCreateInstance(COMPtr<T> object)
{
    return object.releaseRef();
}

// IClassFactory --------------------------------------------------------------

HRESULT STDMETHODCALLTYPE WebKitClassFactory::CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppvObject)
{
    IUnknown* unknown = 0;
    *ppvObject = 0;
    
    if (pUnkOuter)
        return CLASS_E_NOAGGREGATION;

#define INITIALIZE_IF_CLASS(cls) \
    if (IsEqualGUID(m_targetClass, CLSID_##cls)) \
        unknown = static_cast<I##cls*>(releaseRefFromCreateInstance(cls::createInstance())); \
    else \
    // end of macro

    // These #defines are needed to appease the INITIALIZE_IF_CLASS macro.
    // There is no ICFDictionaryPropertyBag, we use IPropertyBag instead.
#define ICFDictionaryPropertyBag IPropertyBag
    // There is no IWebScrollBar, we only have IWebScrollBarPrivate.
#define IWebScrollBar IWebScrollBarPrivate
    // There is no class called WebURLRequest -- WebMutableURLRequest implements it for us.
#define WebURLRequest WebMutableURLRequest

    FOR_EACH_COCLASS(INITIALIZE_IF_CLASS)
        // This is the final else case
        return CLASS_E_CLASSNOTAVAILABLE;

#undef ICFDictionaryPropertyBag
#undef IWebScrollBar
#undef WebURLRequest
#undef INITIALIZE_IF_CLASS

    if (!unknown)
        return E_OUTOFMEMORY;

    HRESULT hr = unknown->QueryInterface(riid, ppvObject);
    if (FAILED(hr))
        delete unknown;
    else
        unknown->Release(); // both createInstance() and QueryInterface() added refs

    return hr;
}

HRESULT STDMETHODCALLTYPE WebKitClassFactory::LockServer(BOOL fLock)
{
    if (fLock)
        gLockCount++;
    else
        gLockCount--;

    return S_OK;
}