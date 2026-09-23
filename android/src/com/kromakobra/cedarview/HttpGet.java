// A plain HTTPS GET, for the public services (the dining menu and the chapel
// schedule). Called from C++ over JNI by src/core/httptransport.cpp.
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

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;

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
