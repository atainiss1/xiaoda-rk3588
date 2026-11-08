import QtQuick 2.15

Item {
    id: board
    objectName: "productBoard"
    width: 480; height: 510
    property var products: ({})
    property string productId: "overview"
    property string secondId: ""
    property string language: "zh"
    property string detailView: "features"
    property real reveal: 0
    property bool held: false
    property real contentFade: 1
    property int overviewPage: 0
    readonly property var overviewIds: (products.overview || {}).items || ["edge2","vim4","mind2","tea_pro"]
    readonly property int pageCount: Math.max(1,Math.ceil(overviewIds.length/4))
    readonly property var entry: products[productId] || ({})
    readonly property bool english: language === "en"
    readonly property bool bilingual: language === "bilingual"
    readonly property bool comparison: secondId.length>0
    readonly property bool overview: productId === "overview"
    signal closeRequested()
    signal productSelected(string product)
    property bool browseEnabled: false
    onProductIdChanged: { overviewPage=0;crossfade.restart() }
    onSecondIdChanged: crossfade.restart()
    onDetailViewChanged: crossfade.restart()
    SequentialAnimation {
        id: crossfade
        NumberAnimation { target: board; property: "contentFade"; from: .25; to: 1; duration: 320; easing.type: Easing.OutCubic }
    }
    Rectangle { x: 7; y: 10; width: parent.width; height: parent.height; radius: 26; color: "#193C36"; opacity: .10*board.reveal }
    Rectangle { anchors.fill: parent; radius: 26; color: "#FFFFFF"; border.color: "#C4DAD2"; border.width: 1 }
    Item {
        anchors.fill: parent; clip: true; opacity: board.contentFade
        Text { x: 24; y: 20; text: "KHADAS  /  " + (board.comparison ? "COMPARE" : board.overview ? "EXPLORE" : "DISCOVER"); font.family: "DejaVu Sans"; font.pixelSize: 12; font.letterSpacing: 2; color: "#598278" }
        Rectangle { x: 430; y: 12; width: 36; height: 36; radius: 18; color: "#EFF5F1"
            Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 26; color: "#6C8A80" }
            MouseArea { anchors.fill: parent; onClicked: board.closeRequested() }
        }
        Item {
            visible: !board.comparison && !board.overview; anchors.fill: parent
            Text { x: 24; y: 48; width: 415; text: board.entry.name || ""; font.family: "DejaVu Sans"; font.pixelSize: 38; font.weight: Font.DemiBold; color: "#24483F" }
            Text { x: 26; y: 96; width: 420; text: (board.english ? board.entry.tag_en : board.entry.tag) || ""; font.family: "Noto Sans CJK SC"; font.pixelSize: 18; color: "#6C847C"; elide: Text.ElideRight }
            Rectangle { x: 20; y: 130; width: 440; height: 170; radius: 16; color: "#F5F7F5" }
            ProductPhoto { x: 34; y: 138; width: 412; height: 154; product: board.entry }
            Column {
                x: 26; y: 318; spacing: board.bilingual ? 9 : 14
                Repeater {
                    model: board.entry[board.detailView] || []
                    Item {
                        width: 428; height: board.bilingual ? 39 : 27
                        Rectangle { x: 0; y: 10; width: 6; height: 6; radius: 3; color: "#449480" }
                        Text { x: 16; width: 412; text: board.english ? (board.entry[board.detailView+"_en"] || [])[index] || modelData : modelData; font.family: "Noto Sans CJK SC"; font.pixelSize: board.english ? 18 : 21; color: "#34584C"; elide: Text.ElideRight }
                        Text { x: 16; y: 25; width: 412; visible: board.bilingual; text: (board.entry[board.detailView+"_en"] || [])[index] || ""; font.family: "DejaVu Sans"; font.pixelSize: 12; color: "#82988E"; elide: Text.ElideRight }
                    }
                }
            }
            Text { x: 26; y: 455; width: 426; text: (board.english ? board.entry.use_en : board.entry.use) || ""; font.family: "Noto Sans CJK SC"; font.pixelSize: 15; color: "#789085"; wrapMode: Text.Wrap; maximumLineCount: 2 }
        }
        Item {
            anchors.fill: parent; visible: board.overview && !board.comparison
            Text { x: 24; y: 54; text: board.english ? "Meet Khadas." : "创意，从这里开始。"; font.family: "Noto Sans CJK SC"; font.pixelSize: 31; font.weight: Font.Medium; color: "#24483F" }
            Text { x: 26; y: 104; text: board.english ? "Computing · Expansion · Making · Audio" : "电脑 · 图形扩展 · 便携屏 · 开发板 · 音频"; font.pixelSize: 17; color: "#789085" }
            Repeater {
                model: board.overviewIds.slice(board.overviewPage*4,board.overviewPage*4+4)
                Rectangle {
                    property var product: board.products[modelData] || ({})
                    x: 24+(index%2)*222; y: 146+Math.floor(index/2)*150; width: 210; height: 138; radius: 16; color: "#F2F6F2"
                    ProductPhoto { x: 15; y: 8; width: 180; height: 89; product: parent.product }
                    Text { x: 12; y: 106; width: 186; text: product.name || ""; horizontalAlignment: Text.AlignHCenter; font.family: "DejaVu Sans"; font.pixelSize: 19; color: "#34584C" }
                    MouseArea { anchors.fill: parent; enabled: board.browseEnabled; onClicked: board.productSelected(modelData) }
                }
            }
            Rectangle {
                x: 24; y: 451; width: 166; height: 33; radius: 16; color: "#EAF2EC"
                opacity: board.overviewPage>0 ? 1 : .35
                Text { anchors.centerIn: parent; text: board.english ? "‹ Previous" : "‹ 上一页"; font.pixelSize: 16; color: "#567E6E" }
                MouseArea { anchors.fill: parent; enabled: board.overviewPage>0; onClicked: board.overviewPage-- }
            }
            Text { x: 195; y: 456; width: 90; horizontalAlignment: Text.AlignHCenter; text: (board.overviewPage+1)+" / "+board.pageCount; font.family: "DejaVu Sans"; font.pixelSize: 17; color: "#789085" }
            Rectangle {
                x: 290; y: 451; width: 166; height: 33; radius: 16; color: "#EAF2EC"
                opacity: board.overviewPage<board.pageCount-1 ? 1 : .35
                Text { anchors.centerIn: parent; text: board.english ? "Next ›" : "下一页 ›"; font.pixelSize: 16; color: "#567E6E" }
                MouseArea { anchors.fill: parent; enabled: board.overviewPage<board.pageCount-1; onClicked: board.overviewPage++ }
            }
        }
        Item {
            anchors.fill: parent; visible: board.comparison
            Text { x: 24; y: 54; text: board.english ? "Side by side." : "放在一起，看得更清楚。"; font.family: "Noto Sans CJK SC"; font.pixelSize: 28; color: "#24483F" }
            Repeater {
                model: [board.productId,board.secondId]
                Item {
                    property var product: board.products[modelData] || ({})
                    x: 24+index*224; y: 109; width: 208; height: 350
                    Rectangle { width: 208; height: 134; radius: 16; color: "#F3F6F2" }
                    ProductPhoto { x: 8; y: 8; width: 192; height: 118; product: parent.product }
                    Text { y: 149; width: 208; text: product.name || ""; font.family: "DejaVu Sans"; font.pixelSize: (product.name || "").length>12 ? 20 : 24; color: "#34584C" }
                    Column { y: board.bilingual ? 178 : 198; spacing: board.bilingual ? 9 : 14
                        Repeater { model: product[board.detailView+(board.english?"_en":"")] || []
                            Item {
                                width: 202; height: board.bilingual ? 50 : 42
                                Text { width: 202; height: board.bilingual ? 23 : 42; text: modelData; font.family: "Noto Sans CJK SC"; font.pixelSize: board.english?16:board.bilingual?17:18; wrapMode: board.bilingual ? Text.NoWrap : Text.Wrap; elide: Text.ElideRight; color: "#607E71" }
                                Text { y: 25; width: 202; height: 25; visible: board.bilingual; text: (product[board.detailView+"_en"] || [])[index] || ""; font.family: "DejaVu Sans"; font.pixelSize: 11; wrapMode: Text.Wrap; maximumLineCount: 2; color: "#879D92" }
                            }
                        }
                    }
                }
            }
            Text { x: 26; y: 461; text: board.english ? "Ask about the differences that matter to you." : "你更在意哪一点？我可以接着讲。"; font.pixelSize: 16; color: "#789085" }
        }
        Text { x: 26; y: 490; text: "khadas.com  ·  " + (board.english ? "Official sources · Oct 2026" : "官方资料 · 2026.10"); font.family: "DejaVu Sans"; font.pixelSize: 10; color: "#A3B2A9" }
    }
}
