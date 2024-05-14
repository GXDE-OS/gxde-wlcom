import QtQuick 2.12
import QtQuick.Window 2.12
import MyComponents 1.0

Rectangle {
    ThumbnailItem {
        id: thumbnailItem
        anchors.centerIn: parent
        anchors.fill: parent
        thumInfo: dataInfo
    }
}
