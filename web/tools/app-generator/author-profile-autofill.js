/* Public GitHub / GitLab author-profile import for the static JSON generator.
 * No API tokens, proxy, server or automatic publication. User reviews the result.
 */
(() => {
    "use strict";

    const DEFAULT_AVATAR = "https://raw.githubusercontent.com/VegettoSan/PSVitaAlive/main/authors/icon/autoricon.png";
    const $ = id => document.getElementById(id);
    let lookupTimer = null;
    let activeRequest = null;
    let requestVersion = 0;

    function slugify(value) {
        return String(value || "")
            .normalize("NFKD")
            .replace(/[\u0300-\u036f]/g, "")
            .toLowerCase()
            .replace(/[^a-z0-9]+/g, "-")
            .replace(/^-+|-+$/g, "")
            .slice(0, 80);
    }

    function profileFromUrl(value) {
        let url;
        try {
            url = new URL(String(value || "").trim());
        } catch {
            throw new Error("Enter a complete GitHub or GitLab profile URL.");
        }
        const domain = url.hostname.toLowerCase().replace(/^www\./, "");
        if (url.protocol !== "https:" || !["github.com", "gitlab.com"].includes(domain) || url.port || url.username || url.password) {
            throw new Error("Use an HTTPS profile URL from github.com or gitlab.com.");
        }
        const parts = url.pathname.split("/").filter(Boolean);
        if (parts.length !== 1) {
            throw new Error("Use a user profile URL, not a repository or project URL.");
        }
        let username;
        try {
            username = decodeURIComponent(parts[0]);
        } catch {
            throw new Error("Invalid username in the profile URL.");
        }
        if (!/^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$/.test(username)) {
            throw new Error("The profile URL does not contain a valid username.");
        }
        if (domain === "github.com" && !/^[a-zA-Z0-9](?:[a-zA-Z0-9-]{0,37}[a-zA-Z0-9])?$/.test(username)) {
            throw new Error("The GitHub username is invalid.");
        }
        return {
            provider: domain === "github.com" ? "GitHub" : "GitLab",
            username,
            url: `https://${domain}/${encodeURIComponent(username)}`
        };
    }

    function setStatus(message, state = "") {
        const status = $("new-author-lookup-status");
        if (!status) return;
        status.textContent = message;
        status.dataset.state = state;
    }

    function isImageUrl(value) {
        try {
            const url = new URL(value);
            return url.protocol === "https:" || url.protocol === "http:";
        } catch {
            return false;
        }
    }

    function updatePreview() {
        const name = $("new-author-name").value.trim();
        const id = $("new-author-id").value.trim();
        $("new-author-name-preview").textContent = name || "New author";
        $("new-author-id-preview").textContent = id ? `ID: ${id}` : "Profile details";
        const avatar = $("new-author-avatar").value.trim();
        const preview = $("new-author-avatar-preview");
        const desired = isImageUrl(avatar) ? avatar : DEFAULT_AVATAR;
        if (preview.dataset.desired !== desired) {
            preview.dataset.desired = desired;
            preview.src = desired;
        }
    }

    function cancelLookup() {
        clearTimeout(lookupTimer);
        lookupTimer = null;
        requestVersion++;
        if (activeRequest) activeRequest.abort();
        activeRequest = null;
        const button = $("lookup-author-profile");
        if (button) button.disabled = false;
    }

    async function fetchJson(url, signal) {
        const response = await fetch(url, {
            method: "GET",
            headers: { Accept: "application/json" },
            signal
        });
        if (!response.ok) {
            if (response.status === 404) throw new Error("Profile not found. Check the username.");
            if (response.status === 403 || response.status === 429) {
                throw new Error("Profile service is temporarily rate-limited. Try again later or enter the details manually.");
            }
            throw new Error(`Profile service returned HTTP ${response.status}. You can fill the fields manually.`);
        }
        return response.json();
    }

    async function loadPublicProfile(profile, signal) {
        if (profile.provider === "GitHub") {
            const data = await fetchJson(`https://api.github.com/users/${encodeURIComponent(profile.username)}`, signal);
            if (!data || !data.login) throw new Error("GitHub did not return a valid public user profile.");
            return {
                username: data.login,
                name: data.name || data.login,
                bio: data.bio || "",
                avatar: data.avatar_url || "",
                url: data.html_url || profile.url
            };
        }

        const users = await fetchJson(`https://gitlab.com/api/v4/users?username=${encodeURIComponent(profile.username)}`, signal);
        const summary = Array.isArray(users) ? users.find(user =>
            String(user.username || "").toLowerCase() === profile.username.toLowerCase()
        ) : null;
        if (!summary) throw new Error("GitLab user not found. Check that this is a public user profile.");
        // A GitLab namespace can be a group; only accept the exact public user URL.
        const publicUrl = summary.web_url || profile.url;
        if (new URL(publicUrl).pathname.replace(/\/$/, "").toLowerCase() !==
            new URL(profile.url).pathname.replace(/\/$/, "").toLowerCase()) {
            throw new Error("This URL does not match a public GitLab user profile.");
        }
        let details = summary;
        if (summary.id != null) {
            try {
                details = await fetchJson(`https://gitlab.com/api/v4/users/${encodeURIComponent(summary.id)}`, signal);
            } catch (error) {
                if (signal.aborted) throw error;
                // Public list data still provides a usable name and avatar.
                details = summary;
            }
        }
        return {
            username: summary.username,
            name: details.name || summary.name || summary.username,
            bio: details.bio || summary.bio || "",
            avatar: details.avatar_url || summary.avatar_url || "",
            url: summary.web_url || profile.url
        };
    }

    function setField(id, value) {
        const field = $(id);
        field.value = String(value || "");
        field.dispatchEvent(new Event("input", { bubbles: true }));
    }

    function ensureProfileLink(profile, url) {
        const list = $("new-author-links");
        let row = list.querySelector('[data-imported-profile="true"]');
        if (!row) {
            row = [...list.querySelectorAll(".author-link-item")].find(item =>
                !item.querySelector(".author-link-url").value.trim() &&
                !item.querySelector(".author-link-name").value.trim()
            );
        }
        if (!row) {
            $("add-author-link").click();
            row = list.lastElementChild;
        }
        if (!row) return;
        row.dataset.importedProfile = "true";
        row.querySelector(".author-link-type").value = "Repository";
        row.querySelector(".author-link-name").value = profile.provider;
        row.querySelector(".author-link-url").value = url;
        list.querySelectorAll(".author-link-recommended").forEach(check => { check.checked = false; });
        row.querySelector(".author-link-recommended").checked = true;
    }

    async function lookupProfile() {
        clearTimeout(lookupTimer);
        lookupTimer = null;
        let profile;
        try {
            profile = profileFromUrl($("new-author-profile-url").value);
        } catch (error) {
            setStatus(error.message, "error");
            return;
        }

        cancelLookup();
        const version = requestVersion;
        const controller = new AbortController();
        activeRequest = controller;
        const button = $("lookup-author-profile");
        button.disabled = true;
        setStatus(`Checking ${profile.provider} profile…`, "loading");

        try {
            const data = await loadPublicProfile(profile, controller.signal);
            if (controller.signal.aborted || version !== requestVersion || $("new-author-dialog").hidden) return;

            // Use the public account handle as the stable ID; display name remains editable.
            const id = slugify(data.username);
            if (!id) throw new Error("Could not derive a valid author ID from this username.");
            const name = String(data.name || data.username).trim().slice(0, 120);
            const bio = String(data.bio || "").trim() || "Developer";
            const avatar = isImageUrl(data.avatar) ? data.avatar : DEFAULT_AVATAR;
            setField("new-author-name", name);
            setField("new-author-id", id);
            setField("new-author-avatar", avatar);
            setField("new-author-bio", bio);
            ensureProfileLink(profile, data.url);
            updatePreview();

            const conflict = [...$("authors").options].find(option => option.value === id);
            if (conflict) {
                setStatus(`Profile loaded, but author ID "${id}" already exists. Select the existing author instead of creating a duplicate.`, "warning");
            } else {
                setStatus(`${profile.provider} profile imported. Review the details before adding the author.`, "success");
            }
        } catch (error) {
            if (!controller.signal.aborted && version === requestVersion) {
                setStatus(error instanceof TypeError
                    ? "Could not connect to the public profile API. You can enter the details manually."
                    : (error.message || "Could not load this profile."), "error");
            }
        } finally {
            if (version === requestVersion) {
                activeRequest = null;
                button.disabled = false;
            }
        }
    }

    function scheduleLookup() {
        cancelLookup();
        setStatus("");
        const value = $("new-author-profile-url").value.trim();
        if (!value) return;
        try {
            profileFromUrl(value);
        } catch {
            return; // Allow the user to finish typing a URL before showing an error.
        }
        lookupTimer = setTimeout(lookupProfile, 650);
    }

    function setup() {
        const dialog = $("new-author-dialog");
        if (!dialog || !$("new-author-profile-url")) return;
        $("new-author-profile-url").addEventListener("input", scheduleLookup);
        $("new-author-profile-url").addEventListener("keydown", event => {
            if (event.key === "Enter") {
                event.preventDefault();
                lookupProfile();
            }
        });
        $("lookup-author-profile").addEventListener("click", lookupProfile);
        for (const field of ["new-author-name", "new-author-id", "new-author-avatar"]) {
            $(field).addEventListener("input", updatePreview);
        }
        $("new-author-avatar-preview").addEventListener("error", event => {
            const image = event.target;
            if (image.src !== DEFAULT_AVATAR) {
                image.src = DEFAULT_AVATAR;
            }
        });
        dialog.addEventListener("author-dialog-open", () => {
            cancelLookup();
            setStatus("");
            updatePreview();
        });
        dialog.addEventListener("author-dialog-close", cancelLookup);
        updatePreview();
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", setup, { once: true });
    } else {
        setup();
    }
})();
