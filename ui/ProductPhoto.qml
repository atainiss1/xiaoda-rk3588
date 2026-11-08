import QtQuick 2.15
Item {
    id: photo
    property var product: ({})
    clip: true
    Image {
        anchors.centerIn: parent
        width: parent.width*(photo.product.imageZoom || 1)
        height: parent.height*(photo.product.imageZoom || 1)
        source: photo.product.image ? "assets/products/"+photo.product.image : ""
        fillMode: Image.PreserveAspectFit
        sourceSize.width: 850
        asynchronous: true
    }
}
