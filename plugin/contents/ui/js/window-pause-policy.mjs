// One-pass TaskManager predicate scan shared by all wallpaper roots.
// The TaskManager model supplies current virtual-desktop filtering; activity
// membership is checked here because its activity filter is unreliable for
// demanding-attention windows.
export function scan(model, mode, activity, primaryGeometry) {
    const result = {fullscreen: false, windowPause: false, geometries: [],
        rowsVisited: 0, roleReads: 0, geometryReads: 0};
    if (!model || typeof model.count !== "number"
            || typeof model.getProperty !== "function")
        return result;

    function validRect(value) {
        return !!value && typeof value.x === "number" && Number.isFinite(value.x)
            && typeof value.y === "number" && Number.isFinite(value.y)
            && typeof value.width === "number" && Number.isFinite(value.width)
            && typeof value.height === "number" && Number.isFinite(value.height)
            && value.width > 0 && value.height > 0;
    }
    const primaryValid = validRect(primaryGeometry);
    function matchesPrimary(screenGeometry, geometry) {
        if (!primaryValid) return false;
        if (screenGeometry)
            return screenGeometry.x === primaryGeometry.x
                && screenGeometry.y === primaryGeometry.y
                && screenGeometry.width === primaryGeometry.width
                && screenGeometry.height === primaryGeometry.height;
        // Old TaskManager versions may omit ScreenGeometry. A fallback window
        // must be wholly contained by the primary screen; spanning/ambiguous
        // geometries are conservatively excluded.
        return validRect(geometry) && geometry.x >= primaryGeometry.x
            && geometry.y >= primaryGeometry.y
            && geometry.x + geometry.width <= primaryGeometry.x + primaryGeometry.width
            && geometry.y + geometry.height <= primaryGeometry.y + primaryGeometry.height;
    }

    const covered = mode === 6;
    const wantsActive = mode === 3 || mode === 4;
    const wantsMaximized = mode === 2 || mode === 4;
    for (let row = 0; row < model.count; ++row) {
        result.rowsVisited++;
        const index = typeof model.makeModelIndex === "function"
            ? model.makeModelIndex(row) : row;
        const read = (name) => {
            result.roleReads++;
            return model.getProperty(index, name);
        };
        if (read("IsWindow") !== true || read("IsMinimized") === true
                || read("IsHidden") === true)
            continue;
        const activities = read("Activities");
        if (activities && activities.length > 0 && activity
                && activities.indexOf(activity) < 0)
            continue;

        const fullscreen = read("IsFullScreen") === true;
        if (fullscreen) {
            result.fullscreen = true;
            // Fullscreen is a global hard gate and dominates every ordinary
            // mode, so no more rows/properties are needed.
            return result;
        }
        if (covered) {
            result.geometryReads++;
            result.geometries.push(read("Geometry"));
            continue;
        }
        if (result.windowPause) continue;
        if (mode === 0) continue;

        let matches = mode === 1;
        if (wantsMaximized) matches = read("IsMaximized") === true;
        if (wantsActive && !matches) matches = read("IsActive") === true;
        if (!matches) continue;

        const screenGeometry = read("ScreenGeometry");
        let isPrimary = false;
        if (validRect(screenGeometry)) {
            isPrimary = matchesPrimary(screenGeometry, null);
        } else {
            result.geometryReads++;
            isPrimary = matchesPrimary(null, read("Geometry"));
        }
        if (isPrimary) result.windowPause = true;
    }
    return result;
}
