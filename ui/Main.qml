import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Window {
    id: window
    width: 800
    height: 1280
    visible: true
    visibility: Window.FullScreen
    title: "小哒 · 语音伙伴"
    color: "#F5F7F5"
    property bool quitPending: false
    property bool chatting: voice.busy
    onChattingChanged: {
        if (quitPending && !chatting) Qt.quit()
    }
    onClosing: {
        if (voice.busy) {
            close.accepted = false
            quitPending = true
            voice.command = "stop"
        }
    }
    Item {
        id: stage
        objectName: "presenterStage"
        width: 800; height: 1280
        anchors.centerIn: parent
        scale: Math.min(window.width / width, window.height / height)
        property bool wantsProduct: voice.productVisible === true
        property real productLayout: 0
        property real boardReveal: 0
        property bool boardHeld: false
        property real frozenRight: 512
        property real frozenTop: 218
        readonly property point gripPoint: {
            // mapToItem alone does not notify when an ancestor transform changes.
            var transformDependency=character.x+character.y+character.scale+character.wristX+character.wristY
            return character.handIn(stage)
        }
        readonly property real boardRight: boardHeld ? gripPoint.x-10 : frozenRight
        readonly property real boardTop: boardHeld ? gripPoint.y-230 : frozenTop
        property string shownProduct: "overview"
        property string shownSecond: ""
        function openProduct() {
            shownProduct=voice.product || "overview";shownSecond=voice.secondProduct || ""
            if(!wantsProduct)return
            if(boardReveal>.98 && !closeBoard.running) return
            closeBoard.stop();openBoard.restart()
        }
        onWantsProductChanged: {
            if(wantsProduct)openProduct()
            else {openBoard.stop();closeBoard.restart()}
        }
        property int productRevision: voice.productRevision || 0
        onProductRevisionChanged: if(wantsProduct)openProduct()
        SequentialAnimation {
            id: openBoard
            ScriptAction { script: { character.presenting=true;stage.boardHeld=true;character.holdBoard() } }
            ParallelAnimation {
                NumberAnimation { target: stage;property: "productLayout";to: 1;duration: 850;easing.type: Easing.InOutCubic }
                SequentialAnimation {
                    PauseAnimation { duration: 380 }
                    NumberAnimation { target: stage;property: "boardReveal";to: 1;duration: 950;easing.type: Easing.InOutCubic }
                }
            }
            ScriptAction { script: { stage.frozenRight=stage.boardRight;stage.frozenTop=stage.boardTop;stage.boardHeld=false;character.presentationHold=false;character.presenterPose();console.log("PRODUCT_BOARD shown="+stage.shownProduct) } }
        }
        SequentialAnimation {
            id: closeBoard
            ScriptAction { script: { character.presentationHold=true;character.holdBoard() } }
            PauseAnimation { duration: 750 }
            ScriptAction { script: stage.boardHeld=true }
            NumberAnimation { target: stage;property: "boardReveal";to: 0;duration: 850;easing.type: Easing.InOutCubic }
            ScriptAction { script: { stage.boardHeld=false;character.presenting=false;character.cancelGesture() } }
            NumberAnimation { target: stage;property: "productLayout";to: 0;duration: 800;easing.type: Easing.InOutCubic }
            ScriptAction { script: console.log("PRODUCT_BOARD hidden") }
        }
        property color accent: voice.phase === "error" ? "#B9604D"
                               : voice.phase === "thinking" ? "#B18A43" : "#288C87"
        property string phaseName: {
            var labels = {"idle":"在这里陪你", "connecting":"正在连接", "preparing":"准备麦克风",
                "listening":"可以说话", "hearing":"正在听", "thinking":"想一想",
                "speaking":"正在回答", "interrupting":"正在听你叫我", "between":"稍等一下", "stopping":"正在停止", "error":"语音暂时中断"}
            return labels[voice.phase] || "稍等一下"
        }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: "#F9FAF6" }
                GradientStop { position: 0.58; color: "#EAF3EF" }
                GradientStop { position: 1; color: "#F5F7F5" }
            }
        }
        Rectangle { x: -210; y: 170; width: 580; height: 580; radius: 290; color: "#ECF2EB" }
        Rectangle { x: 560; y: 430; width: 420; height: 420; radius: 210; color: "#E3EFEB"; opacity: 0.6 }

        Image {
            objectName: "khadasBrandIcon"
            x: 48; y: 32; width: 40; height: 40
            source: "assets/brand/khadas-icon-teal.svg"
            sourceSize.width: 120; sourceSize.height: 120
            fillMode: Image.PreserveAspectFit
        }
        Image {
            objectName: "khadasBrandWordmark"
            x: 100; y: 39; width: 218; height: 25
            source: "assets/brand/khadas-wordmark-teal.svg"
            sourceSize.width: 654; sourceSize.height: 75
            fillMode: Image.PreserveAspectFit
        }
        Text {
            x: 344; y: 44; text: "AI 桌面伙伴"
            font.family: "Noto Sans CJK SC"; font.pixelSize: 16
            color: "#6D827C"
        }
        Text {
            x: 46; y: 78; text: "小哒"
            font.family: "Noto Sans CJK SC"; font.pixelSize: 48; font.weight: Font.Medium
            color: "#203D39"
        }
        Text {
            x: 170; y: 104; text: "在这里，听你说。"
            font.family: "Noto Sans CJK SC"; font.pixelSize: 20; color: "#75847E"
        }
        Rectangle {
            x: 573; y: 87; width: 121; height: 45; radius: 22
            color: "#E2EEEA"
            Text {
                anchors.centerIn: parent
                text: voice.preview ? "界面预览" : (voice.busy ? "聊天中" : "待机")
                font.family: "Noto Sans CJK SC"; font.pixelSize: 19; color: "#38746D"
            }
        }
        Rectangle {
            x: 708; y: 86; width: 48; height: 48; radius: 24; color: "#EBEFEB"
            Text { anchors.centerIn: parent; text: "×"; font.pixelSize: 30; color: "#82918B" }
            MouseArea { anchors.fill: parent; onClicked: window.close() }
        }

        Item {
            id: characterArea
            z: 4
            x: 80; y: 182; width: 640; height: 553
            Rectangle {
                x: 100; y: 80; width: 440; height: 440; radius: 220
                color: "#DFECE5"
                opacity: (voice.phase === "speaking" ? 0.9 : 0.6)*(1-stage.productLayout)
                Behavior on opacity { NumberAnimation { duration: 450 } }
            }
            Rectangle {
                x: 83; y: 63; width: 474; height: 474; radius: 237
                color: "transparent"; border.width: 1; border.color: "#D5E4DC"; opacity: 1-stage.productLayout
            }
            Rectangle {
                x: 130; y: 530; width: 380; height: 13; radius: 190
                color: "#526F63"; opacity: 0.09*(1-stage.productLayout)
            }
            XiaodaCharacter {
                id: character
                x: 120+265*stage.productLayout
                y: -6+44*stage.productLayout
                scale: 1-.26*stage.productLayout
                transformOrigin: Item.TopLeft
                phase: voice.phase
                busy: voice.busy
                wakeEnabled: voice.wakeEnabled
            }
            Rectangle {
                opacity: 1-stage.productLayout
                x: 35; y: 166; width: 10; height: 10; radius: 5; color: "#93B8A5"
                SequentialAnimation on y {
                    loops: Animation.Infinite
                    NumberAnimation { from: 166; to: 151; duration: 2800; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 151; to: 166; duration: 2800; easing.type: Easing.InOutSine }
                }
            }
            Rectangle { opacity: 1-stage.productLayout; x: 586; y: 302; width: 8; height: 8; radius: 4; color: "#C0B68F" }
            Rectangle { x: 555; y: 117; width: 12; height: 12; radius: 6; color: "#AECBB9"; opacity: 0.8*(1-stage.productLayout) }
        }

        Item {
            z: 3; x: stage.boardRight-width; y: stage.boardTop
            width: 480*stage.boardReveal; height: 510; clip: true
            visible: stage.boardReveal>.001
            ProductBoard {
                id: productBoard
                products: voice.products || ({})
                productId: stage.shownProduct
                secondId: stage.shownSecond
                language: voice.productLanguage || "zh"
                detailView: voice.productView || "features"
                reveal: stage.boardReveal
                browseEnabled: !voice.busy
                onProductSelected: voice.command="browse_product:"+product
                onCloseRequested: voice.command="hide_product"
            }
        }
        Text {
            x: 515; y: 690; width: 240; z: 4
            text: voice.productLanguage === "en" ? "Your product guide" : "小哒 · 为你介绍"
            horizontalAlignment: Text.AlignHCenter; font.family: "Noto Sans CJK SC"; font.pixelSize: 18
            color: "#759689"; opacity: stage.productLayout
        }

        Rectangle {
            x: 573; y: 145; width: 182; height: 37; radius: 18
            color: "#E2EEEA"; visible: !voice.busy
            Text { anchors.centerIn: parent; text: "产品一览  ↗"; font.family: "Noto Sans CJK SC"; font.pixelSize: 17; color: "#38746D" }
            MouseArea { anchors.fill: parent; onClicked: voice.command="browse_products" }
        }
        Row {
            y: 750; spacing: 12
            anchors.horizontalCenter: parent.horizontalCenter
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 9; height: 9; radius: 5; color: stage.accent
                SequentialAnimation on opacity {
                    running: voice.busy; loops: Animation.Infinite
                    NumberAnimation { from: 1; to: 0.3; duration: 850 }
                    NumberAnimation { from: 0.3; to: 1; duration: 850 }
                }
            }
            Text {
                text: character.sleeping ? "小哒休息中" : stage.phaseName
                font.family: "Noto Sans CJK SC"; font.pixelSize: 26; font.weight: Font.Medium
                color: stage.accent
            }
            Text {
                visible: voice.busy && voice.turn > 0
                anchors.verticalCenter: parent.verticalCenter
                text: "第 " + voice.turn + " 轮"
                font.family: "DejaVu Sans"; font.pixelSize: 16; color: "#87968F"
            }
        }
        Text {
            x: 48; y: 797; width: 704
            horizontalAlignment: Text.AlignHCenter
            text: voice.preview ? "仅预览界面 · 麦克风未开启" : character.sleeping ? (voice.wakeEnabled ? "说“你好小哒”，或轻触叫醒" : "轻触小哒叫醒，点击开始即可聊天") : voice.detail
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font.family: "Noto Sans CJK SC"; font.pixelSize: 19; color: "#6D8278"
        }

        Rectangle {
            x: 48; y: 852; width: 704; height: 212; radius: 24
            color: "#FCFDFC"; border.width: 1; border.color: "#E4EBE6"
            Text {
                x: 24; y: 18; text: "我们的对话"
                font.family: "Noto Sans CJK SC"; font.pixelSize: 16; color: "#8B9C92"
            }
            Flickable {
                id: conversation
                objectName: "conversationScroll"
                x: 24; y: 52; width: 656; height: 139
                contentWidth: width; contentHeight: conversationText.height
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                property bool followLatest: true
                function follow() {
                    if (followLatest && !moving) contentY=Math.max(0,contentHeight-height)
                }
                onContentHeightChanged: Qt.callLater(follow)
                onMovementStarted: followLatest=false
                onMovementEnded: { if (atYEnd) followLatest=true }
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                Text {
                    id: conversationText
                    width: conversation.width
                    text: {
                        if (!voice.transcript)
                            return "今天有什么想聊的？\n点击下方按钮，开始我们的第一句对话。"
                        return voice.transcript
                    }
                    onTextChanged: Qt.callLater(conversation.follow)
                    textFormat: Text.PlainText
                    wrapMode: Text.Wrap
                    lineHeight: 1.24
                    font.family: "Noto Sans CJK SC"; font.pixelSize: 23
                    color: voice.question || voice.answer ? "#344F43" : "#94A298"
                }
            }
            Rectangle {
                x: 510; y: 12; width: 166; height: 32; radius: 16
                color: "#E2EEEA"; visible: !conversation.followLatest
                Text { anchors.centerIn: parent; text: "回到最新 ↓"; font.pixelSize: 16; color: "#38746D" }
                MouseArea { anchors.fill: parent; onClicked: { conversation.followLatest=true; conversation.follow() } }
            }
        }
        Button {
            id: mainButton
            x: 48; y: 1090; width: 510; height: 84
            enabled: !voice.preview && voice.phase !== "stopping" && (voice.ready || voice.phase === "error" || voice.phase === "idle")
            text: voice.phase === "stopping" ? "正在停止…" : (voice.busy ? "停止聊天" : (voice.ready ? "开始聊天" : (voice.phase === "error" ? "重试语音" : "准备语音")))
            onClicked: voice.command = voice.busy ? "stop" : "start"
            background: Rectangle {
                radius: 24
                color: mainButton.down ? "#206E69" : (voice.busy ? "#4E7770" : "#288C87")
                opacity: mainButton.enabled ? 1 : 0.55
                Behavior on color { ColorAnimation { duration: 180 } }
            }
            contentItem: Text {
                text: mainButton.text; color: "white"
                font.family: "Noto Sans CJK SC"; font.pixelSize: 28; font.weight: Font.Medium
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
        }
        Button {
            id: wakeButton
            x: 574; y: 1090; width: 178; height: 84
            enabled: !voice.busy && voice.ready && !voice.preview
            onClicked: voice.command = "wake"
            background: Rectangle { radius: 24; color: "#E6EDE8"; opacity: wakeButton.enabled ? 1 : 0.55 }
            contentItem: Text {
                text: voice.wakeEnabled ? "唤醒已开启" : "语音唤醒"
                font.family: "Noto Sans CJK SC"; font.pixelSize: 21; color: "#4F7162"
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
            }
        }
        Text {
            x: 52; y: 1202; text: "音量"
            font.family: "Noto Sans CJK SC"; font.pixelSize: 18; color: "#83928B"
        }
        Slider {
            id: volumeSlider
            x: 119; y: 1194; width: 350; height: 42
            from: 0; to: 80; stepSize: 5; value: voice.volume
            onMoved: voice.volume = Math.round(value)
            enabled: !voice.preview
            background: Rectangle {
                x: volumeSlider.leftPadding
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: volumeSlider.availableWidth; height: 4; radius: 2; color: "#D5E2DA"
                Rectangle { width: volumeSlider.visualPosition * parent.width; height: 4; radius: 2; color: "#66A79A" }
            }
            handle: Rectangle {
                x: volumeSlider.leftPadding + volumeSlider.visualPosition * (volumeSlider.availableWidth - width)
                y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                width: 23; height: 23; radius: 12; color: "#FAFCFA"
                border.color: "#7CAC9D"; border.width: 2
            }
        }
        Text {
            x: 489; y: 1204; text: voice.volume + "%"
            font.family: "DejaVu Sans"; font.pixelSize: 16; color: "#83928B"
        }
        Text {
            x: 570; y: 1202; width: 182
            horizontalAlignment: Text.AlignRight
            text: voice.notice ? voice.notice : "说完停顿 · 听完再说"
            textFormat: Text.PlainText; wrapMode: Text.Wrap
            font.family: "Noto Sans CJK SC"; font.pixelSize: 15; color: "#83928B"
        }
    }
}
