import QtQuick
import QtTest
import "../../plugin/contents/ui/js/config-draft-reconciler.mjs" as Reconciler

// Host-shaped fixture for Plasma's verified ordering:
// saveConfig(), then copy every defined cfg_ property to wallpaperConfiguration.
TestCase {
    id: testCase
    name: "ConfigHostSave"

    property var wallpaperConfiguration: ({})
    property var exportedFields: [
        "FollowWallpaperEngine",
        "WallpaperEngineAccount",
        "Fps"
    ]
    property var hostEvents: []
    property int saveCalls: 0
    property int temporaryCreated: 0
    property int temporaryDestroyed: 0
    property int realCreated: 0
    property int realDestroyed: 0

    Component {
        id: configPageComponent
        Item {
            property bool temporary: false
            property bool cfg_FollowWallpaperEngine: false
            property string cfg_WallpaperEngineAccount: ""
            property int cfg_Fps: 30
            property var reconciler: null

            function saveConfig() {
                testCase.saveCalls++
                testCase.hostEvents.push("saveConfig")
                const safeExports = Reconciler.exports(reconciler, testCase.exportedFields)
                for (let i = 0; i < testCase.exportedFields.length; ++i) {
                    const key = testCase.exportedFields[i]
                    this["cfg_" + key] = safeExports[key]
                }
                return false // Plasma ignores the return value.
            }

            function stageDraft(key, value) {
                this["cfg_" + key] = value
                reconciler = Reconciler.stage(reconciler, key, value)
            }

            function updateLive(patch) {
                reconciler = Reconciler.updateLive(reconciler, patch)
            }

            function resolve(key, choice) {
                reconciler = Reconciler.resolveConflict(reconciler, key, choice)
            }

            Component.onCompleted: {
                if (temporary)
                    testCase.temporaryCreated++
                else
                    testCase.realCreated++
            }
            Component.onDestruction: {
                if (temporary)
                    testCase.temporaryDestroyed++
                else
                    testCase.realDestroyed++
            }
        }
    }

    function init() {
        wallpaperConfiguration = {
            FollowWallpaperEngine: false,
            WallpaperEngineAccount: "steam:A",
            Fps: 30
        }
        hostEvents = []
        saveCalls = 0
        temporaryCreated = 0
        temporaryDestroyed = 0
        realCreated = 0
        realDestroyed = 0
    }

    function componentProperties(configuration, isTemporary) {
        return {
            temporary: isTemporary,
            cfg_FollowWallpaperEngine: configuration.FollowWallpaperEngine,
            cfg_WallpaperEngineAccount: configuration.WallpaperEngineAccount,
            cfg_Fps: configuration.Fps
        }
    }

    function openHostDialog() {
        const temporaryPage = configPageComponent.createObject(
            testCase, componentProperties(wallpaperConfiguration, true))
        verify(temporaryPage !== null)
        temporaryPage.destroy()
        tryCompare(testCase, "temporaryDestroyed", 1)

        const realPage = configPageComponent.createObject(
            testCase, componentProperties(wallpaperConfiguration, false))
        verify(realPage !== null)
        realPage.reconciler = Reconciler.create({
            FollowWallpaperEngine: realPage.cfg_FollowWallpaperEngine,
            WallpaperEngineAccount: realPage.cfg_WallpaperEngineAccount,
            Fps: realPage.cfg_Fps
        })
        return realPage
    }

    function hostApply(page) {
        page.saveConfig()
        for (let i = 0; i < exportedFields.length; ++i) {
            const key = exportedFields[i]
            hostEvents.push("copy:" + key)
            wallpaperConfiguration[key] = page["cfg_" + key]
        }
    }

    function test_temporaryComponentLifecycleDoesNotSaveOrMutateConfig() {
        const before = JSON.stringify(wallpaperConfiguration)
        const page = openHostDialog()
        compare(temporaryCreated, 1)
        compare(temporaryDestroyed, 1)
        compare(realCreated, 1)
        compare(page.cfg_FollowWallpaperEngine, false)
        compare(page.cfg_WallpaperEngineAccount, "steam:A")
        compare(page.cfg_Fps, 30)
        compare(saveCalls, 0)
        compare(JSON.stringify(wallpaperConfiguration), before)
        page.destroy()
        tryCompare(testCase, "realDestroyed", 1)
    }

    function test_externalFollowChangeSurvivesWhenOnlyFpsWasEdited() {
        const page = openHostDialog()
        wallpaperConfiguration.FollowWallpaperEngine = true // External tray/runtime update.
        page.updateLive({ FollowWallpaperEngine: true })
        page.stageDraft("Fps", 60) // The dialog's only edit.

        hostApply(page)

        compare(wallpaperConfiguration.FollowWallpaperEngine, true)
        compare(wallpaperConfiguration.Fps, 60)
        compare(saveCalls, 1)
        compare(hostEvents[0], "saveConfig")
        compare(hostEvents[1], "copy:FollowWallpaperEngine")
        compare(hostEvents[2], "copy:WallpaperEngineAccount")
        compare(hostEvents[3], "copy:Fps")
        page.destroy()
    }

    function test_unresolvedSameKeyConflictExportsLiveAndRetainsDraft() {
        const page = openHostDialog()
        page.stageDraft("WallpaperEngineAccount", "steam:B") // Dialog draft from baseline steam:A.
        wallpaperConfiguration.WallpaperEngineAccount = "steam:C" // Newer external value.
        page.updateLive({ WallpaperEngineAccount: "steam:C" })

        hostApply(page)

        compare(wallpaperConfiguration.WallpaperEngineAccount, "steam:C")
        compare(Reconciler.conflictKeys(page.reconciler), ["WallpaperEngineAccount"])
        compare(Reconciler.conflict(page.reconciler, "WallpaperEngineAccount"), {
            baseline: "steam:A", live: "steam:C", draft: "steam:B"
        })
        compare(saveCalls, 1)
        page.resolve("WallpaperEngineAccount", "keepLive")
        compare(Reconciler.conflictKeys(page.reconciler), [])
        compare(Reconciler.exports(page.reconciler, exportedFields).WallpaperEngineAccount, "steam:C")
        page.destroy()
    }

    function test_useMyEditRestagesAgainstLatestLiveThenDetectsNewConflict() {
        const page = openHostDialog()
        page.stageDraft("WallpaperEngineAccount", "steam:B")
        wallpaperConfiguration.WallpaperEngineAccount = "steam:C"
        page.updateLive({ WallpaperEngineAccount: "steam:C" })

        page.resolve("WallpaperEngineAccount", "useMyEdit")
        compare(Reconciler.conflict(page.reconciler, "WallpaperEngineAccount"), null)
        compare(Reconciler.exports(page.reconciler, exportedFields).WallpaperEngineAccount, "steam:B")

        wallpaperConfiguration.WallpaperEngineAccount = "steam:D"
        page.updateLive({ WallpaperEngineAccount: "steam:D" })
        compare(Reconciler.conflictKeys(page.reconciler), ["WallpaperEngineAccount"])
        compare(Reconciler.conflict(page.reconciler, "WallpaperEngineAccount"), {
            baseline: "steam:C", live: "steam:D", draft: "steam:B"
        })

        hostApply(page)
        compare(wallpaperConfiguration.WallpaperEngineAccount, "steam:D")
        page.destroy()
    }
}
