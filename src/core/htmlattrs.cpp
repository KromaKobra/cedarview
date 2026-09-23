#include "htmlattrs.h"

namespace mycu::html {

namespace {

bool isSpace(QChar c)
{
    return c == u' ' || c == u'\t' || c == u'\n' || c == u'\r' || c == u'\f';
}

// `&amp;`, `&quot;`, `&#39;`, `&#x27;`, `&nbsp;` and friends. Anything not
// recognised is left as written, which is what a browser does too.
QString decodeEntities(const QString &text)
{
    if (!text.contains(u'&'))
        return text;

    static const QHash<QString, QString> named = {
        {QStringLiteral("amp"), QStringLiteral("&")},
        {QStringLiteral("lt"), QStringLiteral("<")},
        {QStringLiteral("gt"), QStringLiteral(">")},
        {QStringLiteral("quot"), QStringLiteral("\"")},
        {QStringLiteral("apos"), QStringLiteral("'")},
        {QStringLiteral("nbsp"), QStringLiteral(" ")},
    };

    QString out;
    out.reserve(text.size());
    qsizetype i = 0;
    while (i < text.size()) {
        if (text[i] != u'&') {
            out += text[i++];
            continue;
        }
        const qsizetype semi = text.indexOf(u';', i + 1);
        if (semi < 0 || semi - i > 12) {
            out += text[i++];
            continue;
        }
        const QString name = text.mid(i + 1, semi - i - 1);
        QString replacement;
        if (name.startsWith(u'#')) {
            bool ok = false;
            const uint code = (name.size() > 1 && (name[1] == u'x' || name[1] == u'X'))
                ? name.mid(2).toUInt(&ok, 16)
                : name.mid(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF) {
                const char32_t cp = code;
                replacement = QString::fromUcs4(&cp, 1);
            }
        } else {
            replacement = named.value(name);
        }
        if (replacement.isEmpty()) {
            out += text[i++];
            continue;
        }
        out += replacement;
        i = semi + 1;
    }
    return out;
}

} // namespace

std::optional<Attributes> firstElementWith(const QString &page, const QString &attribute)
{
    const QString wanted = attribute.toLower();
    const qsizetype n = page.size();
    qsizetype i = 0;

    while (i < n) {
        const qsizetype lt = page.indexOf(u'<', i);
        if (lt < 0 || lt + 1 >= n)
            break;

        // Comments run to the first "-->", whatever is inside them.
        if (page.mid(lt, 4) == u"<!--") {
            const qsizetype end = page.indexOf(QStringLiteral("-->"), lt + 4);
            if (end < 0)
                break;
            i = end + 3;
            continue;
        }
        // <!DOCTYPE …>, <?xml …?>, and end tags carry nothing we want.
        const QChar next = page[lt + 1];
        if (next == u'!' || next == u'?' || next == u'/') {
            const qsizetype end = page.indexOf(u'>', lt + 1);
            if (end < 0)
                break;
            i = end + 1;
            continue;
        }
        if (!next.isLetter()) {
            i = lt + 1; // a bare "<" in text
            continue;
        }

        // The tag name.
        qsizetype p = lt + 1;
        while (p < n && !isSpace(page[p]) && page[p] != u'>' && page[p] != u'/')
            ++p;
        const QString tag = page.mid(lt + 1, p - lt - 1).toLower();

        // The attributes, up to the closing '>'.
        Attributes attrs;
        while (p < n) {
            while (p < n && (isSpace(page[p]) || page[p] == u'/'))
                ++p;
            if (p >= n || page[p] == u'>')
                break;

            const qsizetype nameStart = p;
            while (p < n && !isSpace(page[p]) && page[p] != u'=' && page[p] != u'>'
                   && !(page[p] == u'/' && p + 1 < n && page[p + 1] == u'>'))
                ++p;
            const QString name = page.mid(nameStart, p - nameStart).toLower();

            while (p < n && isSpace(page[p]))
                ++p;
            QString value;
            if (p < n && page[p] == u'=') {
                ++p;
                while (p < n && isSpace(page[p]))
                    ++p;
                if (p < n && (page[p] == u'"' || page[p] == u'\'')) {
                    const QChar quote = page[p];
                    const qsizetype close = page.indexOf(quote, p + 1);
                    const qsizetype end = close < 0 ? n : close;
                    value = page.mid(p + 1, end - p - 1);
                    p = close < 0 ? n : close + 1;
                } else {
                    const qsizetype valueStart = p;
                    while (p < n && !isSpace(page[p]) && page[p] != u'>')
                        ++p;
                    value = page.mid(valueStart, p - valueStart);
                }
            }
            if (!name.isEmpty() && !attrs.contains(name))
                attrs.insert(name, decodeEntities(value));
        }
        if (p >= n)
            break; // an unterminated tag at the end of the page
        i = p + 1;

        if (attrs.contains(wanted))
            return attrs;

        // Script and style bodies are text, not markup.
        if (tag == u"script" || tag == u"style") {
            const qsizetype close = page.indexOf(QStringLiteral("</") + tag, i, Qt::CaseInsensitive);
            if (close < 0)
                break;
            i = close;
        }
    }
    return std::nullopt;
}

} // namespace mycu::html
