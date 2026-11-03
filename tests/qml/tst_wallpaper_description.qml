import QtQuick
import QtTest
import "../../plugin/contents/ui/components" as Components
import "../../plugin/contents/ui/js/bbcode.mjs" as BBCode

TestCase {
    id: testCase
    name: "WallpaperDescription"
    when: windowShown
    width: 420
    height: 300

    Component {
        id: descriptionComponent
        Components.WallpaperDescription { }
    }

    function test_plainTextAndDesktopPalette() {
        let reads = 0;
        let receivedPath = "";
        const component = createTemporaryObject(descriptionComponent, testCase, {
            projectUrl: "/tmp/conch-street/project.json",
            readFile: path => {
                reads++;
                receivedPath = path;
                return Promise.resolve(JSON.stringify({
                    description: "Conch Street, Bikini Bottom. SpongeBob's pineapple home is shown at low tide."
                }));
            }
        });
        verify(component);
        tryCompare(component, "status", "ready");
        verify(component.description.includes(
            "Conch Street, Bikini Bottom. SpongeBob's pineapple home is shown at low tide."));
        compare(reads, 1);
        compare(receivedPath, "/tmp/conch-street/project.json");
        compare(component.textControl.readOnly, true);
        compare(component.textControl.selectByMouse, true);
        compare(component.textControl.background.color.a, 0);
        verify(component.textControl.palette.text !== component.textControl.palette.base);
    }

    function test_bbcodeSubsetAndEscaping() {
        const html = BBCode.parser.parse("[h2]Heading[/h2][p][b]bold[/b], [i]italic[/i], [u]underlined[/u][/p][list][*]one[/*][*]two[/*][/list]");
        verify(html.includes("<h2>Heading</h2>"));
        verify(html.includes("<strong>bold</strong>"));
        verify(html.includes("<em>italic</em>"));
        verify(html.includes("<u>underlined</u>"));
        verify(html.includes("<ul><li>one</li><li>two</li></ul>"));

        const rawHtml = BBCode.parser.parse("<img src=x onerror='bad()'> & text");
        verify(rawHtml.includes("&lt;img src=x onerror='bad()'&gt; &amp; text"));
        verify(!rawHtml.includes("<img"));

        const malformed = BBCode.parser.parse("[b]open [color=red]literal[/color]");
        verify(malformed.includes("[b]open [color=red]literal[/color]"));
        verify(!malformed.includes("<strong>"));
    }

    function test_safeLinksAndImages() {
        const html = BBCode.parser.parse(
            "[url=https://example.com/path?a=1&amp;b=2]safe[/url] "
            + "[url=javascript:alert('x')]unsafe[/url] [email]me@example.com[/email] "
            + "[img]https://example.com/picture.jpg[/img] [img=javascript:bad()]bad[/img]");
        verify(html.includes('<a href="https://example.com/path?a=1&amp;amp;b=2">safe</a>'));
        verify(html.includes("[url=javascript:alert('x')]unsafe[/url]"));
        verify(html.includes('<a href="mailto:me@example.com">me@example.com</a>'));
        verify(html.includes('<a href="https://example.com/picture.jpg">Image: https://example.com/picture.jpg</a>'));
        verify(html.includes("[img=javascript:bad()]bad[/img]"));
        verify(!html.includes("<img"));

        const injected = BBCode.parser.parse('[url=https://example.com/" onclick="bad()]click[/url]');
        verify(!injected.includes("<a "));
        verify(injected.includes('[url=https://example.com/" onclick="bad()]click[/url]'));
    }

    function test_pathologicalMarkupIsBounded() {
        const nestedInput = "[b]".repeat(1000) + "nested" + "[/b]".repeat(1000);
        const nestedHtml = BBCode.parser.parse(nestedInput);
        verify(nestedHtml.includes("[b]"));
        verify(nestedHtml.includes("nested"));

        const imageInput = "[img]https://example.com/image.png[/img] ".repeat(2000);
        const imageHtml = BBCode.parser.parse(imageInput);
        compare(imageHtml.split('<a href="https://example.com/image.png">').length - 1, 2000);
        verify(!imageHtml.includes("<img"));
    }

    function test_descriptionTruncation() {
        const preview = BBCode.parser.parseWithLimit("x".repeat(BBCode.parser.maxInputBytes + 1));
        compare(preview.truncated, true);
        verify(preview.html.includes("[Description preview truncated at 128 KiB]"));
        verify(preview.html.startsWith("x".repeat(BBCode.parser.maxInputBytes)));

        const unicodePreview = BBCode.parser.parseWithLimit("é".repeat(65537));
        compare(unicodePreview.truncated, true);
        verify(unicodePreview.html.startsWith("é".repeat(65536)));

        const component = createTemporaryObject(descriptionComponent, testCase, {
            projectUrl: "/tmp/long/project.json",
            readFile: path => Promise.resolve(JSON.stringify({description: "x".repeat(BBCode.parser.maxInputBytes + 1)}))
        });
        verify(component);
        tryCompare(component, "status", "ready");
        compare(component.truncated, true);
        verify(component.description.includes("[Description preview truncated at 128 KiB]"));
    }

    function test_staleReadIgnoredAfterSelectionChange() {
        let resolveA = null;
        const component = createTemporaryObject(descriptionComponent, testCase, {
            projectUrl: "file:///tmp/A/project.json",
            readFile: path => path.toString().includes("/A/")
                ? new Promise(resolve => { resolveA = resolve; })
                : Promise.resolve(JSON.stringify({description: "Description B"}))
        });
        verify(component);
        tryVerify(() => resolveA !== null);
        component.projectUrl = "file:///tmp/B/project.json";
        tryVerify(() => component.description.includes("Description B"));
        resolveA(JSON.stringify({description: "Description A"}));
        wait(20);
        verify(component.description.includes("Description B"));
    }

    function test_destroyDuringPendingRead() {
        failOnWarning(/Cannot read property|TypeError|deleted object/i);
        let resolveRead = null;
        const component = createTemporaryObject(descriptionComponent, testCase, {
            projectUrl: "file:///tmp/pending/project.json",
            readFile: path => new Promise(resolve => { resolveRead = resolve; })
        });
        verify(component);
        tryVerify(() => resolveRead !== null);
        component.destroy();
        wait(0);
        resolveRead(JSON.stringify({description: "Late description"}));
        wait(20);
        verify(true);
    }
}
