/*
 * PROJECT:     LiberNT Web View
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Browser globals on top of linkedom, bridged to the native view through __lnt
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

(function () {
    "use strict";
    const N = globalThis.__lnt;
    const g = globalThis;

    const console = {};
    for (const name of ["log", "info", "warn", "error", "debug", "trace", "table", "dir", "group", "groupCollapsed",
                        "groupEnd", "time", "timeEnd", "count", "assert"])
        console[name] = () => {};
    g.console = console;

    if (!g.Intl) {
        g.Intl = {
            NumberFormat: class { constructor(l, o = {}) { this.max = o.maximumFractionDigits ?? 3; } format(n) { return String(+Number(n).toFixed(this.max)); } resolvedOptions() { return { locale: "en-US" }; } },
            DateTimeFormat: class { format(d) { return new Date(d ?? Date.now()).toISOString(); } resolvedOptions() { return { timeZone: "UTC", locale: "en-US" }; } },
            Collator: class { compare(a, b) { return a < b ? -1 : a > b ? 1 : 0; } },
            PluralRules: class { select(n) { return n === 1 ? "one" : "other"; } },
        };
    }
    if (!g.TextEncoder) {
        g.TextEncoder = class { encode(s = "") { const u = unescape(encodeURIComponent(s)); return Uint8Array.from(u, c => c.charCodeAt(0)); } };
        g.TextDecoder = class { decode(b) { return b ? decodeURIComponent(escape(String.fromCharCode(...new Uint8Array(b)))) : ""; } };
    }

    const timers = new Map();
    let nextTimer = 1;
    const addTimer = (fn, ms, args, repeat) => {
        const id = nextTimer++;
        timers.set(id, { fn, args, repeat, ms: Math.max(+ms || 0, repeat ? 10 : 0) });
        N.setTimer(id, timers.get(id).ms);
        return id;
    };
    g.setTimeout = (fn, ms, ...args) => addTimer(fn, ms, args, false);
    g.setInterval = (fn, ms, ...args) => addTimer(fn, ms, args, true);
    g.clearTimeout = g.clearInterval = id => { if (timers.delete(id)) N.clearTimer(id); };
    g.requestAnimationFrame = fn => g.setTimeout(() => fn(Date.now()), 16);
    g.cancelAnimationFrame = g.clearTimeout;
    g.requestIdleCallback = fn => g.setTimeout(() => fn({ didTimeout: false, timeRemaining: () => 10 }), 1);
    g.cancelIdleCallback = g.clearTimeout;
    if (!g.queueMicrotask)
        g.queueMicrotask = fn => Promise.resolve().then(fn);
    g.__lntFire = id => {
        const timer = timers.get(id);
        if (!timer)
            return;
        if (timer.repeat)
            N.setTimer(id, timer.ms);
        else
            timers.delete(id);
        try {
            typeof timer.fn === "function" ? timer.fn(...timer.args) : (0, eval)(String(timer.fn));
        } catch (e) {
            N.report("timer: " + e + "\n" + (e && e.stack || ""));
        }
    };

    let pageUrl = null;
    const resolve = href => { try { return new URL(href, document.baseURI || pageUrl.href).href; } catch (e) { return null; } };
    const sameDocument = url => url.split("#")[0] === pageUrl.href.split("#")[0];
    let entries = [];
    let index = 0;
    const fireWindow = (type, fields) => {
        const event = new Event(type);
        for (const key of Object.keys(fields || {})) {
            try { Object.defineProperty(event, key, { configurable: true, value: fields[key] }); } catch (e) {}
        }
        g.dispatchEvent(event);
        const handler = g["on" + type];
        if (typeof handler === "function") {
            try { handler.call(g, event); } catch (e) { N.report("on" + type + ": " + e); }
        }
    };
    const moveTo = (url, push, state) => {
        const old = pageUrl.href;
        pageUrl = new URL(url);
        if (push) {
            entries.splice(index + 1);
            entries.push({ url: pageUrl.href, state });
            index = entries.length - 1;
        } else {
            entries[index] = { url: pageUrl.href, state };
        }
        return old;
    };
    const setHash = value => {
        const next = new URL(pageUrl.href);
        next.hash = value;
        if (next.href === pageUrl.href)
            return;
        const old = moveTo(next.href, true, null), now = pageUrl.href;
        g.setTimeout(() => fireWindow("hashchange", { oldURL: old, newURL: now }), 0);
    };
    const go = (href, method = "GET", body = null, type = null) => {
        const target = resolve(href);
        if (!target || !/^https?:/i.test(target))
            return;
        if (method === "GET" && target.includes("#") && sameDocument(target)) {
            setHash(new URL(target).hash);
            return;
        }
        N.navigate(target, method, body, type);
    };

    g.__lntLoad = (html, url) => {
        pageUrl = new URL(url);
        const dom = __lntParseHTML(html);
        const window = dom.window;
        const document = dom.document;
        g.window = g; g.self = g; g.top = g; g.parent = g; g.frames = g;
        g.document = document;
        for (const name of ["Node", "Element", "HTMLElement", "HTMLInputElement", "HTMLSelectElement", "HTMLTextAreaElement",
                            "HTMLButtonElement", "HTMLAnchorElement", "HTMLFormElement", "Event", "CustomEvent", "KeyboardEvent",
                            "MouseEvent", "InputEvent", "FocusEvent", "UIEvent", "DocumentFragment", "Text", "Comment",
                            "NodeList", "HTMLCollection", "DOMParser", "CSSStyleDeclaration", "EventTarget", "customElements",
                            "HTMLTemplateElement", "Range", "SVGElement", "XMLSerializer", "MutationObserver", "Image",
                            "HTMLImageElement", "HTMLScriptElement", "ShadowRoot", "HTMLDivElement", "HTMLSpanElement"]) {
            try { if (window[name] !== undefined && g[name] === undefined) g[name] = window[name]; } catch (e) {}
        }
        if (!g.MouseEvent && g.Event)
            g.MouseEvent = class extends g.Event {};
        if (!g.MutationObserver)
            g.MutationObserver = class { observe() {} disconnect() {} takeRecords() { return []; } };
        if (!g.IntersectionObserver)
            g.IntersectionObserver = class { observe() {} unobserve() {} disconnect() {} };
        if (!g.ResizeObserver)
            g.ResizeObserver = class { observe() {} unobserve() {} disconnect() {} };

        const location = {
            get href() { return pageUrl.href; }, set href(v) { go(v); },
            get protocol() { return pageUrl.protocol; }, get host() { return pageUrl.host; },
            get hostname() { return pageUrl.hostname; }, get port() { return pageUrl.port; },
            get pathname() { return pageUrl.pathname; }, get search() { return pageUrl.search; },
            get hash() { return pageUrl.hash; }, set hash(v) { setHash(String(v)); },
            get origin() { return pageUrl.origin; },
            assign(v) { go(v); }, replace(v) { go(v); }, reload() { go(pageUrl.href); }, toString() { return pageUrl.href; },
        };
        Object.defineProperty(g, "location", { configurable: true, get: () => location, set: v => go(v) });
        try { Object.defineProperty(document, "location", { configurable: true, get: () => location, set: v => go(v) }); } catch (e) {}
        try { Object.defineProperty(document, "URL", { configurable: true, get: () => pageUrl.href }); } catch (e) {}
        try { Object.defineProperty(document, "documentURI", { configurable: true, get: () => pageUrl.href }); } catch (e) {}
        try {
            Object.defineProperty(document, "baseURI", { configurable: true, get: () => {
                const base = document.querySelector("base[href]");
                return base ? new URL(base.getAttribute("href"), pageUrl.href).href : pageUrl.href;
            } });
        } catch (e) {}
        try { Object.defineProperty(document, "cookie", { configurable: true, get: () => "", set: () => {} }); } catch (e) {}
        try { Object.defineProperty(document, "readyState", { configurable: true, writable: true, value: "loading" }); } catch (e) {}
        if (!document.implementation) {
            Object.defineProperty(document, "implementation", { configurable: true, value: {
                createHTMLDocument(title = "") { return __lntParseHTML("<!doctype html><html><head><title>" + title + "</title></head><body></body></html>").document; },
                hasFeature() { return true; },
            } });
        }

        Object.defineProperty(g, "navigator", { configurable: true, writable: true, value: {
            userAgent: N.userAgent, language: N.language, languages: [N.language], cookieEnabled: true,
            platform: "Win32", onLine: true, vendor: "", maxTouchPoints: 0, hardwareConcurrency: 4,
            sendBeacon: () => true,
        } });
        const store = () => { const m = new Map(); return { getItem: k => m.has(String(k)) ? m.get(String(k)) : null,
            setItem: (k, v) => void m.set(String(k), String(v)), removeItem: k => void m.delete(String(k)), clear: () => m.clear(),
            key: i => [...m.keys()][i] ?? null, get length() { return m.size; } }; };
        g.localStorage = store();
        g.sessionStorage = store();
        entries = [{ url: pageUrl.href, state: null }];
        index = 0;
        const traverse = delta => {
            const target = index + delta;
            if (!delta || target < 0 || target >= entries.length)
                return;
            if (!sameDocument(entries[target].url)) {
                index = target;
                N.navigate(entries[target].url, "GET", null, null);
                return;
            }
            const old = pageUrl.href;
            index = target;
            pageUrl = new URL(entries[target].url);
            const now = pageUrl.href, state = entries[target].state;
            g.setTimeout(() => {
                fireWindow("popstate", { state });
                if (old.split("#")[1] !== now.split("#")[1])
                    fireWindow("hashchange", { oldURL: old, newURL: now });
            }, 0);
        };
        const historyUrl = url => {
            const target = url == null ? pageUrl.href : resolve(url);
            return target && new URL(target).origin === pageUrl.origin ? target : null;
        };
        g.history = {
            get length() { return entries.length; },
            get state() { return entries[index].state; },
            scrollRestoration: "auto",
            pushState(state, title, url) { const target = historyUrl(url); if (target) moveTo(target, true, state); },
            replaceState(state, title, url) { const target = historyUrl(url); if (target) moveTo(target, false, state); },
            back() { traverse(-1); },
            forward() { traverse(1); },
            go(delta) { traverse(Math.trunc(+delta || 0)); },
        };
        g.screen = { width: N.screenWidth, height: N.screenHeight, availWidth: N.screenWidth, availHeight: N.screenHeight, colorDepth: 24, pixelDepth: 24 };
        g.innerWidth = N.viewportWidth; g.innerHeight = N.viewportHeight; g.outerWidth = N.viewportWidth; g.outerHeight = N.viewportHeight;
        g.devicePixelRatio = 1; g.pageXOffset = 0; g.pageYOffset = 0; g.scrollX = 0; g.scrollY = 0;
        g.getComputedStyle = el => new Proxy({ getPropertyValue: name => (el && el.style && el.style.getPropertyValue) ? el.style.getPropertyValue(name) || "" : "" },
                                             { get: (t, k) => k in t ? t[k] : (el && el.style && el.style[k]) || "" });
        g.matchMedia = q => ({ matches: false, media: q, onchange: null, addListener() {}, removeListener() {}, addEventListener() {}, removeEventListener() {} });
        g.scrollTo = g.scroll = g.scrollBy = () => {};
        g.focus = g.blur = () => {};
        g.alert = () => {};
        g.confirm = () => true;
        g.prompt = () => null;
        g.open = href => { go(href); return null; };
        g.close = () => {};
        g.print = () => {};
        g.addEventListener = (...a) => window.addEventListener(...a);
        g.removeEventListener = (...a) => window.removeEventListener(...a);
        g.dispatchEvent = e => window.dispatchEvent(e);
        g.CSS = { supports: () => false, escape: s => String(s).replace(/[^\w-]/g, c => "\\" + c) };
        g.crypto = { getRandomValues: a => { for (let i = 0; i < a.length; i++) a[i] = Math.floor(Math.random() * 256); return a; },
                     randomUUID: () => "10000000-1000-4000-8000-100000000000".replace(/[018]/g, c => (c ^ Math.random() * 16 >> c / 4).toString(16)) };
        g.performance = { now: () => Date.now(), timing: {}, mark() {}, measure() {}, getEntriesByName: () => [] };

        const E = window.Element && window.Element.prototype;
        if (E) {
            if (!E.getBoundingClientRect) E.getBoundingClientRect = () => ({ x: 0, y: 0, top: 0, left: 0, right: 100, bottom: 20, width: 100, height: 20 });
            if (!E.getClientRects) E.getClientRects = function () { return [this.getBoundingClientRect()]; };
            if (!E.scrollIntoView) E.scrollIntoView = () => {};
            if (!E.focus) E.focus = () => {};
            if (!E.blur) E.blur = () => {};
            for (const name of ["querySelector", "querySelectorAll", "matches", "closest"]) {
                const original = E[name];
                if (original) E[name] = function (selector) { return original.call(this, String(selector)); };
            }
        }
        for (const name of ["querySelector", "querySelectorAll"]) {
            const original = document[name];
            if (original) document[name] = function (selector) { return original.call(this, String(selector)); };
        }
        for (const owner of [E, document]) {
            const original = owner && owner.getElementsByTagName;
            if (original) {
                owner.getElementsByTagName = function (name) {
                    return String(name) === "*" ? this.querySelectorAll("*") : original.call(this, name);
                };
            }
        }
        const H = window.HTMLElement && window.HTMLElement.prototype;
        if (H) {
            for (const [name, value] of [["offsetWidth", 100], ["offsetHeight", 20], ["offsetTop", 0], ["offsetLeft", 0],
                                         ["clientWidth", 100], ["clientHeight", 20], ["scrollHeight", 20], ["scrollWidth", 100]]) {
                if (!(name in H)) Object.defineProperty(H, name, { configurable: true, get() { return this.hasAttribute && this.hasAttribute("hidden") ? 0 : value; } });
            }
            if (!("offsetParent" in H)) Object.defineProperty(H, "offsetParent", { configurable: true, get() { return this.parentElement; } });
        }

        const valid = el => {
            if (el.disabled || el.type === "hidden" || el.localName === "button")
                return true;
            if (el.hasAttribute("required")) {
                if (el.type === "checkbox" || el.type === "radio") { if (!el.checked) return false; }
                else if (!String(el.value ?? "").length) return false;
            }
            if (el.type === "email" && el.value && !/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(el.value))
                return false;
            if (el.hasAttribute("pattern") && el.value) {
                try { if (!new RegExp("^(?:" + el.getAttribute("pattern") + ")$").test(el.value)) return false; } catch (e) {}
            }
            return !el.__lntCustomError;
        };
        const controls = form => [...form.querySelectorAll("input,select,textarea,button")];
        if (H) {
            H.checkValidity = function () { return this.localName === "form" ? controls(this).every(valid) : valid(this); };
            H.reportValidity = H.checkValidity;
            H.setCustomValidity = function (message) { this.__lntCustomError = !!message; };
            Object.defineProperty(H, "validity", { configurable: true, get() { const ok = valid(this); return { valid: ok, valueMissing: !ok }; } });
            Object.defineProperty(H, "willValidate", { configurable: true, get() { return true; } });
            H.click = function () { activate(this); };
            H.submit = function () { if (this.localName === "form") send(this, null); };
            H.requestSubmit = function (submitter) { if (this.localName === "form") submitForm(this, submitter || null); };
            H.reset = function () {};
        }
        for (const form of document.querySelectorAll("form"))
            Object.defineProperty(form, "elements", { configurable: true, get() { return controls(this); } });

        g.XMLHttpRequest = class {
            constructor() { this.readyState = 0; this.status = 0; this.statusText = ""; this.responseText = ""; this.response = "";
                            this.responseType = ""; this.withCredentials = false; this._headers = []; this.upload = { addEventListener() {} }; this._listeners = {}; }
            open(method, url, async = true) { this.method = String(method).toUpperCase(); this.url = resolve(url); this.async = async !== false; this.readyState = 1; }
            setRequestHeader(name, value) { this._headers.push(name + ": " + value); }
            overrideMimeType() {}
            getAllResponseHeaders() { return this._responseHeaders || ""; }
            getResponseHeader(name) {
                const m = (this._responseHeaders || "").match(new RegExp("^" + name.replace(/[^\w-]/g, "") + ":\\s*(.*)$", "im"));
                return m ? m[1].trim() : null;
            }
            addEventListener(type, fn) { (this._listeners[type] = this._listeners[type] || []).push(fn); }
            removeEventListener(type, fn) { this._listeners[type] = (this._listeners[type] || []).filter(f => f !== fn); }
            _emit(type) {
                const event = { type, target: this, currentTarget: this, lengthComputable: false, loaded: 0, total: 0 };
                try { if (typeof this["on" + type] === "function") this["on" + type](event); } catch (e) { N.report("xhr " + type + ": " + e); }
                for (const fn of this._listeners[type] || []) { try { fn.call(this, event); } catch (e) { N.report("xhr " + type + ": " + e); } }
            }
            abort() { this._aborted = true; }
            send(body = null) {
                const run = () => {
                    if (this._aborted) return;
                    if (body != null && !this._headers.some(h => /^content-type:/i.test(h)))
                        this._headers.push("Content-Type: text/plain;charset=UTF-8");
                    const result = this.url ? N.request(this.method, this.url, this._headers.join("\r\n"), body == null ? null : String(body)) : null;
                    if (!result) {
                        this.readyState = 4; this._emit("readystatechange"); this._emit("error"); this._emit("loadend");
                        return;
                    }
                    this.status = result.status; this.statusText = String(result.status); this._responseHeaders = result.headers;
                    this.responseText = result.body;
                    this.responseURL = this.url;
                    if (this.responseType === "json") { try { this.response = JSON.parse(result.body); } catch (e) { this.response = null; } }
                    else this.response = result.body;
                    this.readyState = 4;
                    this._emit("readystatechange"); this._emit("load"); this._emit("loadend");
                };
                if (this.async) g.setTimeout(run, 0); else run();
            }
        };
        g.fetch = (input, init = {}) => new Promise((ok, fail) => g.setTimeout(() => {
            const url = resolve(typeof input === "string" ? input : input.url);
            const headers = [];
            const h = init.headers || {};
            if (typeof h.forEach === "function") h.forEach((v, k) => headers.push(k + ": " + v)); else for (const k in h) headers.push(k + ": " + h[k]);
            if (init.body != null && !headers.some(h => /^content-type:/i.test(h)))
                headers.push("Content-Type: text/plain;charset=UTF-8");
            const result = url ? N.request((init.method || "GET").toUpperCase(), url, headers.join("\r\n"), init.body == null ? null : String(init.body)) : null;
            if (!result) return fail(new TypeError("Failed to fetch"));
            ok({ ok: result.status >= 200 && result.status < 300, status: result.status, statusText: String(result.status), url,
                 headers: { get: name => { const m = result.headers.match(new RegExp("^" + name + ":\\s*(.*)$", "im")); return m ? m[1].trim() : null; } },
                 text: async () => result.body, json: async () => JSON.parse(result.body), clone() { return this; } });
        }, 0));
        g.FormData = class {
            constructor(form) { this.entries_ = form ? pairs(form, null) : []; }
            append(k, v) { this.entries_.push([String(k), String(v)]); }
            set(k, v) { this.delete(k); this.append(k, v); }
            get(k) { const e = this.entries_.find(p => p[0] === k); return e ? e[1] : null; }
            getAll(k) { return this.entries_.filter(p => p[0] === k).map(p => p[1]); }
            has(k) { return this.entries_.some(p => p[0] === k); }
            delete(k) { this.entries_ = this.entries_.filter(p => p[0] !== k); }
            entries() { return this.entries_[Symbol.iterator](); }
            [Symbol.iterator]() { return this.entries(); }
            forEach(fn) { this.entries_.forEach(p => fn(p[1], p[0], this)); }
            toString() { return new URLSearchParams(this.entries_).toString(); }
        };

        let current = null;
        try { Object.defineProperty(document, "currentScript", { configurable: true, get: () => current }); } catch (e) {}
        try {
            Object.defineProperty(document, "forms", { configurable: true, get: () => [...document.querySelectorAll("form")] });
        } catch (e) {}
        for (const form of document.querySelectorAll("form")) {
            for (const control of controls(form)) {
                for (const key of [control.getAttribute("name"), control.getAttribute("id")]) {
                    if (key && !(key in form))
                        Object.defineProperty(form, key, { configurable: true, get: () => control });
                }
            }
        }
        for (const el of document.querySelectorAll("*")) {
            for (const attr of [...(el.attributes || [])]) {
                if (!/^on[a-z]+$/.test(attr.name))
                    continue;
                const code = attr.value;
                el.addEventListener(attr.name.slice(2), function (event) {
                    let result;
                    try { result = new Function("event", code).call(this, event); }
                    catch (e) { N.report(attr.name + ": " + e); }
                    if (result === false) event.preventDefault();
                });
            }
        }
        g.__lntRunScripts = () => {
            for (const script of [...document.querySelectorAll("script")]) {
                const type = (script.getAttribute("type") || "").trim().toLowerCase();
                if (type && !/^(text|application)\/(x-)?(javascript|ecmascript)$/.test(type))
                    continue;
                const src = script.getAttribute("src");
                const name = src ? resolve(src) : pageUrl.href;
                const code = src ? (name ? N.fetch(name) : null) : script.textContent;
                if (code == null) {
                    N.report("script not loaded: " + src);
                    continue;
                }
                current = script;
                N.evalScript(code, name);
            }
            current = null;
        };
        return true;
    };

    const pairs = (form, submitter) => {
        const result = [];
        for (const el of form.querySelectorAll("input,select,textarea,button")) {
            const name = el.getAttribute("name");
            if (!name || el.disabled || el.hasAttribute("disabled"))
                continue;
            const tag = el.localName, type = (el.getAttribute("type") || (tag === "button" ? "submit" : "text")).toLowerCase();
            if (tag === "button" || type === "submit" || type === "button" || type === "reset" || type === "image") {
                if (el === submitter && (type === "submit" || type === "image")) {
                    if (type === "image") { result.push([name + ".x", "0"], [name + ".y", "0"]); }
                    else result.push([name, el.getAttribute("value") || ""]);
                }
                continue;
            }
            if (type === "checkbox" || type === "radio") { if (el.checked) result.push([name, el.getAttribute("value") ?? "on"]); continue; }
            if (type === "file") continue;
            if (tag === "select") {
                const options = [...el.querySelectorAll("option")];
                const chosen = options.filter(o => o.selected || o.hasAttribute("selected"));
                for (const o of (chosen.length ? chosen : options.slice(0, 1))) result.push([name, o.getAttribute("value") ?? o.textContent]);
                continue;
            }
            result.push([name, tag === "textarea" ? el.value ?? el.textContent : (el.value ?? el.getAttribute("value") ?? "")]);
        }
        return result;
    };

    const send = (form, submitter) => {
        const method = ((submitter && submitter.getAttribute("formmethod")) || form.getAttribute("method") || "get").toLowerCase();
        const action = resolve((submitter && submitter.getAttribute("formaction")) || form.getAttribute("action") || pageUrl.href);
        const body = new URLSearchParams(pairs(form, submitter)).toString();
        if (!action)
            return;
        if (method === "post")
            go(action, "POST", body, "application/x-www-form-urlencoded");
        else
            go(action.replace(/[?#].*$/, "") + "?" + body);
    };

    const submitForm = (form, submitter) => {
        const novalidate = form.hasAttribute("novalidate") || (submitter && submitter.hasAttribute("formnovalidate"));
        if (!novalidate && !form.checkValidity())
            return;
        const event = new Event("submit", { bubbles: true, cancelable: true });
        try { Object.defineProperty(event, "submitter", { value: submitter }); } catch (e) {}
        if (form.dispatchEvent(event))
            send(form, submitter);
    };

    const fire = (el, type) => el.dispatchEvent(new Event(type, { bubbles: true, cancelable: type !== "input" && type !== "change" }));

    const disabledControl = el => !!el && /^(button|input|select|textarea)$/.test(el.localName) &&
                                   (el.disabled === true || el.hasAttribute("disabled"));
    const activate = target => {
        if (disabledControl(target.closest("button,input,select,textarea")))
            return;
        const act = target.closest("a[href],button,input,label,select,textarea,summary");
        const type = act && act.localName === "input" ? (act.getAttribute("type") || "text").toLowerCase() : "";
        let previous;
        if (act && (type === "checkbox" || type === "radio")) {
            if (act.disabled) return;
            previous = act.checked;
            act.checked = type === "checkbox" ? !previous : true;
        }
        const click = new (g.MouseEvent || Event)("click", { bubbles: true, cancelable: true });
        if (!target.dispatchEvent(click)) {
            if (previous !== undefined) act.checked = previous;
            return;
        }
        if (!act) return;
        if (type === "checkbox" || type === "radio") {
            if (type === "radio" && act.checked && act.name) {
                const scope = act.form || act.closest("form") || document;
                for (const other of scope.querySelectorAll('input[type="radio"]'))
                    if (other !== act && other.getAttribute("name") === act.getAttribute("name")) other.checked = false;
            }
            fire(act, "input");
            fire(act, "change");
            return;
        }
        if (act.localName === "label") {
            const id = act.getAttribute("for");
            const control = id ? document.getElementById(id) : act.querySelector("input,select,textarea,button");
            if (control && !control.contains(target) && control !== target) activate(control);
            return;
        }
        if (act.localName === "a") {
            const href = act.getAttribute("href") || "";
            if (/^javascript:/i.test(href)) { try { (0, eval)(decodeURIComponent(href.slice(11))); } catch (e) { N.report("javascript: " + e); } }
            else if (href) go(href);
            return;
        }
        const form = act.form || act.closest("form");
        const kind = act.localName === "button" ? (act.getAttribute("type") || "submit").toLowerCase() : type;
        if (form && !act.disabled && (kind === "submit" || kind === "image"))
            submitForm(form, act);
    };

    const find = id => document.querySelector('[data-lntid="' + id + '"]');
    const sync = fields => {
        for (const f of fields || []) {
            const el = find(f.id);
            if (!el) continue;
            if ("value" in f && el.value !== f.value) el.value = f.value;
            if ("index" in f && el.localName === "select") {
                [...el.querySelectorAll("option")].forEach((o, i) => { o.selected = i === f.index; if (i === f.index) o.setAttribute("selected", ""); else o.removeAttribute("selected"); });
            }
        }
    };
    g.__lntClick = (id, fields) => { sync(fields); const el = find(id); if (el) activate(el); };
    g.__lntInput = (id, fields, final) => {
        sync(fields);
        const el = find(id);
        if (el) { fire(el, "input"); if (final) fire(el, "change"); }
    };
    g.__lntImplicitSubmit = (id, fields) => {
        sync(fields);
        const el = find(id);
        const form = el && (el.form || el.closest("form"));
        if (!form) return;
        const submitter = [...form.querySelectorAll('button,input[type="submit"],input[type="image"]')]
            .find(b => b.localName !== "button" || (b.getAttribute("type") || "submit").toLowerCase() === "submit");
        if (submitter) activate(submitter); else submitForm(form, null);
    };
    g.__lntReady = () => {
        try { Object.defineProperty(document, "readyState", { configurable: true, writable: true, value: "interactive" }); } catch (e) {}
        fire(document, "DOMContentLoaded");
        try { Object.defineProperty(document, "readyState", { configurable: true, writable: true, value: "complete" }); } catch (e) {}
        try { fireWindow("load"); } catch (e) { N.report("load: " + e); }
    };

    let nextId = 1;
    g.__lntSerialize = () => {
        for (const el of document.querySelectorAll("[data-lnt-target]"))
            el.removeAttribute("data-lnt-target");
        let fragment = "";
        try { fragment = pageUrl.hash.length > 1 ? decodeURIComponent(pageUrl.hash.slice(1)) : ""; } catch (e) {}
        const target = fragment && (document.getElementById(fragment) ||
                                    [...document.querySelectorAll("a[name]")].find(a => a.getAttribute("name") === fragment));
        if (target)
            target.setAttribute("data-lnt-target", "");
        for (const el of document.querySelectorAll("*")) {
            if (!el.hasAttribute("data-lntid")) el.setAttribute("data-lntid", String(nextId++));
            if (el.localName === "input") {
                const type = (el.getAttribute("type") || "text").toLowerCase();
                if (type === "checkbox" || type === "radio") { if (el.checked) el.setAttribute("checked", ""); else el.removeAttribute("checked"); }
                else if (el.value !== undefined && el.getAttribute("value") !== el.value) el.setAttribute("value", el.value);
            }
        }
        return "<!DOCTYPE html>" + document.documentElement.outerHTML;
    };
})();
