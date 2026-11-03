import QtQuick
import QtTest
import "../../plugin/contents/ui/page" as Pages

TestCase {
    id: testCase
    name: "WallpaperPagePickerIntegration"
    when: windowShown
    visible: true
    width: 1200
    height: 800

    property var root: testCase
    property var iconSizes: ({large: 48})
    property var customConf: ({favor: new Set()})
    property var wallpaperConfiguration: ({FollowWallpaperEngine: false})
    property var steamDetect: ({libraries: [], selectedLibrary: ""})
    property var followPreview: ({accounts: ["steamuser"], status: "Waiting", selection: {source: ""}})
    property var pyext: ({
        read_wallpaper_config: function() { return Promise.resolve({}); },
        write_wallpaper_config: function() { return Promise.resolve(); },
        reset_wallpaper_config: function() { return Promise.resolve(); },
        readfile: function() { return Promise.resolve("{}"); },
        get_dir_size: function() { return Promise.resolve(0); }
    })
    property var libcheck: ({wallpaper: true, qtwebchannel: false})

    property string cfg_FilterStr: ""
    property int cfg_SortMode: 0
    property string cfg_SteamLibraryPath: ""
    property bool cfg_FollowWallpaperEngine: true
    property string cfg_WallpaperEngineAccount: "steamuser"
    property string cfg_WallpaperSource: ""
    property string cfg_WallpaperWorkShopId: ""
    property string cfg_WallpaperType: ""
    property string cfg_DisplayMode: ""
    property bool cfg_MuteAudio: false
    property real cfg_Volume: 1
    property real cfg_Speed: 1
    property var cfg_PerOptChanged: ({})

    property var wpListModel: null
    property var lastManual: null
    property var lastBrowse: null
    property var lastFollow: null
    property var pageUnderTest: null

    signal rescanSteam()
    signal saveCustomConf()

    Component {
        id: modelComponent
        QtObject {
            id: fakeModel
            property var browseScope: null
            property int countNoFilter: 0
            property ListModel model: ListModel {}
            signal modelStartSync()
            signal modelRefreshed()
            function refresh() {}
        }
    }
    Component {
        id: pageComponent
        Pages.WallpaperPage {
            width: 1200
            height: 800
            wallpaperListModel: testCase.wpListModel
            onManualWallpaperSelected: wallpaper => testCase.lastManual = wallpaper
            onBrowseScopeSelected: scope => testCase.lastBrowse = scope
            onFollowSourceSelected: binding => testCase.lastFollow = binding
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    function init() {
        wpListModel = createTemporaryObject(modelComponent, testCase);
        verify(wpListModel);
        wpListModel.browseScope = null;
        cfg_FollowWallpaperEngine = true;
        cfg_WallpaperSource = "";
        cfg_WallpaperWorkShopId = "";
        lastManual = null;
        lastBrowse = null;
        lastFollow = null;
    }

    function cleanup() {
        if (pageUnderTest) {
            waitForRendering(pageUnderTest);
            pageUnderTest.destroy();
            pageUnderTest = null;
            wait(700);
        }
    }

    function createPage(props) {
        const page = createTemporaryObject(pageComponent, testCase, props || {});
        verify(page);
        pageUnderTest = page;
        return page;
    }
    function verifiedCatalog() {
        return {schemaVersion: 1, accounts: [
            {key: "steam:76561190000000001", steamId64: "76561190000000001",
                accountId32: "12345678", label: "Alice", identityStatus: "verified"}
        ], caches: []};
    }

    function test_catalogUnavailableKeepsLegacyFollowAndBrowseDefault() {
        const page = createPage();
        compare(findChild(page, "browseAccountPicker").visible, false);
        compare(findChild(page, "followSourcePicker").visible, false);
        compare(page.followSelection.mode, "legacySession");
        compare(page.followSelection.sourceKey, "config:steamuser");
        compare(wpListModel.browseScope.mode, "allDownloaded");
        compare(cfg_FollowWallpaperEngine, true);
    }

    function test_browserSettingsAreCollapsedUntilRequested() {
        const page = createPage({
            accountCatalogSnapshot: verifiedCatalog(),
            followSources: [{mode: "legacySession", accountKey: "", sourceKey: "config:steamuser",
                label: "Session", available: true}],
            useFollowSourcePicker: true
        });
        const summary = findChild(page, "libraryScopeSummary");
        const settings = findChild(page, "browserSettingsButton");
        const panel = findChild(page, "browserSettingsPanel");
        verify(summary && settings && panel);
        verify(summary.text.includes("All downloaded"));
        compare(page.settingsExpanded, false);
        compare(panel.visible, false);
        verify(findChild(page, "followWallpaperEngineCheckBox").visible);

        settings.clicked();
        compare(page.settingsExpanded, true);
        verify(panel.visible);
        verify(findChild(page, "browseAccountPicker").visible);
        verify(findChild(page, "followSourcePicker").visible);
        verify(findChild(page, "libraryCombo"));
        verify(findChild(page, "singleLibrary"));
    }

    function test_browseAndFollowSelectionsAreIndependentWhileFollowing() {
        const alice = "steam:76561190000000001";
        const page = createPage({
            accountCatalogSnapshot: verifiedCatalog(),
            followSources: [
                {mode: "account", accountKey: alice, sourceKey: "config:Alice", label: "Saved selection", available: true},
                {mode: "legacySession", accountKey: "", sourceKey: "config:steamuser", label: "Session", available: true}
            ],
            useFollowSourcePicker: true
        });
        page.settingsExpanded = true;
        const browse = findChild(page, "browseAccountPicker");
        const follow = findChild(page, "followSourcePicker");
        verify(browse.visible && follow.visible);

        page.acceptBrowseScope("selectedAccounts", [alice], true);
        compare(lastBrowse.mode, "selectedAccounts");
        compare(page._activeBrowseScope.accountKeys, [alice]);
        verify(page.wallpaperListModel === wpListModel);
        compare(wpListModel.browseScope.accountKeys, [alice]);
        compare(cfg_FollowWallpaperEngine, true);
        compare(page._activeFollowSelection.mode, "legacySession");

        page.acceptFollowSource({mode: "account", accountKey: alice, sourceKey: "config:Alice"});
        compare(lastFollow.mode, "account");
        compare(page._activeFollowSelection.accountKey, alice);
        compare(cfg_FollowWallpaperEngine, true);
        compare(wpListModel.browseScope.accountKeys, [alice]);
        verify(findChild(page, "wallpaperGridLoader").enabled);
        compare(page.allowIdentifiedCurrent, false);
        verify(!follow.choices.some(choice => choice.binding.mode === "identifiedCurrent"));
    }

    function test_manualSelectionStopsFollowOnlyAfterExplicitApply() {
        const page = createPage();
        const wallpaper = {workshopid: "42", path: "/tmp/project", file: "scene.pkg",
            type: "scene", title: "Test scene", preview: "", loaded: false,
            contentrating: "Everyone", tags: [], favor: false, playlists: []};
        page.selectWallpaper(wallpaper);
        compare(cfg_FollowWallpaperEngine, true);
        compare(cfg_WallpaperSource, "");
        compare(cfg_WallpaperWorkShopId, "");
        compare(page.pendingManualSelection.title, "Test scene");
        page.applyPendingManualSelection();
        compare(cfg_FollowWallpaperEngine, false);
        compare(cfg_WallpaperSource, "/tmp/project/scene.pkg+scene");
        compare(cfg_WallpaperWorkShopId, "42");
        compare(lastManual.title, "Test scene");
    }

    function test_wideAndNarrowDetailsNavigationPreservesBrowseState() {
        const page = createPage({width: 1200});
        const details = findChild(page, "wallpaperDetailsView");
        const showDetails = findChild(page, "showDetailsButton");
        const description = findChild(page, "wallpaperDescription");
        const options = findChild(page, "wallpaperOptions");
        verify(details && description && options);
        verify(details.visible); // wide split view remains visible
        verify(options.visible);
        compare(showDetails.visible, false);
        compare(findChild(page, "backToWallpapersButton").visible, false);
        waitForRendering(page);

        page.width = 720;
        tryCompare(page, "narrowLayout", true);
        compare(details.visible, false);
        verify(showDetails.visible);
        verify(findChild(page, "wallpaperBrowseView").visible);
        showDetails.clicked();
        compare(page.detailsView, true);
        verify(details.visible);
        compare(findChild(page, "wallpaperBrowseView").visible, false);
        verify(findChild(page, "backToWallpapersButton").visible);
        verify(description.visible);
        verify(options.visible);
        compare(cfg_FollowWallpaperEngine, true);
        compare(findChild(page, "followSourcePicker").visible, false);
        waitForRendering(page);

        findChild(page, "backToWallpapersButton").clicked();
        compare(page.detailsView, false);
        compare(details.visible, false);
        verify(findChild(page, "wallpaperBrowseView").visible);
        const followCheck = findChild(page, "followWallpaperEngineCheckBox");
        verify(followCheck.visible);
        compare(followCheck.checked, true);
        compare(cfg_FollowWallpaperEngine, true);
    }

    function test_playlistChipsShowLabelsAndKeepNamespacedKeysDistinct() {
        const page = createPage({width: 1200});
        cfg_WallpaperSource = "/tmp/active/scene.pkg+scene";
        cfg_WallpaperWorkShopId = "42";
        const aliceKey = "playlist:account-steam-1:favorites";
        const bobKey = "playlist:account-steam-2:favorites";
        wpListModel.model.append({
            workshopid: "42", path: "file:///tmp/project", file: "scene.pkg",
            type: "scene", title: "Playlist chip fixture", preview: "",
            loaded: true, favor: false, contentrating: "Everyone", tags: [],
            playlists: [
                {key: aliceKey, text: "Favorites"},
                {key: bobKey, text: "Favorites"}
            ]
        });
        wpListModel.modelRefreshed();

        tryVerify(() => {
            const alice = findChild(page, "wallpaper-chip-" + aliceKey);
            const bob = findChild(page, "wallpaper-chip-" + bobKey);
            return alice && bob && alice.visible && bob.visible;
        });
        const aliceChip = findChild(page, "wallpaper-chip-" + aliceKey);
        const bobChip = findChild(page, "wallpaper-chip-" + bobKey);
        compare(aliceChip.contentItem.text, "Favorites");
        compare(bobChip.contentItem.text, "Favorites");
        compare(wpListModel.model.get(0).playlists.get(0).key, aliceKey);
        compare(wpListModel.model.get(0).playlists.get(1).key, bobKey);
        verify(aliceKey !== bobKey);
    }

    function test_detailsRemainReachableAtBreakpointAndWithLargeScrollbar() {
        const page = createPage({width: 1040});
        const details = findChild(page, "wallpaperDetailsView");
        const showDetails = findChild(page, "showDetailsButton");
        const scrollBar = findChild(page, "wallpaperDetailsScrollBar");
        verify(details && showDetails && scrollBar);
        compare(page.narrowLayout, false);
        verify(details.visible);
        compare(showDetails.visible, false);

        page.width = 1039;
        tryCompare(page, "narrowLayout", true);
        verify(showDetails.visible);
        compare(details.visible, false);

        page.width = 1200;
        scrollBar.width = 80; // enlarged scale/style scrollbar allowance
        tryCompare(page, "narrowLayout", true);
        compare(details.visible, false);
        verify(showDetails.visible);
        showDetails.clicked();
        verify(details.visible);
        compare(findChild(page, "backToWallpapersButton").visible, true);
    }
}
