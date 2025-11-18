// SPDX-License-Identifier: GPL-3.0-or-later

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
*
* Copyright (C) 2013 - 2024, nymea GmbH
* Copyright (C) 2024 - 2025, chargebyte austria GmbH
*
* This file is part of nymea-zwave-plugin-openzwave.
*
* nymea-zwave-plugin-openzwave is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* nymea-zwave-plugin-openzwave is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with nymea-zwave-plugin-openzwave. If not, see <https://www.gnu.org/licenses/>.
*
* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

#ifndef OPENZWAVEBACKEND_H
#define OPENZWAVEBACKEND_H

#include <hardware/zwave/zwavebackend.h>
#include <hardware/zwave/zwavevalue.h>

#include <Manager.h>

#include <QObject>
#include <QHash>

class OpenZWaveBackend : public ZWaveBackend
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "io.nymea.ZWaveBackend")
    Q_INTERFACES(ZWaveBackend)

public:
    // Copied from OpenZWave to allow using QMetaEnum on them
    enum NotificationCode
    {
        NotificationCodeMsgComplete = 0, /**< Completed messages */
        NotificationCodeTimeout, /**< Messages that timeout will send a Notification with this code. */
        NotificationCodeNoOperation, /**< Report on NoOperation message sent completion  */
        NotificationCodeAwake, /**< Report when a sleeping node wakes up */
        NotificationCodeSleep, /**< Report when a node goes to sleep */
        NotificationCodeDead, /**< Report when a node is presumed dead */
        NotificationCodeAlive /**< Report when a node is revived */
    };
    Q_ENUM(NotificationCode);

    enum ControllerCommand
    {
        ControllerCommandNone = 0, /**< No command. */
        ControllerCommandAddDevice, /**< Add a new device or controller to the Z-Wave network. */
        ControllerCommandCreateNewPrimary, /**< Add a new controller to the Z-Wave network. Used when old primary fails. Requires SUC. */
        ControllerCommandReceiveConfiguration, /**< Receive Z-Wave network configuration information from another controller. */
        ControllerCommandRemoveDevice, /**< Remove a device or controller from the Z-Wave network. */
        ControllerCommandRemoveFailedNode, /**< Move a node to the controller's failed nodes list. This command will only work if the node cannot respond. */
        ControllerCommandHasNodeFailed, /**< Check whether a node is in the controller's failed nodes list. */
        ControllerCommandReplaceFailedNode, /**< Replace a non-responding node with another. The node must be in the controller's list of failed nodes for this command to succeed. */
        ControllerCommandTransferPrimaryRole, /**< Make a different controller the primary. */
        ControllerCommandRequestNetworkUpdate, /**< Request network information from the SUC/SIS. */
        ControllerCommandRequestNodeNeighborUpdate, /**< Get a node to rebuild its neighbour list.  This method also does RequestNodeNeighbors */
        ControllerCommandAssignReturnRoute, /**< Assign a network return routes to a device. */
        ControllerCommandDeleteAllReturnRoutes, /**< Delete all return routes from a device. */
        ControllerCommandSendNodeInformation, /**< Send a node information frame */
        ControllerCommandReplicationSend, /**< Send information from primary to secondary */
        ControllerCommandCreateButton, /**< Create an id that tracks handheld button presses */
        ControllerCommandDeleteButton /**< Delete id that tracks handheld button presses */
    };
    Q_ENUM(ControllerCommand)

    enum ControllerState
    {
        ControllerStateNormal = 0, /**< No command in progress. */
        ControllerStateStarting, /**< The command is starting. */
        ControllerStateCancel, /**< The command was canceled. */
        ControllerStateError, /**< Command invocation had error(s) and was aborted */
        ControllerStateWaiting, /**< Controller is waiting for a user action. */
        ControllerStateSleeping, /**< Controller command is on a sleep queue wait for device. */
        ControllerStateInProgress, /**< The controller is communicating with the other device to carry out the command. */
        ControllerStateCompleted, /**< The command has completed successfully. */
        ControllerStateFailed, /**< The command has failed. */
        ControllerStateNodeOK, /**< Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node is OK. */
        ControllerStateNodeFailed /**< Used only with ControllerCommand_HasNodeFailed to indicate that the controller thinks the node has failed. */
    };
    Q_ENUM(ControllerState)

    enum UserAlertNotification
    {
        AlertNone, /**< No Alert Currently Present */
        AlertConfigOutOfDate, /**< One of the Config Files is out of date. Use GetNodeId to determine which node is effected. */
        AlertMFSOutOfDate, /**< the manufacturer_specific.xml file is out of date. */
        AlertConfigFileDownloadFailed, /**< A Config File failed to download */
        AlertDNSError, /**< A error occurred performing a DNS Lookup */
        AlertNodeReloadRequired, /**< A new Config file has been discovered for this node, and its pending a Reload to Take affect */
        AlertUnsupportedController, /**< The Controller is not running a Firmware Library we support */
        AlertApplicationStatus_Retry, /**< Application Status CC returned a Retry Later Message */
        AlertApplicationStatus_Queued, /**< Command Has been Queued for later execution */
        AlertApplicationStatus_Rejected, /**< Command has been rejected */
    };
    Q_ENUM(UserAlertNotification)

    explicit OpenZWaveBackend(QObject *parent = nullptr);
    ~OpenZWaveBackend();


    bool startNetwork(const QUuid &networkUuid, const QString &serialPort, const QString &networkKey = QString()) override;
    bool stopNetwork(const QUuid &networkUuid) override;

    quint32 homeId(const QUuid &networkUuid) override;
    quint8 controllerNodeId(const QUuid &networkUuid) override;
    bool isPrimaryController(const QUuid &networkUuid) override;
    bool isStaticUpdateController(const QUuid &networkUuid) override;
    bool isBridgeController(const QUuid &networkUuid) override;

    bool factoryResetNetwork(const QUuid &networkUuid) override;

    ZWaveReply* addNode(const QUuid &networkUuid, bool useSecurity) override;
    ZWaveReply* removeNode(const QUuid &networkUuid) override;
    ZWaveReply* removeFailedNode(const QUuid &networkUuid, quint8 nodeId) override;
    ZWaveReply* cancelPendingOperation(const QUuid &networkUuid) override;

    bool isNodeAwake(const QUuid &networkUuid, quint8 nodeId) override;
    bool isNodeFailed(const QUuid &networkUuid, quint8 nodeId) override;

    QString nodeName(const QUuid &networkUuid, quint8 nodeId) override;
    ZWaveNode::ZWaveNodeType nodeType(const QUuid &networkUuid, quint8 nodeId) override;
    ZWaveNode::ZWaveDeviceType nodeDeviceType(const QUuid &networkUuid, quint8 nodeId) override;
    ZWaveNode::ZWaveNodeRole nodeRole(const QUuid &networkUuid, quint8 nodeId) override;
    quint8 nodeSecurityMode(const QUuid &networkUuid, quint8 nodeId) override;
    quint16 nodeManufacturerId(const QUuid &networkUuid, quint8 nodeId) override;
    QString nodeManufacturerName(const QUuid &networkUuid, quint8 nodeId) override;
    quint16 nodeProductId(const QUuid &networkUuid, quint8 nodeId) override;
    QString nodeProductName(const QUuid &networkUuid, quint8 nodeId) override;
    quint16 nodeProductType(const QUuid &networkUuid, quint8 nodeId) override;
    quint8 nodeVersion(const QUuid &networkUuid, quint8 nodeId) override;

    bool nodeIsZWavePlus(const QUuid &networkUuid, quint8 nodeId) override;
    ZWaveNode::ZWavePlusDeviceType nodePlusDeviceType(const QUuid &networkUuid, quint8 nodeId) override;

    bool nodeIsSecureDevice(const QUuid &networkUuid, quint8 nodeId) override;
    bool nodeIsBeamingDevice(const QUuid &networkUuid, quint8 nodeId) override;

    bool setValue(const QUuid &networkUuid, quint8 nodeId, const ZWaveValue &value) override;

signals:

private slots:
    void onDriverReady(quint32 homeId);
#if OZW_16
    void onDriverFailed(const QString &serialPort);
#else
    void onDriverFailed();
#endif
    void onDriverRemoved(quint32 homeId);
    void onNewNode(quint32 homeId, quint8 nodeId);
    void onNodeAdded(quint32 homeId, quint8 nodeId);
    void onNodeNaming(quint32 homeId, quint8 nodeId);
    void onNodeRemoved(quint32 homeId, quint8 nodeId);
    void onValueAdded(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClass, quint8 instance, quint16 index, ZWaveValue::Type type);
    void onValueChanged(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClass, quint8 instance, quint16 index, ZWaveValue::Type type);
    void onValueRemoved(quint32 homeId, quint8 nodeId, quint64 id);
    void onNodeProtocolInfoReceived(quint32 homeId, quint8 nodeId);
    void onEssentialNodeQueriesComplete(quint32 homeId);
    void onNodeQueryComplete(quint32 homeId, quint8 nodeId);
    void onAwakeNodesQueried(quint32 homeId);
    void onAllNodesQueried(quint32 homeId);
    void onZWaveNotification(quint32 homeId, quint8 nodeId, OpenZWaveBackend::NotificationCode code);
    void onControllerCommand(quint32 homeId, OpenZWaveBackend::ControllerCommand command, OpenZWaveBackend::ControllerState state);

private:
    void initOZW(const QString &networkKey);
    void deinitOZW();

    static void ozwCallback(const OpenZWave::Notification *notification, void *context);

    ZWaveValue readValue(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClassId, quint8 instance, quint16 index, ZWaveValue::Type type);
    void updateNodeLinkQuality(quint32 homeId, quint8 nodeId);

    OpenZWave::Options *m_options = nullptr;
    OpenZWave::Manager *m_manager = nullptr;

    QHash<QUuid, QString> m_serialPorts;
    QHash<QUuid, quint32> m_homeIds;

    QList<QUuid> m_pendingNetworkSetups;

    QHash<quint32, ZWaveReply*> m_pendingControllerCommands;

#ifndef OZW_16
    ControllerCommand m_controllerCommand = ControllerCommandNone;
#endif
};


#endif // OPENZWAVEBACKEND_H
