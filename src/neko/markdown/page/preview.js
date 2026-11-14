// The Markdown preview's page script. C++ (markdown::MarkdownPage) calls
// window.nekoRender(payload) and window.nekoScrollToLine(line). This script
// answers in two ways: document.title becomes "rendered:<n>" when a render has
// finished (Mermaid diagrams included), and a clicked link of one of the application's
// own schemes, or a link to another Markdown file, is reported as a console message
// "neko:custom-link <href>" / "neko:document-link <url>", which the page object
// reads (no QWebChannel needed for two messages).
//
// Raw HTML in a document is off (markdown-it `html: false`) and Mermaid runs with
// securityLevel "strict", so a shared document cannot inject script into this page.
'use strict';

(function () {
    const md = window.markdownit({ html: false, linkify: true, typographer: false });
    if (window.markdownitSub) {
        md.use(window.markdownitSub);
    }
    if (window.markdownitSup) {
        md.use(window.markdownitSup);
    }
    if (window.markdownitFootnote) {
        md.use(window.markdownitFootnote);
    }
    if (window.markdownitDeflist) {
        md.use(window.markdownitDeflist);
    }
    if (window.markdownitEmoji) {
        md.use(window.markdownitEmoji);
    }

    function slugifyHeading(text) {
        const slug = String(text)
            .toLowerCase()
            .trim()
            .replace(/[^\p{L}\p{N}\s_-]/gu, '')
            .replace(/\s+/g, '-')
            .replace(/-+/g, '-')
            .replace(/^-|-$/g, '');
        return slug || 'section';
    }

    function inlinePlainText(inlineToken) {
        if (!inlineToken || !Array.isArray(inlineToken.children)) {
            return inlineToken ? (inlineToken.content || '') : '';
        }
        let out = '';
        for (const child of inlineToken.children) {
            if (child.type === 'softbreak' || child.type === 'hardbreak') {
                out += ' ';
            } else if (child.type !== 'html_inline' && child.content) {
                out += child.content;
            }
        }
        return out.trim();
    }

    function renderTocHtml(headings, sourceLineAttr) {
        if (!headings || headings.length === 0) {
            return `<nav class="table-of-contents"${sourceLineAttr}></nav>\n`;
        }
        let minLevel = 6;
        for (const h of headings) {
            if (h.level < minLevel) {
                minLevel = h.level;
            }
        }
        let html = `<nav class="table-of-contents"${sourceLineAttr} aria-label="Table of Contents">\n`;
        const stack = [];
        for (const h of headings) {
            const level = Math.max(1, h.level - minLevel + 1);
            while (stack.length > level) {
                stack.pop();
                html += '</li></ul>\n';
            }
            if (stack.length === level) {
                html += '</li>\n';
            }
            while (stack.length < level) {
                stack.push(stack.length + 1);
                html += '<ul>\n';
            }
            html += `<li><a href="#${md.utils.escapeHtml(encodeURIComponent(h.id))}">${md.utils.escapeHtml(h.text)}</a>`;
        }
        while (stack.length > 0) {
            stack.pop();
            html += '</li></ul>\n';
        }
        html += '</nav>\n';
        return html;
    }

    // Assign GitHub-style unique IDs to headings and expand standalone [ToC] paragraphs.
    md.core.ruler.push('neko_headings_and_toc', (state) => {
        const slugCounts = new Map();
        const headings = [];
        const tokens = state.tokens;
        for (let i = 0; i < tokens.length; ++i) {
            const token = tokens[i];
            if (token.type === 'heading_open') {
                const level = Number(token.tag.slice(1)) || 1;
                const text = inlinePlainText(tokens[i + 1]);
                const base = slugifyHeading(text);
                const count = slugCounts.get(base) || 0;
                slugCounts.set(base, count + 1);
                const id = count === 0 ? base : `${base}-${count}`;
                token.attrSet('id', id);
                headings.push({ level, text, id });
            } else if (
                token.type === 'paragraph_open' &&
                token.level === 0 &&
                i + 2 < tokens.length &&
                tokens[i + 1].type === 'inline' &&
                tokens[i + 2].type === 'paragraph_close' &&
                tokens[i + 1].content.trim().toLowerCase() === '[toc]'
            ) {
                const tocToken = new state.Token('neko_toc', 'nav', 0);
                tocToken.block = true;
                tocToken.map = token.map;
                tocToken.meta = { headings };
                tokens.splice(i, 3, tocToken);
            }
        }
    });

    md.renderer.rules.neko_toc = (tokens, idx) => {
        const token = tokens[idx];
        const line = token.attrGet('data-source-line');
        const lineAttr = line !== null ? ` data-source-line="${md.utils.escapeHtml(line)}"` : '';
        return renderTocHtml(token.meta && token.meta.headings, lineAttr);
    };

    // Every top-level block remembers the (body) line it came from, for scroll sync.
    md.core.ruler.push('neko_source_lines', (state) => {
        for (const token of state.tokens) {
            if (token.block && token.map && token.nesting !== -1) {
                token.attrSet('data-source-line', String(token.map[0]));
            }
        }
    });

    // ```mermaid blocks become diagrams; ```math/latex blocks become display math.
    const defaultFence = md.renderer.rules.fence;
    md.renderer.rules.fence = (tokens, idx, options, env, self) => {
        const token = tokens[idx];
        const lang = token.info.trim().split(/\s+/)[0].toLowerCase();
        if (lang === 'mermaid') {
            const line = token.map ? token.map[0] : '';
            return `<div class="mermaid-block" data-source-line="${line}">` +
                `<pre class="mermaid-source">${md.utils.escapeHtml(token.content)}</pre></div>\n`;
        }
        if (lang === 'math' || lang === 'latex') {
            const line = token.map ? token.map[0] : '';
            return `<div data-source-line="${line}">${renderMath(token.content, true)}</div>\n`;
        }
        return defaultFence(tokens, idx, options, env, self);
    };

    // LaTeX math ($...$ inline and $$...$$ block).
    function isValidMathDelim(state, pos) {
        const prevChar = pos > 0 ? state.src.charCodeAt(pos - 1) : -1;
        const nextChar = pos + 1 <= state.posMax ? state.src.charCodeAt(pos + 1) : -1;
        let can_open = true;
        let can_close = true;

        if (prevChar === 0x20 || prevChar === 0x09 || (nextChar >= 0x30 && nextChar <= 0x39)) {
            can_close = false;
        }
        if (nextChar === 0x20 || nextChar === 0x09) {
            can_open = false;
        }
        return { can_open, can_close };
    }

    function mathInline(state, silent) {
        if (state.src[state.pos] !== '$') {
            return false;
        }

        const res = isValidMathDelim(state, state.pos);
        if (!res.can_open) {
            if (!silent) {
                state.pending += '$';
            }
            state.pos += 1;
            return true;
        }

        const start = state.pos + 1;
        let match = start;
        while ((match = state.src.indexOf('$', match)) !== -1) {
            let pos = match - 1;
            while (state.src[pos] === '\\') {
                pos -= 1;
            }
            if ((match - pos) % 2 === 1) {
                break;
            }
            match += 1;
        }

        if (match === -1) {
            if (!silent) {
                state.pending += '$';
            }
            state.pos = start;
            return true;
        }

        if (match - start === 0) {
            if (!silent) {
                state.pending += '$$';
            }
            state.pos = start + 1;
            return true;
        }

        const closeRes = isValidMathDelim(state, match);
        if (!closeRes.can_close) {
            if (!silent) {
                state.pending += '$';
            }
            state.pos = start;
            return true;
        }

        if (!silent) {
            const token = state.push('math_inline', 'math', 0);
            token.markup = '$';
            token.content = state.src.slice(start, match);
        }
        state.pos = match + 1;
        return true;
    }

    function mathBlock(state, start, end, silent) {
        let pos = state.bMarks[start] + state.tShift[start];
        const max = state.eMarks[start];

        if (pos + 2 > max) {
            return false;
        }
        if (state.src.slice(pos, pos + 2) !== '$$') {
            return false;
        }

        pos += 2;
        let firstLine = state.src.slice(pos, max);
        let found = false;
        let lastLine = '';

        if (silent) {
            return true;
        }
        if (firstLine.trim().slice(-2) === '$$') {
            firstLine = firstLine.trim().slice(0, -2);
            found = true;
        }

        let next = start;
        while (!found) {
            next++;
            if (next >= end) {
                break;
            }
            pos = state.bMarks[next] + state.tShift[next];
            const lineMax = state.eMarks[next];
            if (pos < lineMax && state.tShift[next] < state.blkIndent) {
                break;
            }
            if (state.src.slice(pos, lineMax).trim().slice(-2) === '$$') {
                const lastPos = state.src.slice(0, lineMax).lastIndexOf('$$');
                lastLine = state.src.slice(pos, lastPos);
                found = true;
            }
        }

        state.line = next + 1;
        const token = state.push('math_block', 'math', 0);
        token.block = true;
        token.content = (firstLine && firstLine.trim() ? firstLine + '\n' : '') +
            state.getLines(start + 1, next, state.tShift[start], true) +
            (lastLine && lastLine.trim() ? lastLine : '');
        token.map = [start, state.line];
        token.markup = '$$';
        return true;
    }

    md.inline.ruler.after('escape', 'math_inline', mathInline);
    md.block.ruler.after('blockquote', 'math_block', mathBlock, {
        alt: ['paragraph', 'reference', 'blockquote', 'list']
    });

    function renderMath(content, displayMode) {
        if (!window.katex) {
            return displayMode
                ? `<div class="katex-display" data-tex="${md.utils.escapeHtml(content)}"><pre>${md.utils.escapeHtml(content)}</pre></div>\n`
                : `<code data-tex="${md.utils.escapeHtml(content)}">${md.utils.escapeHtml(content)}</code>`;
        }
        try {
            const html = window.katex.renderToString(content, {
                displayMode: displayMode,
                throwOnError: false,
                output: 'mathml'
            });
            const escaped = md.utils.escapeHtml(content);
            if (displayMode) {
                return `<div class="katex-display" data-tex="${escaped}">${html}</div>\n`;
            }
            return html.replace(/^<span class="katex"/, `<span class="katex" data-tex="${escaped}"`);
        } catch (e) {
            return `<span class="katex-error" title="${md.utils.escapeHtml(e.message)}">${md.utils.escapeHtml(content)}</span>`;
        }
    }

    md.renderer.rules.math_inline = (tokens, idx) => renderMath(tokens[idx].content, false);
    md.renderer.rules.math_block = (tokens, idx) => {
        const token = tokens[idx];
        const line = token.map ? token.map[0] : '';
        return `<div data-source-line="${line}">${renderMath(token.content, true)}</div>\n`;
    };

    // The application's own link schemes (payload.linkSchemes, e.g. "db" for links to
    // database objects): such a link is marked, and the C++ side handles the click.
    let linkSchemes = new Map(); // lower-case scheme -> marker text ('' for none)

    function customSchemeOf(href) {
        const colon = href.indexOf(':');
        if (colon <= 0) {
            return null;
        }
        const scheme = href.slice(0, colon).toLowerCase();
        return linkSchemes.has(scheme) ? scheme : null;
    }

    const defaultLinkOpen = md.renderer.rules.link_open ||
        ((tokens, idx, options, env, self) => self.renderToken(tokens, idx, options));
    md.renderer.rules.link_open = (tokens, idx, options, env, self) => {
        const href = tokens[idx].attrGet('href') || '';
        const scheme = customSchemeOf(href);
        if (scheme !== null) {
            tokens[idx].attrJoin('class', 'custom-link');
            tokens[idx].attrSet('data-scheme', scheme);
            tokens[idx].attrSet('title', safeDecode(href.slice(scheme.length + 1)));
            if (linkSchemes.get(scheme)) {
                tokens[idx].attrSet('data-marker', linkSchemes.get(scheme));
            }
        }
        return defaultLinkOpen(tokens, idx, options, env, self);
    };

    function safeDecode(text) {
        try {
            return decodeURIComponent(text);
        } catch (e) {
            return text;
        }
    }

    let renderCount = 0;
    let bodyStartLine = 0;
    let mermaidTheme = null;
    let diagramId = 0;
    const diagramCache = new Map(); // theme + source -> svg, so typing elsewhere is cheap
    const kCacheLimit = 64;

    function escapeHtml(text) {
        return md.utils.escapeHtml(String(text));
    }

    // The chips above the document: {text, value, toolTip, dashed} each, `value` in bold.
    function renderMeta(payload) {
        const chips = [];
        for (const chip of payload.chips || []) {
            const parts = [];
            if (chip.text) {
                parts.push(escapeHtml(chip.text));
            }
            if (chip.value) {
                parts.push(`<b>${escapeHtml(chip.value)}</b>`);
            }
            if (parts.length === 0) {
                continue;
            }
            const title = chip.toolTip ? ` title="${escapeHtml(chip.toolTip)}"` : '';
            chips.push(`<span class="chip${chip.dashed ? ' dashed' : ''}"${title}>${parts.join(' ')}</span>`);
        }
        return chips.join('');
    }

    // Export mode draws labels as SVG text: HTML labels (foreignObject) would taint the
    // canvas the diagrams are turned into PNGs on.
    async function renderDiagrams(dark, exportMode) {
        const theme = (dark ? 'dark' : 'default') + (exportMode ? ':export' : '');
        if (theme !== mermaidTheme) {
            window.mermaid.initialize({
                startOnLoad: false,
                securityLevel: 'strict',
                theme: dark ? 'dark' : 'default',
                htmlLabels: !exportMode,
                flowchart: { htmlLabels: !exportMode },
            });
            mermaidTheme = theme;
        }
        let errors = 0;
        for (const block of document.querySelectorAll('.mermaid-block')) {
            const source = block.querySelector('.mermaid-source').textContent;
            const key = theme + '\n' + source;
            let svg = diagramCache.get(key);
            if (svg === undefined) {
                try {
                    svg = (await window.mermaid.render('neko-mermaid-' + (++diagramId), source)).svg;
                } catch (e) {
                    svg = null;
                    block.innerHTML = `<pre class="mermaid-error">${escapeHtml(e && e.message ? e.message : e)}</pre>`;
                    errors++;
                    continue;
                }
                if (diagramCache.size >= kCacheLimit) {
                    diagramCache.delete(diagramCache.keys().next().value);
                }
                diagramCache.set(key, svg);
            }
            block.innerHTML = svg;
        }
        document.body.dataset.mermaidErrors = String(errors);
    }

    // payload: {body, bodyStartLine, baseUrl, dark, export, chips, linkSchemes, backgroundColor}
    window.nekoRender = async function (payload) {
        const n = ++renderCount;
        bodyStartLine = payload.bodyStartLine || 0;
        linkSchemes = new Map();
        for (const entry of payload.linkSchemes || []) {
            if (entry && /^[a-z][a-z0-9+.-]*$/i.test(entry.scheme || '')) {
                linkSchemes.set(entry.scheme.toLowerCase(), entry.marker || '');
            }
        }
        document.documentElement.dataset.theme = payload.dark ? 'dark' : 'light';
        if (payload.backgroundColor) {
            document.documentElement.style.setProperty('--bg', payload.backgroundColor);
            document.documentElement.style.setProperty('--fg', payload.dark ? '#e6edf3' : '#1f2328');
            document.documentElement.style.setProperty('--code-bg', payload.dark ? 'rgba(255,255,255,0.08)' : 'rgba(0,0,0,0.06)');
            document.documentElement.style.setProperty('--border', payload.dark ? 'rgba(255,255,255,0.15)' : 'rgba(0,0,0,0.12)');
        } else {
            document.documentElement.style.removeProperty('--bg');
            document.documentElement.style.removeProperty('--fg');
            document.documentElement.style.removeProperty('--code-bg');
            document.documentElement.style.removeProperty('--border');
        }
        document.querySelector('base').href = payload.baseUrl || 'qrc:/neko/markdown/';
        document.getElementById('meta').innerHTML = renderMeta(payload);
        document.getElementById('content').innerHTML = md.render(payload.body || '');
        await renderDiagrams(payload.dark, payload.export === true);
        if (n === renderCount) {
            document.title = 'rendered:' + n;
        }
    };

    // A diagram's or formula's SVG as a PNG data URL, at twice its size for print, on white.
    // Also returns the SVG XML string so vector graphics can be embedded directly.
    async function rasterize(svg) {
        let width = 0;
        let height = 0;
        if (svg.isConnected) {
            const box = svg.getBoundingClientRect();
            width = Math.round(box.width);
            height = Math.round(box.height);
        }
        if (!width || !height) {
            const attrW = parseFloat(svg.getAttribute('width'));
            const attrH = parseFloat(svg.getAttribute('height'));
            const wStr = svg.getAttribute('width') || '';
            if (attrW > 0 && attrH > 0 && !wStr.endsWith('ex') && !wStr.endsWith('%')) {
                width = Math.round(attrW);
                height = Math.round(attrH);
            }
        }
        if (!width || !height) {
            const wasConnected = svg.isConnected;
            if (!wasConnected) {
                document.body.appendChild(svg);
            }
            const box = svg.getBoundingClientRect();
            width = Math.round(box.width);
            height = Math.round(box.height);
            if (!wasConnected) {
                document.body.removeChild(svg);
            }
        }
        width = Math.max(8, width);
        height = Math.max(8, height);
        const copy = svg.cloneNode(true);
        copy.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
        copy.setAttribute('width', String(width));
        copy.setAttribute('height', String(height));
        copy.style.maxWidth = 'none';
        const xml = new XMLSerializer().serializeToString(copy);
        const url = URL.createObjectURL(new Blob([xml], { type: 'image/svg+xml' }));
        try {
            const img = new Image();
            await new Promise((resolve, reject) => {
                img.onload = resolve;
                img.onerror = () => reject(new Error('the diagram could not be drawn'));
                img.src = url;
            });
            const scale = 6;
            const canvas = document.createElement('canvas');
            canvas.width = width * scale;
            canvas.height = height * scale;
            const ctx = canvas.getContext('2d');
            ctx.drawImage(img, 0, 0, canvas.width, canvas.height);
            return { data: canvas.toDataURL('image/png'), svg: xml, width: width, height: height };
        } finally {
            URL.revokeObjectURL(url);
        }
    }

    function texToSvg(tex, display) {
        if (!window.MathJax || typeof window.MathJax.tex2svg !== 'function') {
            return null;
        }
        try {
            const node = window.MathJax.tex2svg(tex, { display: display });
            const svg = node.querySelector('svg');
            if (!svg) {
                return null;
            }
            document.body.appendChild(node);
            const svgRect = svg.getBoundingClientRect();
            document.body.removeChild(node);
            let w = Math.round(svgRect.width);
            let h = Math.round(svgRect.height);
            if (!w || !h) {
                const exW = parseFloat(svg.getAttribute('width'));
                const exH = parseFloat(svg.getAttribute('height'));
                if (exW && exH) {
                    w = Math.round(exW * 8.5);
                    h = Math.round(exH * 8.5);
                }
            }
            if (!w || !h) {
                const vb = svg.viewBox && svg.viewBox.baseVal;
                if (vb && vb.width && vb.height) {
                    w = Math.round(vb.width * 0.016);
                    h = Math.round(vb.height * 0.016);
                }
            }
            w = Math.max(8, w);
            h = Math.max(8, h);
            svg.setAttribute('width', String(w));
            svg.setAttribute('height', String(h));
            // Ensure standalone SVGs render in black even in viewers like LibreOffice
            // that do not resolve `currentColor`.
            svg.style.color = '#000000';
            for (const el of svg.querySelectorAll('[stroke="currentColor"]')) {
                el.setAttribute('stroke', '#000000');
            }
            for (const el of svg.querySelectorAll('[fill="currentColor"]')) {
                el.setAttribute('fill', '#000000');
            }
            if (svg.getAttribute('stroke') === 'currentColor') {
                svg.setAttribute('stroke', '#000000');
            }
            if (svg.getAttribute('fill') === 'currentColor') {
                svg.setAttribute('fill', '#000000');
            }
            return svg;
        } catch (e) {
            console.warn('MathJax tex2svg error:', e);
            return null;
        }
    }

    // The rendered body for a word processor: every diagram and formula an <img src="neko-export:N">
    // (N into `images`, with both PNG data URLs and vector SVG XML), other images with absolute sources.
    // The result waits in window.nekoExportResult; the title says "exported:<n>" when it is there.
    let exportCount = 0;
    window.nekoExportContent = async function () {
        const n = ++exportCount;
        // The copy lives in an inert document: its <img>s are never loaded.
        const inert = document.implementation.createHTMLDocument('');
        const content = document.getElementById('content');
        const copy = inert.importNode(content, true);
        const live = content.querySelectorAll('.mermaid-block');
        const copies = copy.querySelectorAll('.mermaid-block');
        const images = [];

        // 1. Mermaid diagrams
        for (let i = 0; i < live.length; ++i) {
            const svg = live[i].querySelector('svg');
            if (!svg) {
                continue; // an error message: it stays text
            }
            const p = inert.createElement('p');
            try {
                const imgData = await rasterize(svg);
                const img = inert.createElement('img');
                img.setAttribute('src', 'neko-export:' + images.length);
                img.setAttribute('width', String(imgData.width));
                img.setAttribute('height', String(imgData.height));
                images.push({ png: imgData.data, svg: imgData.svg });
                p.appendChild(img);
            } catch (e) {
                p.textContent = '[diagram: ' + (e && e.message ? e.message : e) + ']';
            }
            copies[i].replaceWith(p);
        }

        // 2. Block math ($$...$$ and ```math)
        for (const block of copy.querySelectorAll('.katex-display')) {
            const tex = block.getAttribute('data-tex') || block.querySelector('annotation')?.textContent || '';
            const svg = tex ? texToSvg(tex, true) : null;
            if (svg) {
                const p = inert.createElement('p');
                try {
                    const imgData = await rasterize(svg);
                    const img = inert.createElement('img');
                    img.setAttribute('src', 'neko-export:' + images.length);
                    img.setAttribute('width', String(imgData.width));
                    img.setAttribute('height', String(imgData.height));
                    images.push({ png: imgData.data, svg: imgData.svg });
                    p.appendChild(img);
                    block.replaceWith(p);
                } catch (e) {
                    console.warn('Formula export error:', e);
                }
            }
        }

        // 3. Inline math ($...$)
        for (const inline of copy.querySelectorAll('.katex')) {
            // Skip if somehow inside a block that was already replaced
            if (!inline.parentElement) {
                continue;
            }
            const tex = inline.getAttribute('data-tex') || inline.querySelector('annotation')?.textContent || '';
            const svg = tex ? texToSvg(tex, false) : null;
            if (svg) {
                try {
                    const imgData = await rasterize(svg);
                    const img = inert.createElement('img');
                    img.setAttribute('src', 'neko-export:' + images.length);
                    img.setAttribute('width', String(imgData.width));
                    img.setAttribute('height', String(imgData.height));
                    images.push({ png: imgData.data, svg: imgData.svg });
                    inline.replaceWith(img);
                } catch (e) {
                    console.warn('Inline formula export error:', e);
                }
            }
        }

        for (const img of copy.querySelectorAll('img')) {
            if (!img.getAttribute('src').startsWith('neko-export:')) {
                img.setAttribute('src', new URL(img.getAttribute('src'), document.baseURI).href);
            }
        }

        // Footnote backreferences (the return arrows)
        for (const backref of copy.querySelectorAll('a.footnote-backref')) {
            backref.remove();
        }

        // Footnote references in the body (replace optical [N] with clean anchor)
        for (const ref of copy.querySelectorAll('sup.footnote-ref')) {
            const a = ref.querySelector('a');
            if (a) {
                const href = a.getAttribute('href');
                if (href) {
                    const cleanA = inert.createElement('a');
                    cleanA.setAttribute('href', href);
                    cleanA.textContent = '\u2060';
                    ref.replaceWith(cleanA);
                }
            }
        }

        for (const ann of copy.querySelectorAll('annotation')) {
            ann.remove();
        }

        window.nekoExportResult = { html: copy.innerHTML, images: images };
        document.title = 'exported:' + n;
    };

    // `line` is a line of the whole file (what precedes the body included).
    window.nekoScrollToLine = function (line) {
        const target = line - bodyStartLine;
        let best = null;
        for (const el of document.querySelectorAll('#content [data-source-line]')) {
            if (Number(el.dataset.sourceLine) <= target) {
                best = el;
            } else {
                break;
            }
        }
        window.scrollTo(0, best ? best.getBoundingClientRect().top + window.scrollY - 6 : 0);
    };

    // Every link click is decided here. Only http(s) and mailto links navigate (and the
    // page object sends those to the system browser); anything else would reach
    // Chromium's external-protocol handling, which can start other programs.
    document.addEventListener('click', (event) => {
        const a = event.target.closest('a');
        if (!a) {
            return;
        }
        const href = a.getAttribute('href') || '';
        if (/^(https?:|mailto:)/i.test(a.href)) {
            return;
        }
        event.preventDefault();
        if (href.startsWith('#')) {
            const target = document.getElementById(safeDecode(href.slice(1)));
            if (target) {
                target.scrollIntoView();
            }
        } else if (customSchemeOf(href) !== null) {
            console.info('neko:custom-link ' + href);
        } else if (/^neko-note:/i.test(a.href) && /\.(md|markdown)$/i.test(a.pathname)) {
            console.info('neko:document-link ' + a.href);
        }
    });
})();
