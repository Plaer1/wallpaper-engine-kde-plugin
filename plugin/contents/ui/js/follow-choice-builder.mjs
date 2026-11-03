// Build stable FollowSourcePicker candidates from verified catalog identities
// and the follower's legacy section names. Legacy entries remain explicitly
// unassociated; current-session identity is intentionally not synthesized.

const PUBLIC_INDIVIDUAL_BASE = "76561197960265728";
const MAX_ACCOUNT_ID32 = "4294967295";

function canonicalAbsolutePath(value) {
    if (typeof value !== "string" || !value.trim()) return "";
    const path = value.trim().replace(/\\/g, "/");
    if (!path.startsWith("/") || path.startsWith("//")) return "";
    const parts = [];
    for (const part of path.split("/")) {
        if (!part || part === ".") continue;
        if (part === "..") {
            if (parts.length) parts.pop();
            continue;
        }
        parts.push(part);
    }
    return `/${parts.join("/")}`;
}

function canonicalDecimal(value) {
    return typeof value === "string" && /^(0|[1-9][0-9]*)$/.test(value);
}

function compareDecimal(left, right) {
    if (left.length !== right.length) return left.length < right.length ? -1 : 1;
    return left < right ? -1 : left > right ? 1 : 0;
}

function subtractDecimal(value, subtrahend) {
    let borrow = 0;
    let result = "";
    for (let i = value.length - 1, j = subtrahend.length - 1; i >= 0; i--, j--) {
        let digit = Number(value[i]) - (j >= 0 ? Number(subtrahend[j]) : 0) - borrow;
        borrow = digit < 0 ? 1 : 0;
        if (borrow) digit += 10;
        result = String(digit) + result;
    }
    return result.replace(/^0+/, "") || "0";
}

function accountId32FromSteamId64(value) {
    if (!canonicalDecimal(value) || value === "0" || value.length > 20) return "";
    if (compareDecimal(value, PUBLIC_INDIVIDUAL_BASE) <= 0) return "";
    const accountId32 = subtractDecimal(value, PUBLIC_INDIVIDUAL_BASE);
    if (accountId32 === "0" || compareDecimal(accountId32, MAX_ACCOUNT_ID32) > 0) return "";
    return accountId32;
}

function verifiedCatalogAccounts(catalog) {
    if (!catalog || typeof catalog !== "object" || catalog.schemaVersion !== 1
        || !Number.isSafeInteger(catalog.generation) || catalog.generation < 1
        || !Array.isArray(catalog.accounts)) return [];

    const unique = new Map();
    for (const account of catalog.accounts) {
        if (!account || typeof account !== "object" || account.identityStatus !== "verified") continue;
        const id = account.steamId64;
        const derivedAccountId32 = accountId32FromSteamId64(id);
        if (!derivedAccountId32 || account.key !== `steam:${id}`
            || account.accountId32 !== derivedAccountId32) continue;
        const label = typeof account.label === "string" && account.label.trim()
            ? account.label.trim() : `Steam account ${derivedAccountId32}`;
        unique.set(account.key, {
            key: account.key,
            steamId64: id,
            accountId32: derivedAccountId32,
            label
        });
    }
    return [...unique.values()].sort((left, right) => left.key < right.key ? -1 : left.key > right.key ? 1 : 0);
}

function encodeSection(section) {
    return encodeURIComponent(section).replace(/[!'()*]/g, character =>
        `%${character.charCodeAt(0).toString(16).toUpperCase()}`);
}

function legacySectionValues(values) {
    if (typeof values === "string" || !values || typeof values !== "object") return [];
    try {
        if (Array.isArray(values)) return values.length <= 4096 ? values : [];
        // Qt exposes a native QStringList to QML as an array-like object with
        // an integer length, but Array.isArray() remains false for that wrapper.
        const length = values.length;
        if (!Number.isSafeInteger(length) || length < 0 || length > 4096) return [];
        const result = [];
        for (let i = 0; i < length; ++i) result.push(values[i]);
        return result;
    } catch (reason) {
        return [];
    }
}

function uniqueLegacySections(values) {
    const entries = legacySectionValues(values);
    const unique = new Set();
    try {
        for (const value of entries) {
            if (typeof value !== "string" || !value.trim()) continue;
            unique.add(value);
        }
    } catch (reason) {
        return [];
    }
    return [...unique].sort((left, right) => left < right ? -1 : left > right ? 1 : 0);
}

/**
 * Return stable candidates shaped for the FollowSourcePicker/runtime binding:
 * {mode, accountKey, sourceKey, label, identityVerified}.
 *
 * `legacySections` comes from WallpaperEngineFollower.accounts. Account choices
 * require an available, nonzero catalog generation and fully matching verified
 * SteamID64 fields. The source key is anchored to the config path; account mode
 * additionally carries the SteamID64. Legacy sections never receive an
 * accountKey, and `identifiedCurrent` is not offered before the G2 gate.
 */
export function buildFollowChoices(catalog, legacySections, configPath, savedBinding = null) {
    const path = canonicalAbsolutePath(configPath);
    if (!path) return [];
    const configSource = `config:${path}`;
    const choices = [];
    const seen = new Set();
    const pushUnique = choice => {
        const uniqueKey = `${choice.mode}\u0000${choice.accountKey}\u0000${choice.sourceKey}`;
        if (seen.has(uniqueKey)) return;
        seen.add(uniqueKey);
        choices.push(choice);
    };

    const accounts = verifiedCatalogAccounts(catalog);
    const labelCounts = new Map();
    for (const account of accounts) {
        const folded = account.label.toLowerCase();
        labelCounts.set(folded, (labelCounts.get(folded) || 0) + 1);
    }
    for (const account of accounts) {
        const duplicateName = labelCounts.get(account.label.toLowerCase()) > 1;
        const detail = duplicateName ? `Saved selection (${account.accountId32})` : "Saved selection";
        pushUnique({
            mode: "account",
            accountKey: account.key,
            sourceKey: `${configSource}#account:${account.steamId64}`,
            label: detail,
            identityVerified: true
        });
    }

    for (const section of uniqueLegacySections(legacySections)) {
        pushUnique({
            mode: "legacySession",
            accountKey: "",
            sourceKey: `${configSource}#section:${encodeSection(section)}`,
            label: `Unassociated Wallpaper Engine session — ${section}`,
            identityVerified: false
        });
    }

    const savedManualMapping = buildSavedManualMappingChoice(savedBinding, accounts, choices, configSource);
    if (savedManualMapping) pushUnique(savedManualMapping);

    return choices.sort((left, right) => {
        const leftRank = left.mode === "account" ? 0 : left.mode === "manualMapping" ? 1 : 2;
        const rightRank = right.mode === "account" ? 0 : right.mode === "manualMapping" ? 1 : 2;
        if (leftRank !== rightRank) return leftRank - rightRank;
        return left.sourceKey < right.sourceKey ? -1 : left.sourceKey > right.sourceKey ? 1 : 0;
    });
}

function buildSavedManualMappingChoice(savedBinding, accounts, choices, configSource) {
    if (!savedBinding || typeof savedBinding !== "object" || Array.isArray(savedBinding)
        || savedBinding.mode !== "manualMapping"
        || typeof savedBinding.accountKey !== "string"
        || typeof savedBinding.sourceKey !== "string") return null;

    const account = accounts.find(candidate => candidate.key === savedBinding.accountKey);
    if (!account) return null;
    const marker = `${configSource}#section:`;
    if (!savedBinding.sourceKey.startsWith(marker)) return null;

    const encodedSection = savedBinding.sourceKey.slice(marker.length);
    if (!encodedSection) return null;
    let section;
    try { section = decodeURIComponent(encodedSection); }
    catch (reason) { return null; }
    if (!section || encodeSection(section) !== encodedSection) return null;
    const isDiscovered = choices.some(choice => choice.mode === "legacySession"
        && choice.sourceKey === savedBinding.sourceKey);
    if (!isDiscovered) return null;

    return {
        mode: "manualMapping",
        accountKey: savedBinding.accountKey,
        sourceKey: savedBinding.sourceKey,
        label: `User-associated mapping — ${account.label} → ${section}`,
        identityVerified: false,
        userConfirmed: true
    };
}
