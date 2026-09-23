// Finding one element's attributes in a server-rendered page.
//
// The meal-plan page is the only HTML this app still reads, and since it
// became a Vue app it carries no figures at all — only who to look up, on its
// mount element:
//
//     <cu-container id="app" data-target-id="0000000" data-target-card="">
//
// So this is not an HTML parser, and must not grow into one. It walks the start
// tags of a page in document order and returns the attributes of the first one
// that carries a given attribute. It knows exactly as much HTML as that needs:
//
// * comments, doctypes and processing instructions are skipped;
// * `<script>` and `<style>` bodies are skipped, so a tag inside a JavaScript
//   string never counts (the live page is full of both);
// * attribute names are lower-cased, values may be double-, single- or
//   unquoted, a bare attribute has the value "", and character references in
//   values are decoded.

#pragma once

#include <QHash>
#include <QString>

#include <optional>

namespace mycu::html {

using Attributes = QHash<QString, QString>;

// The attributes of the first element, in document order, that has
// `attribute`; nothing if no element does. Never throws on malformed markup.
std::optional<Attributes> firstElementWith(const QString &page, const QString &attribute);

} // namespace mycu::html
