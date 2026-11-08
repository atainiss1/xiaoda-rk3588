import QtQuick 2.15
import "RigPaths.js" as Rig
Item {
 id: layer
 property string partName: "base"
 property real partMode: 0
 property real elbowX: 302
 property real elbowY: 856
 property real wristX: 215
 property real wristY: 951
 property real shoulder: 0
 property real elbow: 0
 property real wrist: 0
 property real squeeze: 1
 property real eye: 0
 property real mouth: 0
 property real smile: 0
 property real hip: 0
 property real knee: 0
 property var sleepSource
 property var mouthSource
 property var happySource
 property url neutralUrl: Qt.resolvedUrl("assets/neutral.png")
 property url backingUrl: Qt.resolvedUrl("assets/backing.png")
 property url waveUrl: Qt.resolvedUrl("assets/wave.png")
 property url thinkingUrl: Qt.resolvedUrl("assets/thinking.png")
 Canvas {
  id: painted; width: 744; height: 1189; renderTarget: Canvas.Image
  Component.onCompleted: {loadImage(layer.neutralUrl);loadImage(layer.backingUrl);loadImage(layer.waveUrl);loadImage(layer.thinkingUrl)}
  onImageLoaded: requestPaint()
  onPaint: {if(!isImageLoaded(layer.neutralUrl)||!isImageLoaded(layer.backingUrl)||!isImageLoaded(layer.waveUrl)||!isImageLoaded(layer.thinkingUrl))return;var c=getContext('2d');c.reset();c.clearRect(0,0,width,height);c.save();c.scale(width/992,height/1585);Rig.paint(c,layer.partName,layer.neutralUrl,layer.backingUrl,layer.waveUrl,layer.thinkingUrl);c.restore();}
 }
 ShaderEffectSource {id: tex;sourceItem: painted;hideSource: true;visible: false;live: true}
 ShaderEffect {
  anchors.fill: parent
  property variant source: tex
  property variant sleepSource: layer.sleepSource
  property variant mouthSource: layer.mouthSource
  property variant happySource: layer.happySource
  property real smile: layer.smile
  property real eye: layer.eye
  property real mouth: layer.mouth
  property real isBase: layer.partName === "base" ? 1 : 0
  property size uElbowPos: Qt.size(layer.elbowX,layer.elbowY)
  property size uWristPos: Qt.size(layer.wristX,layer.wristY)
  property real uPart: layer.partMode
  property real uShoulder: layer.shoulder*Math.PI/180
  property real uElbow: layer.elbow*Math.PI/180
  property real uWrist: layer.wrist*Math.PI/180
  property real uSqueeze: layer.squeeze
  property real uHip: layer.hip*Math.PI/180
  property real uKnee: layer.knee*Math.PI/180
  property size itemSize: Qt.size(width,height)
  mesh: GridMesh {resolution: Qt.size(80,128)}
  vertexShader: "attribute highp vec4 qt_Vertex; attribute highp vec2 qt_MultiTexCoord0; uniform highp mat4 qt_Matrix; uniform highp vec2 itemSize; varying vec2 vUV;uniform float uPart;uniform float uShoulder;uniform float uElbow;uniform float uWrist;uniform float uSqueeze;uniform float uHip;uniform float uKnee;uniform vec2 uElbowPos;uniform vec2 uWristPos;\nvec2 rot(vec2 p,vec2 c,float a){vec2 d=p-c;return c+vec2(d.x*cos(a)-d.y*sin(a),d.x*sin(a)+d.y*cos(a));}\nvec2 bone(vec2 p,vec2 a,vec2 b,vec2 A,vec2 B){\n vec2 d=b-a;float len=length(d);vec2 axis=d/len;\n vec2 newAxis=normalize(B-A);\n return A+(B-A)*(dot(p-a,axis)/len)+vec2(-newAxis.y,newAxis.x)*dot(p-a,vec2(-axis.y,axis.x));\n}\nvoid main(){vUV=qt_MultiTexCoord0;vec2 p=qt_Vertex.xy/itemSize*vec2(992.,1585.);\nif(uPart>4.5){\n float bend=smoothstep(1205.,1290.,p.y);\n p=mix(p,rot(p,vec2(614.,1260.),uKnee),bend);\n p=rot(p,vec2(597.,1122.),uHip);\n}else if(uPart>1.5){\n vec2 d;vec2 axis;\n if(uPart>3.5){d=(p-vec2(398.,770.))*.82;axis=normalize(vec2(.503,-.864));}\n else if(uPart>2.5){d=(p-vec2(186.,655.))*.82;axis=normalize(vec2(-.26,-.965));}\n else{d=p-vec2(215.,951.);axis=normalize(vec2(-.96,.28));}\n vec2 normal=vec2(-axis.y,axis.x);d=axis*dot(d,axis)+normal*dot(d,normal)*uSqueeze;\n if(uPart>3.5)d=rot(d,vec2(0.),-2.371702);\n else if(uPart>2.5)d=rot(d,vec2(0.),-1.780236);\n float fore=atan(uWristPos.y-uElbowPos.y,uWristPos.x-uElbowPos.x)-atan(95.,-87.);\n p=uWristPos+rot(d,vec2(0.),uWrist+fore);\n}else if(uPart>.5){vec2 source=p;\nfloat elbow=smoothstep(-30.,45.,dot(source-vec2(302.,856.),vec2(-.586,.811)));vec2 upper=bone(p,vec2(380.,780.),vec2(302.,856.),vec2(380.,780.),uElbowPos);\n vec2 lower=bone(p,vec2(302.,856.),vec2(215.,951.),uElbowPos,uWristPos);\n p=mix(upper,lower,elbow);}\nvec2 uv=p/vec2(992.,1585.);gl_Position=qt_Matrix*vec4((vec2(.5)+(uv-vec2(.5))*.94)*itemSize,0.,1.);}"
  fragmentShader: "precision mediump float;varying highp vec2 vUV;uniform sampler2D source;uniform sampler2D sleepSource;uniform sampler2D mouthSource;uniform sampler2D happySource;uniform float smile;uniform lowp float qt_Opacity;uniform float eye;uniform float mouth;uniform float isBase;\nvoid main(){vec4 c=texture2D(source,vUV);if(isBase>.5){vec2 p=vUV*vec2(992.,1585.);float eyes=max(1.-smoothstep(.9,1.,length((p-vec2(363.,540.))/vec2(90.,65.))),1.-smoothstep(.9,1.,length((p-vec2(613.,515.))/vec2(91.,62.))));float m=1.-smoothstep(.9,1.,length((p-vec2(499.,619.))/vec2(40.,30.)));c=mix(c,texture2D(sleepSource,vUV),eye*eyes);c=mix(c,texture2D(mouthSource,vUV),mouth*m);\n vec2 happyEyeUV=(p+(p.x<495.?vec2(7.,4.):vec2(11.,8.)))/vec2(1008.,1561.);\n c=mix(c,texture2D(happySource,happyEyeUV),smile*eyes);\n float grin=1.-smoothstep(.87,1.,length((p-vec2(499.,620.))/vec2(45.,42.)));\n vec2 happyMouthUV=((p-vec2(499.,620.))*1.05+vec2(501.,618.))/vec2(1008.,1561.);\n c=mix(c,texture2D(happySource,happyMouthUV),smile*grin);}gl_FragColor=c*qt_Opacity;}"
  onStatusChanged: if(status===ShaderEffect.Error)console.error('RIG_SHADER',layer.partName,log)
 }
}
