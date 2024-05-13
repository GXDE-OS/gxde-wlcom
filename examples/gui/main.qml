import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.5
import MyComponents 1.0

//Window {
//    visible: true
//    ThumbnailItem {
//        id: thumbnailItem
//        anchors.centerIn: parent
//        anchors.fill: parent
//        thumInfo: dataInfo
//    }
//}

Rectangle {
    ThumbnailItem {
        id: thumbnailItem
        anchors.centerIn: parent
        anchors.fill: parent
        thumInfo: dataInfo
    }
}
