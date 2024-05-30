#include "Join.hpp"
#include "Lobby.hpp"
#include "NetFoundation.hpp"
#include <QGridLayout>
#include <QLabel>
#include <QJsonObject>
#include <QJsonDocument>
#include <QHeaderView>
#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QString>
#include <QLineEdit>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>

#include <RMG-Core/m64p/Api.hpp>
#include <RMG-Core/Settings/Settings.hpp>
#include <RMG-Core/Core.hpp>
#include <RMG/UserInterface/Widget/RomBrowser/RomBrowserWidget.hpp>
#include <RMG/UserInterface/Widget/RomBrowser/RomBrowserModelData.hpp>

Join::Join(QWidget *parent, RomBrowserWidget *romBrowser)
    : QDialog(parent), romBrowserWidget(romBrowser)
{

    setWindowTitle("NetPlay Setup");
    setMinimumWidth(1000);
    setMinimumHeight(500);
    QGridLayout *layout = new QGridLayout(this);
    layout->setColumnMinimumWidth(1, 500);

    QRegularExpression rx("[a-zA-Z0-9]+");
    QValidator *validator = new QRegularExpressionValidator(rx, this);

    playerNameEdit = new QLineEdit(this);
    playerNameEdit->setValidator(validator);
    playerNameEdit->setMaxLength(30);
    
    std::string netplayName;
    netplayName = CoreSettingsGetStringValue(SettingsID::Core_Netplay_Name);    
    if (!netplayName.empty()) {
        playerNameEdit->setText(QString::fromStdString(netplayName));
    }
    pNameLabel = new QLabel("Nickname:", this);
    layout->addWidget(pNameLabel, 0, 0);
    layout->addWidget(playerNameEdit, 0, 1);

    pingLabel = new QLabel("(Calculating)", this);
    layout->addWidget(pingLabel, 0, 2);

    serverChooser = new QComboBox(this);
    serverChooser->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    connect(serverChooser, SIGNAL(currentIndexChanged(int)), this, SLOT(serverChanged(int)));

    layout->addWidget(serverChooser, 0, 3);

    refreshButton = new QPushButton(this);
    refreshButton->setText("Refresh");
    layout->addWidget(refreshButton, 0, 4);
    connect(refreshButton, &QPushButton::released, this, &Join::refresh);

    listWidget = new QTableWidget(this);
    listWidget->setSelectionBehavior(QAbstractItemView::SelectRows);
    resetList();

    layout->addWidget(listWidget, 1, 0, 1, 5);

    joinButton = new QPushButton(this);
    connect(joinButton, &QPushButton::released, this, &Join::joinGame);
    joinButton->setText("Join Game");
    layout->addWidget(joinButton, 2, 2, 1, 2);

    setLayout(layout);

    connect(&manager, SIGNAL(finished(QNetworkReply*)),
            SLOT(downloadFinished(QNetworkReply*)));

    connect(this, SIGNAL (finished(int)), this, SLOT (onFinished(int)));

    QNetworkRequest request(QUrl(QStringLiteral("https://gist.githubusercontent.com/EndangeredNayla/509752dc059d5b868e9403fb30218315/raw/c10dafddb99f89854e00c223f0687485a5e9009c/")));
    manager.get(request);

    broadcastSocket.bind(QHostAddress(QHostAddress::AnyIPv4), 0);
    connect(&broadcastSocket, &QUdpSocket::readyRead, this, &Join::processBroadcast);
    QByteArray multirequest;
    multirequest.append(1);
    broadcastSocket.writeDatagram(multirequest, QHostAddress::Broadcast, 27886);

    launched = 0;
}

void Join::processBroadcast()
{
    while (broadcastSocket.hasPendingDatagrams())
    {
        QNetworkDatagram datagram = broadcastSocket.receiveDatagram();
        QByteArray incomingData = datagram.data();
        QJsonDocument json_doc = QJsonDocument::fromJson(incomingData);
        QJsonObject json = json_doc.object();
        QStringList servers = json.keys();
        for (int i = 0; i < servers.size(); ++i)
            serverChooser->addItem(servers.at(i), json.value(servers.at(i)).toString());
    }
}

void Join::onFinished(int)
{
    broadcastSocket.close();
    m64p::Core.DoCommand(M64CMD_ROM_CLOSE, 0, NULL);
    if (!launched && webSocket)
    {
        webSocket->close();
        webSocket->deleteLater();
    }
}

void Join::resetList()
{
    row = 0;
    rooms.clear();
    listWidget->clear();
    listWidget->setColumnCount(2);
    listWidget->setRowCount(row);
    QStringList headers;
    headers.append("Name");
    headers.append("Game");
    //headers.append("Players");
    //headers.append("Game MD5");
    //headers.append("Password Protected");
    //headers.append("Fixed Input Delay");
    listWidget->setHorizontalHeaderLabels(headers);
    listWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
}

void Join::downloadFinished(QNetworkReply *reply)
{
    if (!reply->error())
    {
        QJsonDocument json_doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject json = json_doc.object();
        QStringList servers = json.keys();
        for (int i = 0; i < servers.size(); ++i)
            serverChooser->addItem(servers.at(i), json.value(servers.at(i)).toString());
        serverChooser->addItem(QString("Custom"), QString("Custom"));
    }

    reply->deleteLater();
}

void Join::refresh()
{
    serverChanged(serverChooser->currentIndex());
}

void Join::joinGame()
{
    CoreAddCallbackMessage(CoreDebugMessageType::Info, "joinGame called");
    CoreAddCallbackMessage(CoreDebugMessageType::Info, ("Player name: " + playerNameEdit->text()).toStdString().c_str());

    if (webSocket && webSocket->state() != QAbstractSocket::ConnectedState)
    {
        CoreAddCallbackMessage(CoreDebugMessageType::Error, "Could not connect to server");
        QMessageBox msgBox;
        msgBox.setText("Could not connect to server");
        msgBox.exec();
        return;
    }
    if (listWidget->currentRow() < 0)
    {
        CoreAddCallbackMessage(CoreDebugMessageType::Error, "No game selected to join");
        QMessageBox msgBox;
        msgBox.setText("You haven't selected a game to join");
        msgBox.exec();
        return;
    }
    
    QMessageBox msgBox;
    if (webSocket && webSocket->state() != QAbstractSocket::ConnectedState)
    {
        msgBox.setText("Could not connect to server");
        msgBox.exec();
        return;
    }
    if (listWidget->currentRow() < 0)
    {
        msgBox.setText("You haven't selected a game to join");
        msgBox.exec();
        return;
    }

    QString gameName = listWidget->item(listWidget->currentRow(), 1)->text();
    QString romFilePath = findRomFilePath(gameName);

    CoreAddCallbackMessage(CoreDebugMessageType::Info, ("Game Name: " + gameName).toStdString().c_str());
    CoreAddCallbackMessage(CoreDebugMessageType::Info, ("ROM File Path: " + romFilePath).toStdString().c_str());



    if (!filename.isNull())
    {
        if (loadROM(filename) == M64ERR_SUCCESS)
        {
            int room_port = rooms.at(listWidget->currentRow()).value("port").toInt();
            m64p_rom_settings rom_settings;
            m64p::Core.DoCommand(M64CMD_ROM_GET_SETTINGS, sizeof(rom_settings), &rom_settings);

            joinButton->setEnabled(false);
            QJsonObject json;
            json.insert("type", "request_join_room");
            json.insert("player_name", playerNameEdit->text());
            json.insert("password", "MPN");
            json.insert("client_sha", "demo");
            json.insert("MD5", QString(rom_settings.MD5));
            json.insert("port", room_port);

            QJsonDocument json_doc(json);
            webSocket->sendTextMessage(json_doc.toJson());
        }
        else
        {
            msgBox.setText("Could not open ROM");
            msgBox.exec();
        }
    }
}

void Join::serverChanged(int index)
{
    QString serverName = serverChooser->itemData(index).toString();
    if (webSocket)
    {
        webSocket->close();
        webSocket->deleteLater();
        webSocket = NULL;
    }

    if (serverName == "Custom")
    {
        bool ok;
        customServerAddress = QInputDialog::getText(this, "Custom Netplay Server", "IP Address / Host:", QLineEdit::Normal, "", &ok);

        if (!ok || customServerAddress.isEmpty())
        {
            customServerAddress.clear();
            resetList();
            return;
        }
        else
        {
            customServerAddress.prepend("ws://");
        }
    }
    else
    {
        customServerAddress.clear();
    }

    pingLabel->setText("(Calculating)");

    resetList();
    webSocket = new QWebSocket();
    connect(webSocket, &QWebSocket::connected, this, &Join::onConnected);
    connectionTimer = new QTimer(this);
    connectionTimer->setSingleShot(true);
    connectionTimer->start(5000);
    connect(webSocket, &QWebSocket::disconnected, connectionTimer, &QTimer::stop);
    connect(webSocket, &QObject::destroyed, connectionTimer, &QTimer::stop);

    connect(webSocket, &QWebSocket::textMessageReceived, this, &Join::processTextMessage);

    QTimer *pingTimer = new QTimer(this);
    connect(webSocket, &QWebSocket::pong, this, &Join::updatePing);
    connect(pingTimer, &QTimer::timeout, this, &Join::sendPing);
    connect(webSocket, &QWebSocket::disconnected, pingTimer, &QTimer::stop);
    connect(webSocket, &QObject::destroyed, pingTimer, &QTimer::stop);
    pingTimer->start(2500);
    webSocket->ping();

    QString serverUrlStr = customServerAddress.isEmpty() ? serverChooser->currentData().toString() : customServerAddress;
    QUrl serverUrl = QUrl(serverUrlStr);
    if (!customServerAddress.isEmpty() && serverUrl.port() < 0)
        // Be forgiving of custom server addresses that forget the port
        serverUrl.setPort(27886);

    webSocket->open(serverUrl);
}


void Join::onConnected()
{
    connectionTimer->stop();

    QJsonObject json;
    json.insert("type", "request_get_rooms");
    json.insert("netplay_version", NETPLAY_VER);
    json.insert("emulator", "MPN");
    QJsonDocument json_doc(json);
    webSocket->sendTextMessage(json_doc.toJson());
}

void Join::processTextMessage(QString message)
{
    QJsonDocument json_doc = QJsonDocument::fromJson(message.toUtf8());
    QJsonObject json = json_doc.object();
    QMessageBox msgBox;
    msgBox.setTextFormat(Qt::RichText);
    msgBox.setTextInteractionFlags(Qt::TextBrowserInteraction);

    if (json.value("type").toString() == "reply_get_rooms")
    {
        if (json.value("accept").toInt() == 0)
        {
            json.remove("type");
            rooms << json;

            listWidget->insertRow(row);
            QTableWidgetItem *newItem = new QTableWidgetItem(json.value("room_name").toString());
            newItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
            listWidget->setItem(row, 0, newItem);
            newItem = new QTableWidgetItem(json.value("game_name").toString());
            newItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
            listWidget->setItem(row, 1, newItem);
            //newItem = new QTableWidgetItem(QString::number(json.value("player_count").toInt())); // Display player count
            //ewItem->setFlags(newItem->flags() & ~Qt::ItemIsEditable);
            //listWidget->setItem(row, 2, newItem);
            ++row;
        }
        else
        {
            msgBox.setText(json.value("message").toString());
            msgBox.exec();
        }
    }
    else if (json.value("type").toString() == "reply_join_room")
    {
        if (json.value("accept").toInt() == 0)
        {
            json.remove("type");
            json.remove("accept");
            launched = 1;
            Lobby *waitRoom = new Lobby(filename, json, webSocket, parentWidget());
            waitRoom->show();
            accept();
            return;
        }
        else
        {
            m64p::Core.DoCommand(M64CMD_ROM_CLOSE, 0, NULL);
            msgBox.setText(json.value("message").toString());
            msgBox.exec();
        }
        joinButton->setEnabled(true);
    }
}

void Join::updatePing(quint64 elapsedTime, const QByteArray&)
{
    pingLabel->setText(QString::number(elapsedTime) + " ms");
}

void Join::sendPing()
{
    webSocket->ping();
}

QString Join::findRomFilePath(const QString& gameName)
{
    RomBrowserModelData data;
    if (romBrowserWidget->getCurrentData(data) && QString::fromStdString(data.settings.GoodName).compare(gameName, Qt::CaseInsensitive) == 0)
    {
        CoreAddCallbackMessage(CoreDebugMessageType::Info, ("Match found: " + data.file).toStdString().c_str());
        return data.file;
    }

    CoreAddCallbackMessage(CoreDebugMessageType::Error, "No matching ROM file found");
    return "";
}