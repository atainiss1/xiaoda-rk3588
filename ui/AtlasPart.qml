import QtQuick 2.15

// Render a region of the generated layer atlas once. Animation moves the GPU
// texture, rather than repainting a Canvas on every frame.
Item {
    id: part
    property rect region
    property var outline: []
    property url atlas: "assets/xiaoda-parts-v1.png"
    Canvas {
        id: canvas
        anchors.fill: parent
        antialiasing: true
        renderTarget: Canvas.Image
        Component.onCompleted: loadImage(part.atlas)
        onImageLoaded: requestPaint()
        onPaint: {
            if (!isImageLoaded(part.atlas)) return
            var c = getContext("2d")
            c.clearRect(0, 0, width, height)
            c.save()
            if (part.outline.length) {
                c.beginPath()
                c.moveTo(part.outline[0][0] * width / part.region.width,
                         part.outline[0][1] * height / part.region.height)
                for (var i=1; i<part.outline.length; ++i)
                    c.lineTo(part.outline[i][0] * width / part.region.width,
                             part.outline[i][1] * height / part.region.height)
                c.closePath(); c.clip()
            }
            c.drawImage(part.atlas, part.region.x, part.region.y,
                        part.region.width, part.region.height, 0, 0, width, height)
            c.restore()
        }
    }
}
