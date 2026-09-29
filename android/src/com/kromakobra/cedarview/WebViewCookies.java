// Sign-out's cookie wipe, called from src/platform/android.cpp over JNI.
//
// The federated logout ends the Entra session but not Self-Service's own: its
// session cookie outlives the round trip, and the app relaunches signed in.
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
