import QtQuick
import QtTest
import "../../plugin/contents/ui" as Ui

TestCase {
    id: testCase
    name: "PyextCatalogRelay"
    when: windowShown
    visible: true

    QtObject {
        id: fakeCatalogApi
        property int snapshotRequests: 0
        property int rescanRequests: 0
        signal steamAccountsCatalogChanged(var catalog)
        function steamAccountsCatalog() {
            snapshotRequests++;
            return {schemaVersion: 1, generation: 0, installations: [], accounts: [], caches: []};
        }
        function rescanSteamAccounts() { rescanRequests++; }
    }

    Component {
        id: pyextComponent
        Ui.Pyext { catalogApi: fakeCatalogApi }
    }
    Component {
        id: catalogSignalSpyComponent
        SignalSpy { }
    }

    function test_catalogIsLazyAndSnapshotSignalIsForwarded() {
        const wrapper = createTemporaryObject(pyextComponent, testCase);
        verify(wrapper);
        const changed = createTemporaryObject(catalogSignalSpyComponent, testCase, {
            target: wrapper,
            signalName: "steamAccountsCatalogChanged"
        });
        verify(changed);

        wait(50);
        compare(fakeCatalogApi.snapshotRequests, 0);
        compare(fakeCatalogApi.rescanRequests, 0);
        compare(changed.count, 0);

        const initial = wrapper.steam_accounts_catalog();
        compare(fakeCatalogApi.snapshotRequests, 1);
        compare(initial.schemaVersion, 1);
        compare(initial.generation, 0); // initial scan is still pending

        wrapper.rescan_steam_accounts();
        compare(fakeCatalogApi.rescanRequests, 1);

        fakeCatalogApi.steamAccountsCatalogChanged({
            schemaVersion: 1,
            generation: 7,
            installations: [],
            accounts: [{key: "synthetic-account"}],
            caches: []
        });
        tryCompare(changed, "count", 1);
        const forwarded = changed.signalArguments[0][0];
        compare(forwarded.schemaVersion, 1);
        compare(forwarded.generation, 7);
        compare(forwarded.accounts.length, 1);
    }
}
