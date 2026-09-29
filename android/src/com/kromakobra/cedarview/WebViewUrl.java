// The sign-in WebView's real address, read straight from android.webkit.
//
// QtWebView reports only the first page of each load. Its WebViewClient
// forwards onPageStarted when a start counter reaches one, and onPageFinished
// resets the counter. So when a load passes through several pages before it
// finishes (Duo's verify page posts to Microsoft, which posts back to
// Self-Service, which redirects to the dashboard), Qt tells us about Duo, then
// nothing until the dashboard has finished loading every image and script.
// The dashboard is on screen that whole time. WebView.getUrl() changes the
// moment each page commits, so src/platform/android_urlprobe.cpp polls this
// while the sign-in surface is showing.
//
// Must run on the Android UI thread, which owns the WebView. Returns "" when
// there is no WebView on screen: Qt removes it from the view tree whenever the
// surface is hidden.
package com.kromakobra.cedarview;

import android.app.Activity;
import android.content.Context;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.webkit.WebView;

public final class WebViewUrl
{
    private WebViewUrl() {}

    public static String current(Context context)
    {
        if (!(context instanceof Activity))
            return "";
        final Window window = ((Activity) context).getWindow();
        if (window == null)
            return "";
        final WebView view = find(window.getDecorView());
        if (view == null)
            return "";
        final String url = view.getUrl();
        return url == null ? "" : url;
    }

    private static WebView find(View view)
    {
        if (view instanceof WebView && view.isShown())
            return (WebView) view;
        if (view instanceof ViewGroup) {
            final ViewGroup group = (ViewGroup) view;
            for (int i = 0; i < group.getChildCount(); ++i) {
                final WebView found = find(group.getChildAt(i));
                if (found != null)
                    return found;
            }
        }
        return null;
    }
}
