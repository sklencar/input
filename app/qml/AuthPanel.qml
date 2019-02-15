import QtQuick 2.7
import QtQuick.Controls 2.0
import QtQuick.Layouts 1.3
import QtGraphicalEffects 1.0
import QgsQuick 0.1 as QgsQuick
import "."  // import InputStyle singleton

Item {

    signal authFailed()

    property alias loginName: loginName
    property alias password: password
    property string errorText: errorText

    property real fieldHeight: InputStyle.rowHeight
    property real panelMargin: fieldHeight/4
    property color fontColor: "white"

    function close() {
        visible = false
        password.text = ""
        loginName.text = ""
        if (!__merginApi.hasAuthData()) {
            authFailed()
        }
    }

    Keys.onReleased: {
        if (event.key === Qt.Key_Back || event.key === Qt.Key_Escape) {
            event.accepted = true;
            root.close()
        }
    }

    id: root
    focus: true
    Pane {
        id: pane

        width: parent.width
        height: parent.height
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter


        background: Rectangle {
            color: InputStyle.fontColor
        }

        Item {
            id: loginForm
            anchors.fill: parent
            anchors.bottomMargin: Qt.inputMethod.keyboardRectangle.height ? Qt.inputMethod.keyboardRectangle.height: 0

            Column {
                id: columnLayout
                spacing: root.panelMargin
                anchors.verticalCenter: parent.verticalCenter

                Image {
                    source: "mergin.svg"
                    width: loginForm.width/2
                    sourceSize.width: width
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Row {
                    id: row
                    width: loginForm.width
                    height: fieldHeight
                    spacing: 0

                    Rectangle {
                        id: iconContainer
                        height: fieldHeight
                        width: fieldHeight
                        color: InputStyle.fontColor

                        Image {
                            anchors.margins: root.panelMargin
                            id: icon
                            height: fieldHeight
                            width: fieldHeight
                            anchors.fill: parent
                            source: 'account.svg'
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit
                        }

                        ColorOverlay {
                            anchors.fill: icon
                            source: icon
                            color: root.fontColor
                        }
                    }

                    TextField {
                        id: loginName
                        x: iconContainer.width
                        width: parent.width - iconContainer.width
                        height: fieldHeight
                        font.pixelSize: InputStyle.fontPixelSizeNormal
                        color: root.fontColor
                        placeholderText: qsTr("Username")
                        font.capitalization: Font.MixedCase
                        background: Rectangle {
                            color: InputStyle.fontColor
                        }
                    }
                }

                Rectangle {
                    id: loginNameBorder
                    color: root.fontColor
                    y: loginName.height - height
                    height: 2 * QgsQuick.Utils.dp
                    opacity: loginName.focus ? 1 : 0.6
                    width: parent.width - fieldHeight/2
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Row {
                    width: loginForm.width
                    height: fieldHeight
                    spacing: 0

                    Rectangle {
                        id: iconContainer2
                        height: fieldHeight
                        width: fieldHeight
                        color: InputStyle.fontColor

                        Image {
                            anchors.margins: (fieldHeight/4)
                            id: icon2
                            height: fieldHeight
                            width: fieldHeight
                            anchors.fill: parent
                            source: 'lock.svg'
                            sourceSize.width: width
                            sourceSize.height: height
                            fillMode: Image.PreserveAspectFit

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    if (password.echoMode === TextInput.Normal) {
                                        password.echoMode = TextInput.Password
                                    } else {
                                        password.echoMode = TextInput.Normal
                                    }
                                }
                            }
                        }

                        ColorOverlay {
                            anchors.fill: icon2
                            source: icon2
                            color: root.fontColor
                        }
                    }

                    TextField {
                        id: password
                        width: parent.width - iconContainer.width
                        height: fieldHeight
                        font.pixelSize: InputStyle.fontPixelSizeNormal
                        color: root.fontColor
                        placeholderText: qsTr("Password")
                        echoMode: TextInput.Password
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                        font.capitalization: Font.MixedCase

                        background: Rectangle {
                            color: InputStyle.fontColor
                        }
                    }
                }

                Rectangle {
                    id: passBorder
                    color: InputStyle.panelBackgroundLight
                    height: 2 * QgsQuick.Utils.dp
                    y: password.height - height
                    opacity: password.focus ? 1 : 0.6
                    width: loginForm.width - fieldHeight/2
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Button {
                    id: loginButton
                    width: loginForm.width - 2* root.panelMargin
                    height: fieldHeight
                    text: qsTr("Sign in")
                    font.pixelSize: loginButton.height/2
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        __merginApi.authorize(loginName.text, password.text)
                    }
                    background: Rectangle {
                        color: InputStyle.panelBackgroundLight
                    }

                    contentItem: Text {
                        text: loginButton.text
                        font: loginButton.font
                        opacity: enabled ? 1.0 : 0.3
                        color: InputStyle.fontColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                Button {
                    id: singInButton
                    width: loginForm.width - 2* root.panelMargin
                    height: fieldHeight * 0.7
                    text: qsTr("Sign up")
                    font.pixelSize: singInButton.height/2
                    anchors.horizontalCenter: parent.horizontalCenter
                    onClicked: {
                        // TODO changed for merginApi.apiRoot
                        Qt.openUrlExternally("https://public.cloudmergin.com/auth/signup");
                    }
                    background: Rectangle {
                        color: InputStyle.fontColor
                    }

                    contentItem: Text {
                        text: singInButton.text
                        font: singInButton.font
                        color: InputStyle.highlightColor
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                }
            }
        }

    }
}
