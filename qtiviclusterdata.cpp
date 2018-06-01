/****************************************************************************
**
** Copyright (C) 2015 Pelagicore AG
** Contact: http://www.qt.io/ or http://www.pelagicore.com/
**
** This file is part of the QtIVI module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3-PELAGICORE$
** Commercial License Usage
** Licensees holding valid commercial Qt IVI licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Pelagicore. For licensing terms
** and conditions, contact us at http://www.pelagicore.com.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: LGPL-3.0
**
****************************************************************************/

#include "qtiviclusterdata.h"
#include <QtDebug>
#include <QTimer>
#include <QCanBusFrame>
#include <QCanBus>
#include <QTextCodec>
#include <typeinfo>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>

QtIVIClusterData::QtIVIClusterData(QObject* parent)
    : QObject(parent),
      m_vehicleSpeed(0),
      m_latitude(43.2355),
      m_longitude(45.2245),
      m_direction(0),
      m_flatTire(false),
      m_collision(false),
      m_doorOpen(false),
      m_lightFailure(false),
      m_seatBelt(false),
      m_reverse(false),
      m_leftTurnLight(false),
      m_rightTurnLight(false),
      m_headLight(false),
      m_parkLight(false),
      m_carId(1),
      m_brake(false),
      m_engineTemp(0),
      m_oilTemp(0.0),
      m_oilPressure(0),
      m_batteryPotential(0),
      m_gasLevel(0),
      m_rpm(0),
      m_gear(0),
      m_canDevice(nullptr)
{
    connectToServiceObject();

//    Allow for direct connection to the CAN Bus
//
//    m_canDevice = QCanBus::instance()->createDevice("socketcan", "vcan0");
//    if (!m_canDevice) {
//        qDebug()<< "couldn't connect can device";
//        return;
//    }
//    qDebug()<< m_canDevice;
//    qDebug()<< this;
//    connect(m_canDevice, &QCanBusDevice::framesReceived, this, &QtIVIClusterData::checkMessages);
//    qDebug()<< "after connect";
//    if (!m_canDevice->connectDevice()) {
//        qDebug()<< "not connect";
//        delete m_canDevice;
//        m_canDevice = nullptr;
//    } else {
//        int bitr = 10000;
//        m_canDevice->setConfigurationParameter(QCanBusDevice::BitRateKey, QVariant(bitr));

//        QVariant bitRate = m_canDevice->configurationParameter(QCanBusDevice::BitRateKey);
//        qDebug()<< bitRate.toInt();
//        if (bitRate.isValid()) {
//            qDebug()<<"Backend: connected";
//        } else {
//            qDebug()<<"Backend:connected 2";
//        }
//    }


    // Can bus socket
    // Check if certificate exists
    QLatin1String rootCApath = QLatin1String("pubkey.pem");
    QFileInfo check_file(rootCApath);

    // Load certificate
    QList<QSslCertificate> cert = QSslCertificate::fromPath(rootCApath, QSsl::Pem, QRegExp::Wildcard);

    // Create an self-signed certificate error
    // and add it to the SSL ignore list
    // For a production system this won't be an issue
    QSslError error(QSslError::SelfSignedCertificate, cert.at(0));
    QList<QSslError> expectedSslErrors;
    expectedSslErrors.append(error);
    m_webSocket.ignoreSslErrors();

    // Attach QSslSocket::encrypted signal
    // to the onSslConnected method
    connect(&m_webSocket, &QSslSocket::encrypted, this, &QtIVIClusterData::onSslConnected);

    // Do the same for when there is an error
    // An call the onSslError function
    typedef void (QSslSocket:: *sslErrorsSignal)(const QList<QSslError> &);
    connect(&m_webSocket, static_cast<sslErrorsSignal>(&QSslSocket::sslErrors),
            this, &QtIVIClusterData::onSslErrors);

    // Connect to the CAN Bus with encrypton
    m_webSocket.connectToHostEncrypted("169.254.22.105", 8082);

    // Create the Fog node signal-slot connection
    connect(&m_tcpSocket, &QTcpSocket::connected, this, &QtIVIClusterData::onTcpConnected);
    // Connect to the Fog Node
    m_tcpSocket.connectToHost("10.42.0.1", 1234);

}

// Method is Called when connected to the CAN Bus
void QtIVIClusterData::onSslConnected()
{
    qDebug() << "socket connected";
    // When receiving data from the socket, call checkMessages
    connect(&m_webSocket, &QSslSocket::readyRead,
            this, &QtIVIClusterData::checkMessages);
}


// Method is called if TCP( Fog Node ) is connected
// Immediately send location and speed of vehicle
// to fog node
void QtIVIClusterData::onTcpConnected()
{
    qDebug() << "socket TCP connected";
    connect(&m_tcpSocket, &QTcpSocket::readyRead,
            this, &QtIVIClusterData::checkCollision);
    QJsonObject object;
    QString location = QString::number(m_latitude)+","+QString::number(m_longitude);
    QString speed = QString::number(m_vehicleSpeed);

    object.insert("location", location);
    object.insert("speed", speed);
    QJsonDocument doc = QJsonDocument(object);
    m_tcpSocket.write(doc.toJson());
}

void QtIVIClusterData::onSslErrors(const QList<QSslError> &errors)
{
    qDebug()<< errors;
    m_webSocket.ignoreSslErrors();

    // WARNING: Never ignore SSL errors in production code.
    // The proper way to handle self-signed certificates is to add a custom root
    // to the CA store.

    m_webSocket.ignoreSslErrors();
}


static QByteArray dataToHex(const QByteArray &data)
{
    QByteArray result = data.toHex().toUpper();

    for (int i = 0; i < result.size(); i += 3)
        result.insert(i, ' ');

    return result;
}

// Method to check whether there is a collision
void QtIVIClusterData::checkCollision()
{
    QString message = m_tcpSocket.readLine();

    QJsonDocument jsonResp = QJsonDocument::fromJson(message.toUtf8());
    QJsonObject jsonObject = jsonResp.object();

    if(jsonObject.value("collision").toString() != ""){
        onCollisionChanged(true);
    }
}

// Check the ID's of the CAN messages and
// appropriately react 
void QtIVIClusterData::checkMessages()
{
    QString message = m_webSocket.readLine();

    QJsonDocument jsonResp = QJsonDocument::fromJson(message.toUtf8());
    QJsonObject jsonObject = jsonResp.object();

    int arbitration_id = jsonObject.value("id").toInt();
    QJsonArray payload = jsonObject.value("data").toArray();

    bool ok;
    if (arbitration_id == 580) {
        // set speed
        int speed = payload.at(3).toString().toUInt(&ok, 10);
        onVehicleSpeedChanged(speed);
        int nRpm = speed * 150;
        onRpmChanged(nRpm);

    } else if (arbitration_id == 501) {
	// gear
        int gear = payload.at(3).toString().toUInt(&ok, 10);
        onGearChanged(gear);

    } else if (arbitration_id == 503) {
        // parking brake
        qDebug()<< "park brake";
        bool park_light = (bool)payload.at(5).toString().toUInt(&ok, 10);
        onParkLightChanged(park_light);

    } else if (arbitration_id == 504) {
        // lights
        qDebug()<< "lights";
        bool lights = (bool)payload.at(5).toString().toUInt(&ok, 10);
        onHeadLightChanged(lights);

    } else if (arbitration_id == 392) {
        // turn signals
        qDebug()<< "turn signals";

        int turn_signal = payload.at(5).toString().toUInt(&ok, 10);

        if( 1 == turn_signal ) {
            onLeftTurnLightChanged(true);
        } else if ( 2 == turn_signal ) {
            onRightTurnLightChanged(true);
        } else {
            onLeftTurnLightChanged(false);
            onRightTurnLightChanged(false);
        }

    } else if (arbitration_id == 507) {
        // seat belt
        qDebug()<< "seat belt";
        bool seatbelt = (bool)payload.at(5).toString().toUInt(&ok, 10);
        onSeatBeltChanged(seatbelt);

    } else if (arbitration_id == 508) {
        // fuel gauge
        qDebug()<< "gas";
        int gaslvl = payload.at(5).toString().toUInt(&ok, 10);
        onGasLevelChanged(gaslvl);

    } else if (arbitration_id == 509) {
        //temperature
        qDebug()<< "temp";
        int temp = payload.at(5).toString().toUInt(&ok, 10);
        onEngineTempChanged(temp);

    } else if (arbitration_id == 510) {
        //battery
        qDebug()<< "battery";
        int battery = payload.at(5).toString().toUInt(&ok, 10);
        onBatteryPotentialChanged(battery);

    } else if (arbitration_id == 500) {
        //oil
        qDebug()<< "oil";
        int oil = payload.at(5).toString().toUInt(&ok, 10);
        onOilTempChanged(oil);

    } else if (arbitration_id == 500) {
        // set rpm
    } else if (arbitration_id == 500) {
        // ABS warn
    } else if (arbitration_id == 500) {
        // tire warn
    } else if (arbitration_id == 500) {
        // door open
    } else if (arbitration_id == 511) {
        // engine on/off
        qDebug()<< "on/off";
    }
}


void QtIVIClusterData::connectToServiceObject()
{
    initializeZones();
}

ZonedProperties *QtIVIClusterData::zoneAt(const QString &zone) const
{
    foreach (ZonedProperties *f, m_zoneFeatures)
        if (f->zone() == zone)
            return f;
    return 0;
}

void QtIVIClusterData::initializeZones()
{

}


QVariantMap QtIVIClusterData::zoneFeatureMap() const
{
    return m_zoneFeatureMap;

}

double QtIVIClusterData::vehicleSpeed() const
{
    return m_vehicleSpeed;
}

double QtIVIClusterData::latitude() const
{
    return m_latitude;
}

double QtIVIClusterData::longitude() const
{
    return m_longitude;
}

double QtIVIClusterData::direction() const
{
    return m_direction;
}

bool QtIVIClusterData::flatTire() const
{
    return m_flatTire;
}

bool QtIVIClusterData::collision() const
{
    return m_collision;
}

bool QtIVIClusterData::doorOpen() const
{
    return m_doorOpen;
}

bool QtIVIClusterData::lightFailure() const
{
    return m_lightFailure;
}

bool QtIVIClusterData::seatBelt() const
{
    return m_seatBelt;
}

bool QtIVIClusterData::reverse() const
{
    return m_reverse;
}

bool QtIVIClusterData::leftTurnLight() const
{
    return m_leftTurnLight;
}

bool QtIVIClusterData::rightTurnLight() const
{
    return m_rightTurnLight;
}

bool QtIVIClusterData::headLight() const
{
    return m_headLight;
}

bool QtIVIClusterData::parkLight() const
{
    return m_parkLight;
}

int QtIVIClusterData::carId() const
{
    return m_carId;
}

bool QtIVIClusterData::brake() const
{
    return  m_brake;
}

int QtIVIClusterData::engineTemp() const
{
    return m_engineTemp;
}

double QtIVIClusterData::oilTemp() const
{
    return m_oilTemp;
}

int QtIVIClusterData::oilPressure() const
{
    return m_oilPressure;
}

double QtIVIClusterData::batteryPotential() const
{
    return  m_batteryPotential;
}

double QtIVIClusterData::gasLevel() const
{
    return m_gasLevel;
}

int QtIVIClusterData::rpm() const
{
    return m_rpm;
}

int QtIVIClusterData::gear() const
{
    return  m_gear;
}

void QtIVIClusterData::classBegin()
{

}

void QtIVIClusterData::componentComplete()
{

}

void QtIVIClusterData::onVehicleSpeedChanged(int vehicleSpeed)
{
    m_vehicleSpeed = vehicleSpeed;
    emit vehicleSpeedChanged(m_vehicleSpeed);
}

void QtIVIClusterData::onLatitudeChanged(double latitude)
{
    m_latitude = latitude;
    emit latitudeChanged(latitude);
}

void QtIVIClusterData::onLongitudeChanged(double longitude)
{
    m_longitude = longitude;
    emit longitudeChanged(longitude);
}

void QtIVIClusterData::onDirectionChanged(double direction, const QString &zone)
{
    Q_UNUSED(zone);
    m_direction = direction;
    emit directionChanged(direction);
}

void QtIVIClusterData::onFlatTireChanged(bool flatTire, const QString &zone)
{
    Q_UNUSED(zone);
    m_flatTire = flatTire;
    emit flatTireChanged(flatTire);
}

void QtIVIClusterData::onCollisionChanged(bool collision)
{
    m_collision = collision;
    emit collisionChanged(collision);
}

void QtIVIClusterData::onDoorOpenChanged(bool doorOpen, const QString &zone)
{
    ZonedProperties *z = zoneAt(zone);
    if (z) {
        z->setDoorOpen(doorOpen);
    }
}

void QtIVIClusterData::onLightFailureChanged(bool lightFailure, const QString &zone)
{
    Q_UNUSED(zone);
    m_lightFailure = lightFailure;
    emit lightFailureChanged(lightFailure);
}

void QtIVIClusterData::onSeatBeltChanged(bool seatBelt)
{
    m_seatBelt = seatBelt;
    emit seatBeltChanged(seatBelt);
}

void QtIVIClusterData::onReverseChanged(bool reverse, const QString &zone)
{
    Q_UNUSED(zone);
    m_reverse = reverse;
    emit reverseChanged(reverse);
}

void QtIVIClusterData::onLeftTurnLightChanged(bool leftTurnLight)
{
    m_leftTurnLight = leftTurnLight;
    emit leftTurnLightChanged(leftTurnLight);
}

void QtIVIClusterData::onRightTurnLightChanged(bool rightTurnLight)
{
    m_rightTurnLight = rightTurnLight;
    emit rightTurnLightChanged(rightTurnLight);
}

void QtIVIClusterData::onHeadLightChanged(bool headLight)
{
    m_headLight = headLight;
    emit headLightChanged(headLight);
}

void QtIVIClusterData::onParkLightChanged(bool parkLight)
{
    m_parkLight = parkLight;
    emit parkLightChanged(parkLight);
}

void QtIVIClusterData::onCarIdChanged(int carId, const QString &zone)
{
    Q_UNUSED(zone);
    m_carId = carId;
    emit carIdChanged(carId);
}

void QtIVIClusterData::onBrakeChanged(bool brakeOn, const QString &zone)
{
    Q_UNUSED(zone);
    m_brake = brakeOn;
    emit brakeChanged(brakeOn);
}

void QtIVIClusterData::onEngineTempChanged(int engineTemp)
{
    m_engineTemp = engineTemp;
    emit engineTempChanged(engineTemp);
}

void QtIVIClusterData::onOilTempChanged(double oilTemp)
{
    m_oilTemp = oilTemp;
    emit oilTempChanged(oilTemp);
}

void QtIVIClusterData::onOilPressureChanged(int oilPressure, const QString &zone)
{
    Q_UNUSED(zone);
    m_oilPressure = oilPressure;
    emit oilPressureChanged(oilPressure);
}

void QtIVIClusterData::onBatteryPotentialChanged(double batteryPotential)
{
    m_batteryPotential = batteryPotential;
    emit batteryPotentialChanged(batteryPotential);
}

void QtIVIClusterData::onGasLevelChanged(double gasLevel)
{
    m_gasLevel = gasLevel;
    emit gasLevelChanged(gasLevel);
}

void QtIVIClusterData::onRpmChanged(int rpm)
{
    m_rpm = rpm;
    emit rpmChanged(rpm);
}

void QtIVIClusterData::onGearChanged(int gear)
{
    m_gear = gear;
    emit gearChanged(gear);
}
