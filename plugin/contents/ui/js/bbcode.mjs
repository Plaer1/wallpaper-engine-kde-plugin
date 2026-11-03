const MAX_DESCRIPTION_BYTES = 128 * 1024;
const MAX_NESTING = 32;
const TAG = /^\[(\/)?([a-z][a-z0-9]*|\*)(?:=([^\]\r\n]*))?\]/i;
const OPEN_TAGS = {
    b: ["<strong>", "</strong>"],
    i: ["<em>", "</em>"],
    u: ["<u>", "</u>"],
    p: ["<p>", "</p>"],
    h1: ["<h1>", "</h1>"],
    h2: ["<h2>", "</h2>"],
    h3: ["<h3>", "</h3>"],
    list: ["<ul>", "</ul>"],
    li: ["<li>", "</li>"],
    "*": ["<li>", "</li>"]
};

function escapeText(value) {
    return String(value).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function escapeAttribute(value) {
    return escapeText(value).replace(/"/g, "&quot;").replace(/'/g, "&#39;");
}

function safeUrl(value, allowMailto = true) {
    const url = String(value).trim();
    if (/[\u0000-\u0020<>"'`\\]/.test(url))
        return "";
    if (/^https?:\/\/[^\s]+$/i.test(url))
        return url;
    if (allowMailto && /^mailto:[^\s@]+@[^\s@]+$/i.test(url))
        return url;
    return "";
}

function utf8Length(value) {
    let bytes = 0;
    for (let i = 0; i < value.length; i++) {
        const code = value.charCodeAt(i);
        if (code <= 0x7f) bytes++;
        else if (code <= 0x7ff) bytes += 2;
        else if (code >= 0xd800 && code <= 0xdbff && i + 1 < value.length
                 && value.charCodeAt(i + 1) >= 0xdc00 && value.charCodeAt(i + 1) <= 0xdfff) {
            bytes += 4;
            i++;
        } else bytes += 3;
    }
    return bytes;
}

function truncateUtf8(value, byteLimit) {
    let bytes = 0;
    let end = 0;
    for (let i = 0; i < value.length; i++) {
        const code = value.charCodeAt(i);
        let width;
        let chars = 1;
        if (code <= 0x7f) width = 1;
        else if (code <= 0x7ff) width = 2;
        else if (code >= 0xd800 && code <= 0xdbff && i + 1 < value.length
                 && value.charCodeAt(i + 1) >= 0xdc00 && value.charCodeAt(i + 1) <= 0xdfff) {
            width = 4;
            chars = 2;
        } else width = 3;
        if (bytes + width > byteLimit)
            break;
        bytes += width;
        i += chars - 1;
        end = i + 1;
    }
    return value.slice(0, end);
}

function parseMarkup(source) {
    const lowercaseSource = source.toLowerCase();

    function parseFrom(start, expectedClose, inLink, depth) {
        let html = "";
        let position = start;
        while (position < source.length) {
            const bracket = source.indexOf("[", position);
            const end = bracket < 0 ? source.length : bracket;
            if (end > position)
                html += escapeText(source.slice(position, end)).replace(/\r\n|\r|\n/g, "<br/>");
            if (bracket < 0)
                return {html, position: source.length, closed: false};

            const match = TAG.exec(source.slice(bracket));
            if (!match) {
                html += "[";
                position = bracket + 1;
                continue;
            }
            const [token, slash, rawName, attribute] = match;
            const name = rawName.toLowerCase();
            const tokenEnd = bracket + token.length;
            if (slash) {
                if (expectedClose === name)
                    return {html, position: tokenEnd, closeStart: bracket, closed: true};
                html += escapeText(token);
                position = tokenEnd;
                continue;
            }

            if (name === "br" && attribute === undefined) {
                html += "<br/>";
                position = tokenEnd;
                continue;
            }
            if (name === "img") {
                let imageUrl = attribute;
                let imageEnd = tokenEnd;
                if (imageUrl === undefined) {
                    const closeAt = lowercaseSource.indexOf("[/img]", tokenEnd);
                    if (closeAt < 0) {
                        html += escapeText(token);
                        position = tokenEnd;
                        continue;
                    }
                    imageUrl = source.slice(tokenEnd, closeAt).trim();
                    imageEnd = closeAt + 6;
                }
                const safeImage = safeUrl(imageUrl, false);
                if (safeImage)
                    html += `<a href="${escapeAttribute(safeImage)}">Image: ${escapeText(safeImage)}</a>`;
                else
                    html += escapeText(source.slice(bracket, imageEnd));
                position = imageEnd;
                continue;
            }

            const isLink = name === "url" || name === "email";
            const openTag = OPEN_TAGS[name];
            if ((!openTag && !isLink) || (isLink && inLink)) {
                html += escapeText(token);
                position = tokenEnd;
                continue;
            }
            if (depth >= MAX_NESTING) {
                html += escapeText(token);
                position = tokenEnd;
                continue;
            }

            let closeName = name;
            if (name === "email") closeName = "email";
            const inner = parseFrom(tokenEnd, closeName, inLink || isLink, depth + 1);
            if (!inner.closed) {
                html += escapeText(token) + inner.html;
                position = inner.position;
                continue;
            }

            if (isLink) {
                let href = attribute === undefined ? source.slice(tokenEnd, inner.closeStart).trim() : attribute;
                let labelHtml = inner.html;
                if (name === "email" && !/^mailto:/i.test(href)) href = `mailto:${href}`;
                href = safeUrl(href, true);
                if (!href) {
                    html += escapeText(token) + inner.html
                        + escapeText(source.slice(inner.closeStart, inner.position));
                } else {
                    if (!labelHtml) labelHtml = escapeText(href);
                    html += `<a href="${escapeAttribute(href)}">${labelHtml}</a>`;
                }
            } else {
                html += openTag[0] + inner.html + openTag[1];
            }
            position = inner.position;
        }
        return {html, position, closed: false};
    }
    return parseFrom(0, null, false, 0).html;
}

function parseWithLimit(input) {
    const source = String(input ?? "");
    const truncated = utf8Length(source) > MAX_DESCRIPTION_BYTES;
    const boundedSource = truncated ? truncateUtf8(source, MAX_DESCRIPTION_BYTES) : source;
    let html = parseMarkup(boundedSource);
    if (truncated)
        html += "<br/><br/><em>[Description preview truncated at 128 KiB]</em>";
    return {html, truncated};
}

export const parser = {
    parse(text) { return parseWithLimit(text).html; },
    parseWithLimit,
    maxInputBytes: MAX_DESCRIPTION_BYTES
};
