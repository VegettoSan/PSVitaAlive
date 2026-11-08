/* PSVitaAlive Catalog Schema V2 — reversible field-key adapter.
 * Only JSON field names change; all values, URLs and metadata remain intact.
 * Source name, regional title ID and overview-language map keys are data.
 */
(function (global) {
    "use strict";
    const fields = Object.freeze({
    "title_id": "tid",
    "title_ids": "tids",
    "name": "n",
    "description": "d",
    "long_description": "ld",
    "author_ids": "aids",
    "author_id": "aid",
    "category_id": "cid",
    "subcategory_ids": "sids",
    "version": "v",
    "version_date": "vd",
    "release_date": "rd",
    "updated_at": "ua",
    "date": "dt",
    "icon": "ic",
    "icon_url": "icu",
    "cover": "cv",
    "cover_url": "cvu",
    "background": "bg",
    "background_url": "bgu",
    "screenshots": "ss",
    "screenshot_urls": "ssu",
    "links": "l",
    "status": "st",
    "source": "so",
    "source_name": "sn",
    "source_id": "si",
    "source_url": "su",
    "source_catalog": "sct",
    "source_labels": "sl",
    "sources": "srcs",
    "release_page": "rp",
    "requirements": "req",
    "changelog": "cl",
    "downloads": "dl",
    "download_url": "dlu",
    "hash": "h",
    "hash2": "h2",
    "score": "sc",
    "size": "sz",
    "data_size": "dsz",
    "data_url": "du",
    "data_extract_path": "dxp",
    "data_size_bytes": "dsb",
    "game_data_languages": "gdl",
    "languages": "langs",
    "language": "lang",
    "extract_path": "xp",
    "installed_paths": "ips",
    "recommended": "rec",
    "type": "t",
    "url": "u",
    "avatar": "av",
    "bio": "b",
    "order": "ord",
    "subcategories": "subs",
    "required_fw": "rfw",
    "pkg_name": "pn",
    "rap": "rap",
    "content_id": "coid",
    "contentId": "coi",
    "region": "rg",
    "regions": "rgs",
    "zrif": "zr",
    "zRIF": "zR",
    "license": "lic",
    "required": "rq",
    "section": "sec",
    "line": "ln",
    "plugin_install_path": "pip",
    "plugin_config_section": "pcs",
    "plugin_config_line": "pcl",
    "plugins": "plg",
    "conflicts_with": "cwf",
    "replaces": "repl",
    "requires": "requires",
    "kernel_priority": "kp",
    "reboot_required": "rr",
    "last_seen": "ls",
    "original_name": "on",
    "overview": "ov",
    "install_count": "inct",
    "average_rating": "ar",
    "ratings_count": "rc",
    "likes_count": "lc",
    "comments_count": "cc",
    "user_liked": "ul",
    "user_rating": "ur",
    "dr": "dr",
    "id": "id"
});
    const reverse = Object.create(null);
    for (const [oldKey, newKey] of Object.entries(fields)) {
        if (Object.prototype.hasOwnProperty.call(reverse, newKey)) {
            throw new Error("Catalog V2 duplicate alias: " + newKey);
        }
        reverse[newKey] = oldKey;
    }
    const dynamicMaps = new Set(["sources", "title_ids", "overview"]);

    function transform(value, compact, passthrough) {
        if (Array.isArray(value)) return value.map(item => transform(item, compact, passthrough));
        if (!value || typeof value !== "object") return value;
        const out = {};
        for (const [key, item] of Object.entries(value)) {
            const oldKey = compact ? key : (reverse[key] || key);
            const newKey = passthrough ? key : compact ? (fields[key] || key) : oldKey;
            if (Object.prototype.hasOwnProperty.call(out, newKey)) {
                throw new Error("Catalog V2 field collision at " + newKey);
            }
            out[newKey] = transform(item, compact, !passthrough && dynamicMaps.has(oldKey));
        }
        return out;
    }

    global.PSVitaAliveCatalogSchemaV2 = Object.freeze({
        toV1: data => transform(data, false, false),
        toV2: data => transform(data, true, false)
    });
})(typeof window !== "undefined" ? window : globalThis);
