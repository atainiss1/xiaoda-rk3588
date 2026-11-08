import QtQuick 2.15
Item {
    id: puppet
    objectName: "xiaodaPuppet"
    width: 400; height: 560
    property string phase: "idle"
    property bool busy: false
    property bool wakeEnabled: false
    property bool sleeping: false
    property bool testMode: false
    property bool presenting: false
    property bool presentationHold: false
    function handIn(item) { return art.mapToItem(item,wristX*art.width/992,wristY*art.height/1585) }
    function holdBoard() {
        lower.stop();stopAccentTimers();nap.stop();sleeping=false;smile=0;presentationHold=true
        moveTo([280,850,150,880,0,6],"board-grip",.75)
    }
    function presenterPose() {
        if(presentationHold)return
        // Product mode uses the same thinking pose after the board is released.
        // presentationHold above keeps opening/closing gestures in control.
        if(thinking)pose("thinking",1,-89.2646,2,-236.847)
        else if(speaking)moveTo([280,850,150,835,.78,-10],"board-explain",.85)
        else moveTo([285,865,185,960,0,0],"board-listen",.85)
    }
    onPresentingChanged: {
        if(presenting){sleeping=false;lower.stop();stopAccentTimers();nap.stop()}
        else {presentationHold=false;lastPhase="";Qt.callLater(reconcile)}
    }
    property int expressionOverride: -1
    property real pointBlend: 0
    property bool fingerWanted: false
    property real legLift: 0
    property real smile: 0
    property real sleepBlend: sleeping ? 1 : 0
    property real wristOsc: 0
    property real blink: 0
    property real mouth: 0
    property real heart: 0
    property int beat: 0
    property string gesture: "rest"
    property real waveBlend: gesture === "greeting" ? 1 : 0
    property string lastPhase: ""
    property bool lastBusy: false
    property bool initialized: false
    readonly property bool canNap: !busy && phase==="idle" && !presenting && !sleeping && !testMode
    onCanNapChanged: if(initialized)armNap()
    function armNap() {
        if(!initialized)return
        nap.stop()
        if(canNap)nap.start()
    }
    readonly property bool speaking: phase === "speaking"
    readonly property bool hearing: phase === "hearing"
    readonly property bool thinking: phase === "thinking"
    readonly property real eyeClosure: expressionOverride === 1 ? 1 : Math.max(sleepBlend,blink)
    readonly property real mouthOpening: expressionOverride === 2 ? 1 : (speaking ? mouth : 0)
    function smooth(t) { t=Math.max(0,Math.min(1,t));return t*t*t*(t*(t*6-15)+10) }

    // Interpolate projected elbow/wrist positions, never sweep the forearm
    // through a >180-degree angle. Carry velocity across state changes.
    property var origin: [302,856,215,951,0,0]
    property var destination: [302,856,215,951,0,0]
    property var initialVelocity: [0,0,0,0,0,0]
    property real motionT: 1
    property real motionSeconds: 1
    property real pathBow: 0
    function sample(i) {
        var t=motionT,t2=t*t,t3=t2*t
        var value=(2*t3-3*t2+1)*origin[i]+(t3-2*t2+t)*initialVelocity[i]*motionSeconds+(-2*t3+3*t2)*destination[i]
        if(i===3)value+=pathBow*Math.pow(Math.sin(Math.PI*t),2)
        return value
    }
    function velocity(i) {
        var t=motionT
        var v=((6*t*t-6*t)*origin[i]+(3*t*t-4*t+1)*initialVelocity[i]*motionSeconds+(-6*t*t+6*t)*destination[i])/motionSeconds
        if(i===3)v+=pathBow*Math.PI*Math.sin(2*Math.PI*t)/motionSeconds
        return v
    }
    readonly property real elbowX: sample(0)
    readonly property real elbowY: sample(1)
    readonly property real wristX: sample(2)
    readonly property real wristY: sample(3)
    readonly property real lift: Math.max(0,Math.min(1.1,sample(4)))
    readonly property real wristBase: sample(5)
    readonly property real shoulder: Math.atan2(elbowY-780,elbowX-380)*180/Math.PI-Math.atan2(76,-78)*180/Math.PI
    readonly property real elbow: Math.atan2(wristY-elbowY,wristX-elbowX)*180/Math.PI-Math.atan2(95,-87)*180/Math.PI-shoulder
    readonly property bool front: lift>=.5
    readonly property real squeeze: front ? .5+.5*smooth((lift-.5)/.3) : 1-.5*smooth((lift-.22)/.28)
    readonly property real wrist: -2*lift+wristBase+wristOsc*smooth((lift-.75)/.2)*waveBlend
    NumberAnimation { id: movement; target: puppet; property: "motionT"; from: 0; to: 1; duration: puppet.motionSeconds*1000; easing.type: Easing.Linear }
    Behavior on waveBlend { NumberAnimation { duration: 300; easing.type: Easing.InOutSine } }
    Behavior on pointBlend { NumberAnimation { duration: 400; easing.type: Easing.InOutSine } }
    Behavior on legLift { NumberAnimation { duration: 1100; easing.type: Easing.InOutSine } }
    Behavior on smile { NumberAnimation { duration: 240; easing.type: Easing.InOutSine } }
    Behavior on sleepBlend { NumberAnimation { duration: 350; easing.type: Easing.InOutSine } }
    Behavior on mouth { NumberAnimation { duration: 65 } }
    function rotatePoint(x,y,cx,cy,degrees) {
        var a=degrees*Math.PI/180,c=Math.cos(a),s=Math.sin(a)
        return [cx+(x-cx)*c-(y-cy)*s,cy+(x-cx)*s+(y-cy)*c]
    }
    function pose(name,amount,shoulderDelta,twist,elbowDelta) {
        var sh=amount*14+shoulderDelta,el=amount*88+(elbowDelta===undefined?0:elbowDelta)
        var e=rotatePoint(302,856,380,780,sh),w=rotatePoint(215,951,302,856,el)
        w=rotatePoint(w[0],w[1],380,780,sh)
        var next=[e[0],e[1],w[0],w[1],amount,twist]
        moveTo(next,name,0)
    }
    function moveTo(next,name,secondsOverride) {
        if(gesture===name && next.every(function(v,i){return Math.abs(v-destination[i])<.001}))return
        var current=[],speed=[]
        for(var i=0;i<6;i++){current.push(sample(i));speed.push(velocity(i))}
        var distance=Math.sqrt(Math.pow(next[2]-current[2],2)+Math.pow(next[3]-current[3],2))
        var seconds=Math.max(.65,Math.min(1.15,distance/225))
        if(name==="greeting")seconds=1.0
        if(secondsOverride>0)seconds=secondsOverride
        for(i=0;i<6;i++){
            var cap=i<4?Math.max(100,Math.abs(next[i]-current[i])*2/seconds):(i===4?1.6:45)
            speed[i]=Math.max(-cap,Math.min(cap,speed[i]))
        }
        movement.stop()
        origin=current;destination=next;initialVelocity=speed;motionSeconds=seconds
        pathBow=name==="thinking" || gesture==="thinking" ? 12 : 4
        gesture=name;motionT=0;movement.start()
        pointDelay.stop()
        fingerWanted=name==="thinking"
        if(fingerWanted)pointDelay.restart()
        if(name!=="greeting")legLift=0
    }
    function stopAccentTimers(){speechAccent.stop();accentEnd.stop();listenSettle.stop();thinkDelay.stop();phaseSettle.stop()}
    function greet(){
        if(presenting)return
        sleeping=false;stopAccentTimers();pose("greeting",1,0,0);legLift=0
        lower.restart();armNap()
    }
    function celebrate(){
        if(presenting){touch();return}
        if(busy)return
        greet();legLift=.85;smile=1;smileEnd.restart();hearts.restart()
    }
    function touch(){
        var waking=sleeping;sleeping=false;hearts.restart()
        // Touch feedback must remain available at the smaller presenter scale.
        // Keep the current arm motion so opening/closing the board stays smooth.
        if(presenting){smile=1;smileEnd.restart();return}
        if(!busy && waking)greet()
        else if(!busy){lower.stop();pose("happy",0,0,0);smile=1;smileEnd.restart()}
        armNap()
    }
    function cancelGesture(){lower.stop();stopAccentTimers();pointDelay.stop();pose("rest",0,0,0)}
    function readyPose(){pose("ready",.08,-1,0)}
    function explainPose(){pose("explaining",.85,-16,5,-36)}
    function applyPhase(){
        if(presenting){presenterPose();return}
        if(phase==="hearing"){
            pose("listening",.28,-4,0);listenSettle.restart()
        }else if(phase==="thinking"){
            // A short wait does not need a complete acting routine.
            thinkDelay.restart()
        }else if(phase==="speaking"){
            explainPose();speechAccent.interval=6200;speechAccent.restart()
        }else if(phase==="listening" && busy){readyPose()}
        else if(phase==="interrupting" && busy){pose("attention",.22,-3,0)}
        else if(!lower.running){pose("rest",0,0,0)}
    }
    function reconcile(){
        if(!initialized)return
        var changed=lastPhase!==phase,sessionChanged=lastBusy!==busy
        lastPhase=phase;lastBusy=busy
        if(!changed && !sessionChanged)return
        if(presenting){stopAccentTimers();presenterPose();return}
        if(phase!=="idle" || busy)sleeping=false
        if(phase!=="idle"){smile=0;smileEnd.stop()}
        if(phase==="error" || phase==="stopping" || phase==="interrupting"){
            lower.stop();stopAccentTimers();pointDelay.stop();applyPhase()
        }else if(sessionChanged && (!busy || !(hearing || thinking || speaking))){greet()}
        else if(changed){
            stopAccentTimers()
            if(hearing || thinking || speaking){lower.stop();applyPhase()}
            else if(!lower.running)phaseSettle.restart()
        }
        if(changed)console.log("CHARACTER_SCENE phase="+phase+" gesture="+gesture+" busy="+busy)
        armNap()
    }
    onBusyChanged: Qt.callLater(reconcile)
    onPhaseChanged: Qt.callLater(reconcile)
    onSpeakingChanged: if(!speaking){mouth=0;beat=0}
    Timer { id: lower; interval: 2600; onTriggered: puppet.applyPhase() }
    Timer { id: phaseSettle; interval: 240; onTriggered: puppet.applyPhase() }
    Timer { id: pointDelay; interval: 180; onTriggered: if(puppet.gesture==="thinking")puppet.pointBlend=1 }
    Timer {
        interval: 40; repeat: true; running: !puppet.fingerWanted && puppet.pointBlend>0
        onTriggered: if(puppet.wristY>810 || puppet.wristX<345 || puppet.motionT>=.98)puppet.pointBlend=0
    }
    Timer { id: thinkDelay; interval: 320; onTriggered: if(puppet.thinking)puppet.pose("thinking",1,-89.2646,2,-236.847) }
    Timer { id: listenSettle; interval: 1600; onTriggered: if(puppet.hearing)puppet.pose("listen-hold",.12,-2,0) }
    Timer {
        id: speechAccent; interval: 6200
        onTriggered: if(puppet.speaking){puppet.pose("emphasis",.95,-8,4,-8);accentEnd.restart()}
    }
    Timer {
        id: accentEnd; interval: 1600
        onTriggered: if(puppet.speaking){puppet.explainPose();speechAccent.interval=8500;speechAccent.restart()}
    }
    Timer { id: nap; objectName: "idleSleepTimer"; interval: 15000; onTriggered: if(puppet.canNap){puppet.sleeping=true;puppet.smile=0;puppet.cancelGesture()} }
    Timer { id: smileEnd; interval: 1600; onTriggered: puppet.smile=0 }
    SequentialAnimation {
        running: puppet.visible && !puppet.sleeping && !puppet.testMode;loops: Animation.Infinite
        NumberAnimation { target: puppet;property: "wristOsc";to: 6;duration: 320;easing.type: Easing.InOutSine }
        NumberAnimation { target: puppet;property: "wristOsc";to: -6;duration: 640;easing.type: Easing.InOutSine }
        NumberAnimation { target: puppet;property: "wristOsc";to: 0;duration: 320;easing.type: Easing.InOutSine }
    }
    Timer { interval: 4100;running: puppet.visible && !puppet.sleeping && !puppet.testMode;repeat: true;onTriggered: blinkAction.restart() }
    SequentialAnimation {
        id: blinkAction
        NumberAnimation { target: puppet;property: "blink";to: 1;duration: 110 }
        PauseAnimation { duration: 65 }
        NumberAnimation { target: puppet;property: "blink";to: 0;duration: 150 }
    }
    Timer {
        interval: 125;running: puppet.speaking && !puppet.testMode;repeat: true;triggeredOnStart: true
        onTriggered: {var rhythm=[.25,.85,.55,1,.15,.6,.4,0];puppet.mouth=rhythm[puppet.beat++%rhythm.length]}
    }
    SequentialAnimation {
        id: hearts
        NumberAnimation { target: puppet;property: "heart";to: 1;duration: 150 }
        NumberAnimation { target: puppet;property: "heart";to: 0;duration: 1200 }
    }
    Image { id: sleepImage; source: "assets/sleep.png"; visible: false }
    Image { id: mouthImage; source: "assets/speaking.png"; visible: false }
    Image { id: happyImage; source: "assets/happy.png"; visible: false }
    Item {
        id: art
        width: height*992/1585; height: parent.height; anchors.centerIn: parent
        Repeater {
            model: [{name:"leg",mode:5},{name:"base",mode:0},{name:"hand",mode:2},{name:"front",mode:3},{name:"arm",mode:1},{name:"collar",mode:0},{name:"point",mode:4}]
            delegate: RigLayer {
                anchors.fill: parent; partName: modelData.name; partMode: modelData.mode
                opacity: partName==="hand" ? (puppet.front?0:1)*(1-puppet.pointBlend) : partName==="front" ? (puppet.front?1:0)*(1-puppet.pointBlend) : partName==="point" ? puppet.pointBlend : 1
                elbowX: puppet.elbowX; elbowY: puppet.elbowY; wristX: puppet.wristX; wristY: puppet.wristY
                shoulder: puppet.shoulder; elbow: puppet.elbow; wrist: puppet.wrist; squeeze: puppet.squeeze
                eye: puppet.eyeClosure; mouth: puppet.mouthOpening
                smile: puppet.sleeping || puppet.speaking ? 0 : puppet.smile
                hip: -9*puppet.legLift; knee: -60*puppet.legLift
                sleepSource: sleepImage; mouthSource: mouthImage; happySource: happyImage
            }
        }
    }
    Text { x: 285; y: 130+(1-puppet.heart)*-35; text: "♥  ♥"; font.pixelSize: 23; color: "#DFA698"; opacity: puppet.heart }
    Text {
        objectName: "sleepMarks"
        x: 315; y: 110; text: "z z z"; font.pixelSize: 24; color: "#6C9A8E"
        visible: puppet.sleeping
        SequentialAnimation on y {
            running: puppet.sleeping && puppet.visible; loops: Animation.Infinite
            NumberAnimation { from: 110; to: 103; duration: 1400; easing.type: Easing.InOutSine }
            NumberAnimation { from: 103; to: 110; duration: 1400; easing.type: Easing.InOutSine }
        }
    }
    // Place feedback beside the face, away from the subtitles and hand path.
    Item {
        x: 325; y: 153; width: 58; height: 55; visible: puppet.hearing
        Repeater {
            model: 3
            Rectangle {
                x: index*14; width: 7; height: 13; radius: 3.5; color: "#599E93"
                anchors.verticalCenter: parent.verticalCenter
                SequentialAnimation on height {
                    running: puppet.hearing && puppet.visible; loops: Animation.Infinite
                    PauseAnimation { duration: index*90 }
                    NumberAnimation { to: 29+index*6; duration: 350; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 13; duration: 350; easing.type: Easing.InOutSine }
                }
            }
        }
    }
    Rectangle {
        x: 316; y: 110; width: 78; height: 48; radius: 22; color: "#FFF9E8"
        border.color: "#E4D3A5"; visible: puppet.thinking
        Rectangle { x: 5; y: 42; width: 10; height: 10; radius: 5; color: "#E4D3A5" }
        Row {
            anchors.centerIn: parent; spacing: 7
            Repeater {
                model: 3
                Rectangle {
                    width: 10; height: 10; radius: 5; color: "#B18A43"
                    SequentialAnimation on opacity {
                        loops: Animation.Infinite; running: puppet.thinking && puppet.visible
                        PauseAnimation { duration: index*120 }
                        NumberAnimation { to: .2; duration: 330 }
                        NumberAnimation { to: 1; duration: 330 }
                    }
                }
            }
        }
    }
    MouseArea {
        objectName: "characterTouch"
        x: puppet.presenting ? 80 : 0; y: 0
        width: parent.width-x; height: parent.height
        onClicked: puppet.touch()
        onDoubleClicked: puppet.celebrate()
        onPressAndHold: if (!puppet.busy && !puppet.presenting) { puppet.sleeping=!puppet.sleeping; puppet.smile=0; puppet.cancelGesture() }
    }
    Component.onCompleted: {
        initialized=true; armNap(); Qt.callLater(reconcile)
        console.log("RIG_READY version=20261025-product-v2 native=true")
    }
}
