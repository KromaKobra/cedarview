// HTTPS GETs over the platform's own stack, called from C++ over JNI.
//
// get() is a plain request, for the public services (the dining menu and the
// chapel schedule), from src/core/httptransport.cpp.
//
// getWithWebViewCookies() is the Self-Service request, from
// src/platform/android_sessiontransport.cpp. It carries the WebView's session
// cookies, taken from android.webkit.CookieManager, which is shared by the
// whole process and, unlike Qt's cookie API, includes HttpOnly cookies.
//
// Why Java rather than Qt Network: Qt has no native TLS backend on Android, so
// QNetworkAccessManager there would need an OpenSSL bundled into the APK — and
// a CA list bundled with it, which goes stale and would keep trusting a CA the
// phone's owner had distrusted. HttpsURLConnection is the platform's own
// stack, using the phone's own trust store, so "trusted" here means exactly
// what it means everywhere else on the device.
//
// Never throws: every outcome comes back as {status, finalUrl, contentType,
// body}, and a request that never completed has status "-1" with the reason in
// the body slot. That keeps exceptions from ever crossing into native code.
package com.kromakobra.cedarview;

import android.content.Context;
import android.webkit.CookieManager;
import android.webkit.WebSettings;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;

public final class HttpGet
{
    private HttpGet() {}

    public static String[] get(String url, String userAgent, String accept, int timeoutMs)
    {
        HttpURLConnection connection = null;
        try {
            connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setInstanceFollowRedirects(true);
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setRequestProperty("User-Agent", userAgent);
            connection.setRequestProperty("Accept", accept);

            final int status = connection.getResponseCode();
            final InputStream stream =
                status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            final String contentType = connection.getContentType();
            final String body = stream == null ? "" : read(stream, charsetOf(contentType));

            return new String[] {
                Integer.toString(status),
                connection.getURL().toString(),
                contentType == null ? "" : contentType,
                body,
            };
        } catch (Exception e) {
            return new String[] { "-1", "", "", String.valueOf(e) };
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }

    // At most this many redirects within the one host.
    private static final int MAX_REDIRECTS = 10;

    private static String s_userAgent;

    // The same GET as get(), but carrying the WebView's cookies for each URL
    // and writing back any cookie the server sets, so the WebView and this
    // request share one session.
    //
    // Redirects are followed by hand and only within the starting host. Where
    // one leads anywhere else (Microsoft's sign-in, when the session has
    // ended), the request stops there without following it. That hop's status
    // and URL come back with an empty body, and the caller reads the URL to
    // tell that the session has ended. Cookies are only ever sent where
    // CookieManager says they belong.
    public static String[] getWithWebViewCookies(Context context, String url, String accept, int timeoutMs)
    {
        HttpURLConnection connection = null;
        try {
            final CookieManager cookies = CookieManager.getInstance();
            final String host = new URL(url).getHost();
            String current = url;
            for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
                connection = (HttpURLConnection) new URL(current).openConnection();
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(timeoutMs);
                connection.setReadTimeout(timeoutMs);
                connection.setRequestProperty("User-Agent", userAgent(context));
                connection.setRequestProperty("Accept", accept);
                final String cookie = cookies.getCookie(current);
                if (cookie != null && !cookie.isEmpty())
                    connection.setRequestProperty("Cookie", cookie);

                final int status = connection.getResponseCode();
                storeCookies(cookies, current, connection.getHeaderFields());

                final String location = connection.getHeaderField("Location");
                if (status >= 300 && status < 400 && location != null) {
                    final URL next = new URL(new URL(current), location);
                    connection.disconnect();
                    connection = null;
                    if (!"https".equals(next.getProtocol()) || !host.equalsIgnoreCase(next.getHost()))
                        return new String[] { Integer.toString(status), next.toString(), "", "" };
                    current = next.toString();
                    continue;
                }

                final InputStream stream =
                    status >= 400 ? connection.getErrorStream() : connection.getInputStream();
                final String contentType = connection.getContentType();
                final String body = stream == null ? "" : read(stream, charsetOf(contentType));
                return new String[] {
                    Integer.toString(status),
                    current,
                    contentType == null ? "" : contentType,
                    body,
                };
            }
            return new String[] { "-1", "", "", "more than " + MAX_REDIRECTS + " redirects" };
        } catch (Exception e) {
            return new String[] { "-1", "", "", String.valueOf(e) };
        } finally {
            if (connection != null)
                connection.disconnect();
        }
    }

    // The WebView's own User-Agent, so these requests look like the browser
    // the session was signed in with.
    private static synchronized String userAgent(Context context)
    {
        if (s_userAgent == null)
            s_userAgent = WebSettings.getDefaultUserAgent(context);
        return s_userAgent;
    }

    private static void storeCookies(CookieManager cookies, String url, Map<String, List<String>> headers)
    {
        boolean stored = false;
        for (Map.Entry<String, List<String>> header : headers.entrySet()) {
            if (header.getKey() == null || !header.getKey().equalsIgnoreCase("Set-Cookie"))
                continue;
            for (String value : header.getValue()) {
                cookies.setCookie(url, value);
                stored = true;
            }
        }
        if (stored)
            cookies.flush();
    }

    // The charset the server named, UTF-8 otherwise.
    private static Charset charsetOf(String contentType)
    {
        if (contentType != null) {
            for (String part : contentType.split(";")) {
                String p = part.trim();
                if (p.toLowerCase().startsWith("charset=")) {
                    try {
                        return Charset.forName(p.substring(8).replace("\"", "").trim());
                    } catch (Exception ignored) {
                        break;
                    }
                }
            }
        }
        return StandardCharsets.UTF_8;
    }

    private static String read(InputStream stream, Charset charset) throws java.io.IOException
    {
        try (InputStream in = stream) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[16384];
            int n;
            while ((n = in.read(buffer)) > 0)
                out.write(buffer, 0, n);
            return new String(out.toByteArray(), charset);
        }
    }
}
