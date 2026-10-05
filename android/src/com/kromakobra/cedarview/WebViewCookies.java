// Sign-out's cookie wipe, called from src/platform/android.cpp over JNI.
//
// Sign-out is this and nothing more: Self-Service's session cookie and
// Microsoft's both go, so the next sign-in starts from nothing.
// android.webkit.CookieManager is the WebView's jar, shared by the whole
// process, so emptying it here is the Android equivalent of the desktop's
// deleteAllCookies().
package com.kromakobra.cedarview;

import android.webkit.CookieManager;

public final class WebViewCookies
{
    private WebViewCookies() {}

    public static void clear()
    {
        final CookieManager cookies = CookieManager.getInstance();
        // Asynchronous, but queued ahead of any request the WebView or
        // HttpGet makes afterwards. A null callback lets it be called from a
        // thread with no Looper.
        cookies.removeAllCookies(null);
        cookies.flush();
    }
}
