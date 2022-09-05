#include "openzwavebackend.h"

#include <Options.h>
#include <Manager.h>
#include <Notification.h>
#include <platform/Log.h>
#include <Utils.h>

#include <QDir>
#include "nymeasettings.h"

#include "loggingcategories.h"
NYMEA_LOGGING_CATEGORY(dcOpenZWave, "OpenZWaveBackend")

OpenZWaveBackend::OpenZWaveBackend(QObject *parent)
    : ZWaveBackend(parent)
{
    qRegisterMetaType<OpenZWaveBackend::NotificationCode>();
    qRegisterMetaType<OpenZWaveBackend::ControllerCommand>();
    qRegisterMetaType<OpenZWaveBackend::ControllerState>();


}

OpenZWaveBackend::~OpenZWaveBackend()
{
    if (m_manager) {
        m_manager->Destroy();
        m_options->Destroy();
    }
}

bool OpenZWaveBackend::startNetwork(const QUuid &networkUuid, const QString &serialPort, const QString &networkKey)
{
    if (!m_options) {
        initOZW(networkKey);
    } else {
        qCWarning(dcOpenZWave()) << "OpenZWave does not support different network keys per network";
    }
    if (m_manager->AddDriver(serialPort.toStdString())) {
        m_pendingNetworkSetups.append(networkUuid);
        m_serialPorts.insert(networkUuid, serialPort);
        return true;
    }
    return false;
}

bool OpenZWaveBackend::stopNetwork(const QUuid &networkUuid)
{
    if (!m_serialPorts.contains(networkUuid)) {
        qCWarning(dcOpenZWave()) << "No network found for network uuid:" << networkUuid.toString();
        return false;
    }
    qCDebug(dcOpenZWave()) << "Removing driver:" << m_serialPorts.value(networkUuid);
    bool status = m_manager->RemoveDriver(m_serialPorts.value(networkUuid).toStdString());

    m_serialPorts.remove(networkUuid);
    m_homeIds.remove(networkUuid);

    if (m_serialPorts.isEmpty()) {
        deinitOZW();
    }
    return status;
}

quint32 OpenZWaveBackend::homeId(const QUuid &networkUuid)
{
    return m_homeIds.value(networkUuid);
}

quint8 OpenZWaveBackend::controllerNodeId(const QUuid &networkUuid)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->GetControllerNodeId(m_homeIds.value(networkUuid));
}

bool OpenZWaveBackend::isPrimaryController(const QUuid &networkUuid)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsPrimaryController(m_homeIds.value(networkUuid));
}

bool OpenZWaveBackend::isStaticUpdateController(const QUuid &networkUuid)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsStaticUpdateController(m_homeIds.value(networkUuid));
}

bool OpenZWaveBackend::isBridgeController(const QUuid &networkUuid)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsBridgeController(m_homeIds.value(networkUuid));
}

bool OpenZWaveBackend::factoryResetNetwork(const QUuid &networkUuid)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    m_pendingNetworkSetups.append(networkUuid);
    m_manager->ResetController(m_homeIds.value(networkUuid));
    return true;
}

ZWaveReply *OpenZWaveBackend::addNode(const QUuid &networkUuid, bool useSecurity)
{
    ZWaveReply *reply = new ZWaveReply(this);
    if (!m_homeIds.contains(networkUuid)) {
        finishReply(reply, ZWave::ZWaveErrorNetworkUuidNotFound);
        return reply;
    }
    quint32 homeId = m_homeIds.value(networkUuid);
    if (m_pendingControllerCommands.contains(homeId)) {
        reply->finished(ZWave::ZWaveErrorInUse);
        return reply;
    }
#ifndef OZW_16
    m_controllerCommand = ControllerCommandAddDevice;
#endif
    bool status = m_manager->AddNode(m_homeIds.value(networkUuid), useSecurity);
    if (!status) {
        finishReply(reply, ZWave::ZWaveErrorBackendError);
        return reply;
    }
    startReply(reply);
    connect(reply, &ZWaveReply::finished, this, [this, homeId](){
        m_pendingControllerCommands.remove(homeId);
    });
    m_pendingControllerCommands.insert(homeId, reply);
    return reply;
}

ZWaveReply *OpenZWaveBackend::removeNode(const QUuid &networkUuid)
{
    ZWaveReply *reply = new ZWaveReply(this);
    if (!m_homeIds.contains(networkUuid)) {
        finishReply(reply, ZWave::ZWaveErrorNetworkUuidNotFound);
        return reply;
    }
    quint32 homeId = m_homeIds.value(networkUuid);
    if (m_pendingControllerCommands.contains(homeId)) {
        finishReply(reply, ZWave::ZWaveErrorInUse);
        return reply;
    }
    qCDebug(dcOpenZWave()) << "Starting node removal procedure for network" << m_homeIds.value(networkUuid);
#ifndef OZW_16
    m_controllerCommand = ControllerCommandRemoveDevice;
#endif
    bool status = m_manager->RemoveNode(homeId);
    if (!status) {
        finishReply(reply, ZWave::ZWaveErrorBackendError);
        return reply;
    }
    startReply(reply);
    connect(reply, &ZWaveReply::finished, this, [this, homeId](){
        m_pendingControllerCommands.remove(homeId);
    });
    m_pendingControllerCommands.insert(homeId, reply);
    return reply;
}

ZWaveReply *OpenZWaveBackend::removeFailedNode(const QUuid &networkUuid, quint8 nodeId)
{
    ZWaveReply *reply = new ZWaveReply(this);
    if (!m_homeIds.contains(networkUuid)) {
        finishReply(reply, ZWave::ZWaveErrorNetworkUuidNotFound);
        return reply;
    }
    quint32 homeId = m_homeIds.value(networkUuid);
    if (m_pendingControllerCommands.contains(homeId)) {
        reply->finished(ZWave::ZWaveErrorInUse);
        return reply;
    }
    qCDebug(dcOpenZWave()) << "Removing failed node" << nodeId << "from network" << m_homeIds.value(networkUuid);

    bool status = m_manager->RemoveFailedNode(m_homeIds.value(networkUuid), nodeId);
    if (!status) {
        finishReply(reply, ZWave::ZWaveErrorBackendError);
        return reply;
    }
    startReply(reply);
    connect(reply, &ZWaveReply::finished, this, [this, homeId](){
        m_pendingControllerCommands.remove(homeId);
    });
    m_pendingControllerCommands.insert(homeId, reply);
    return reply;
}

ZWaveReply *OpenZWaveBackend::cancelPendingOperation(const QUuid &networkUuid)
{
    ZWaveReply *reply = new ZWaveReply(this);
    if (!m_homeIds.contains(networkUuid)) {
        finishReply(reply, ZWave::ZWaveErrorNetworkUuidNotFound);
        return reply;
    }

    qCDebug(dcOpenZWave()) << "Cancelling pending controller command";
    bool status = m_manager->CancelControllerCommand(m_homeIds.value(networkUuid));
    finishReply(reply, status ? ZWave::ZWaveErrorNoError : ZWave::ZWaveErrorInUse);
    return reply;
}

bool OpenZWaveBackend::isNodeAwake(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsNodeAwake(m_homeIds.value(networkUuid), nodeId);
}

bool OpenZWaveBackend::isNodeFailed(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsNodeFailed(m_homeIds.value(networkUuid), nodeId);
}

QString OpenZWaveBackend::nodeName(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return QString();
    }
    return QString::fromStdString(m_manager->GetNodeName(m_homeIds.value(networkUuid), nodeId));
}

ZWaveNode::ZWaveNodeType OpenZWaveBackend::nodeType(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return ZWaveNode::ZWaveNodeTypeUnknown;
    }
    return static_cast<ZWaveNode::ZWaveNodeType>(m_manager->GetNodeBasic(m_homeIds.value(networkUuid), nodeId));
}

ZWaveNode::ZWaveDeviceType OpenZWaveBackend::nodeDeviceType(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return ZWaveNode::ZWaveDeviceTypeUnknown;
    }
    return static_cast<ZWaveNode::ZWaveDeviceType>(m_manager->GetNodeDeviceType(m_homeIds.value(networkUuid), nodeId));
}

ZWaveNode::ZWaveNodeRole OpenZWaveBackend::nodeRole(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return ZWaveNode::ZWaveNodeRoleUnknown;
    }
    return static_cast<ZWaveNode::ZWaveNodeRole>(m_manager->GetNodeRole(m_homeIds.value(networkUuid), nodeId));
}

quint8 OpenZWaveBackend::nodeSecurityMode(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return ZWaveNode::ZWaveNodeRoleUnknown;
    }
    return static_cast<ZWaveNode::ZWaveNodeRole>(m_manager->GetNodeSecurity(m_homeIds.value(networkUuid), nodeId));
}

ZWaveNode::ZWavePlusDeviceType OpenZWaveBackend::nodePlusDeviceType(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return ZWaveNode::ZWavePlusDeviceTypeUnknown;
    }
    return static_cast<ZWaveNode::ZWavePlusDeviceType>(m_manager->GetNodePlusType(m_homeIds.value(networkUuid), nodeId));
}

bool OpenZWaveBackend::nodeIsSecureDevice(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }

    bool secured;
    OpenZWave::ValueID valueId(m_homeIds.value(networkUuid), nodeId, OpenZWave::ValueID::ValueGenre_System, 0x98, 0, 0, OpenZWave::ValueID::ValueType_Bool);
    try {
        m_manager->GetValueAsBool(valueId, &secured);
    } catch (OpenZWave::OZWException e) {
        secured = false;
    }
    return secured;
}

bool OpenZWaveBackend::nodeIsBeamingDevice(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsNodeBeamingDevice(m_homeIds.value(networkUuid), nodeId);
}

quint16 OpenZWaveBackend::nodeManufacturerId(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return 0;
    }
    return QString::fromStdString(m_manager->GetNodeManufacturerId(m_homeIds.value(networkUuid), nodeId)).remove("0x").toUInt(nullptr, 16);
}

QString OpenZWaveBackend::nodeManufacturerName(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return QString();
    }
    return QString::fromStdString(m_manager->GetNodeManufacturerName(m_homeIds.value(networkUuid), nodeId));
}

quint16 OpenZWaveBackend::nodeProductId(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return 0;
    }
    return QString::fromStdString(m_manager->GetNodeProductId(m_homeIds.value(networkUuid), nodeId)).remove("0x").toUInt(nullptr, 16);
}

QString OpenZWaveBackend::nodeProductName(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return QString();
    }
    return QString::fromStdString(m_manager->GetNodeProductName(m_homeIds.value(networkUuid), nodeId));
}

quint16 OpenZWaveBackend::nodeProductType(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return 0;
    }
    return QString::fromStdString(m_manager->GetNodeProductType(m_homeIds.value(networkUuid), nodeId)).remove("0x").toUInt(nullptr, 16);
}

quint8 OpenZWaveBackend::nodeVersion(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return 0;
    }
    return m_manager->GetNodeVersion(m_homeIds.value(networkUuid), nodeId);
}

bool OpenZWaveBackend::nodeIsZWavePlus(const QUuid &networkUuid, quint8 nodeId)
{
    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    return m_manager->IsNodeZWavePlus(m_homeIds.value(networkUuid), nodeId);
}

bool OpenZWaveBackend::setValue(const QUuid &networkUuid, quint8 nodeId, const ZWaveValue &value)
{
    Q_UNUSED(nodeId)


    if (!m_homeIds.contains(networkUuid)) {
        return false;
    }
    OpenZWave::ValueID valueId(m_homeIds.value(networkUuid), value.id());
    try {
        switch (value.type()) {
        case ZWaveValue::TypeBool:
            return m_manager->SetValue(valueId, value.value().toBool());
        case ZWaveValue::TypeButton:
            if (value.value().toBool()) {
                return m_manager->PressButton(valueId);
            } else {
                return m_manager->ReleaseButton(valueId);
            }
        case ZWaveValue::TypeByte:
            return m_manager->SetValue(valueId, static_cast<quint8>(value.value().toUInt()));
        case ZWaveValue::TypeShort:
            return m_manager->SetValue(valueId, static_cast<qint16>(value.value().toInt()));
        case ZWaveValue::TypeList: {
            QStringList values = value.value().toStringList();
            if (value.valueListSelection() < 0 || value.valueListSelection() >= values.count()) {
                qCWarning(dcOpenZWave()) << "Values:" << values << "has no index:" << value.valueListSelection();
                return false;
            }
            return m_manager->SetValueListSelection(valueId, values.at(value.valueListSelection()).toStdString());
        }
        default:
            qCritical(dcOpenZWave) << "SetValue type not handled:" << value.type();
            return false;
        }
    } catch (OpenZWave::OZWException e) {
        qCWarning(dcOpenZWave()) << "Error setting value:" << e.what();
        return false;
    }
}

ZWaveValue OpenZWaveBackend::readValue(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClassId, quint8 instance, quint16 index, ZWaveValue::Type type)
{
    OpenZWave::ValueID valueId(homeId, nodeId, (OpenZWave::ValueID::ValueGenre)genre, commandClassId, instance, index, (OpenZWave::ValueID::ValueType)type);
    QVariant variant;
    int selection = -1;

    switch (type) {
    case ZWaveValue::TypeButton:
    case ZWaveValue::TypeBool: {
        bool val;
        m_manager->GetValueAsBool(valueId, &val);
        variant = val;
        break;
    }
    case ZWaveValue::TypeShort: {
        qint16 val;
        m_manager->GetValueAsShort(valueId, &val);
        variant = val;
        break;
    }
    case ZWaveValue::TypeByte: {
        quint8 val;
        m_manager->GetValueAsByte(valueId, &val);
        variant = val;
        break;
    }
    case ZWaveValue::TypeInt: {
        qint32 val;
        m_manager->GetValueAsInt(valueId, &val);
        variant = val;
        break;
    }
    case ZWaveValue::TypeList: {
        std::vector<std::string> val;
        m_manager->GetValueListItems(valueId, &val);
        QStringList ret;
        for (const std::string &str: val) {
            ret.append(QString::fromStdString(str));
        }
        variant = ret;
        std::string selectionStr;
        m_manager->GetValueListSelection(valueId, &selectionStr);
        selection = ret.indexOf(QString::fromStdString(selectionStr));
        break;
    }
    case ZWaveValue::TypeDecimal: {
        float val;
        m_manager->GetValueAsFloat(valueId, &val);
        variant = val;
        break;
    }
    case ZWaveValue::TypeString: {
        std::string val;
        m_manager->GetValueAsString(valueId, &val);
        variant = QString::fromStdString(val);
        break;
    }



//    case ZWaveValue::TypeBitSet:
//        m_manager->GetValueAsBitSet(valueId, )
//        break;
    default:
        qCCritical(dcOpenZWave()) << "Unhandled type in readValue" << type;
    }

    QString description = QString::fromStdString(m_manager->GetValueHelp(valueId));

    ZWaveValue value(id, genre, commandClassId, instance, index, type, description);
    value.setValue(variant, selection);
    return value;
}

void OpenZWaveBackend::updateNodeLinkQuality(quint32 homeId, quint8 nodeId)
{
    OpenZWave::Node::NodeData nodeData;
    m_manager->GetNodeStatistics(homeId, nodeId, &nodeData);
//    qCDebug(dcOpenZWave()) << "Driver stats:" << nodeData.m_quality;

#ifdef OZW_16
//    qCDebug(dcOpenZWave()) << "RSSI values:" << nodeData.m_rssi_1 << QByteArray::fromHex(QByteArray(nodeData.m_rssi_1, 8));

    QStringList rssis = {
        QString(nodeData.m_rssi_1),
        QString(nodeData.m_rssi_2),
        QString(nodeData.m_rssi_3),
        QString(nodeData.m_rssi_4),
        QString(nodeData.m_rssi_5),
    };

    quint8 avg = 0;
    quint8 count = 0;
    foreach (const QString &rssi, rssis) {
        if (rssi == "MAX") {
            avg += -50;
            count++;
        } else if (rssi == "MIN") {
            avg += -100;
            count++;
        } else {
            bool ok;
            quint8 val = rssi.toInt(&ok);
            if (ok) {
                avg += val;
                count++;
            }
        }
    }
    if (count > 0) {
        avg /= count;
    } else {
        avg = -76;
    }

    quint8 linkQuality = qMin(100, qMax(0, 2 * (avg + 100)));
#else
    quint8 linkQuality = qMin(100, qMax(0, 2 * (nodeData.m_quality + 100)));
#endif

    emit nodeLinkQualityStatus(m_homeIds.key(homeId), nodeId, linkQuality);
}

void OpenZWaveBackend::ozwCallback(const OpenZWave::Notification *notification, void *context)
{
    Q_UNUSED(context)
    OpenZWaveBackend *self = static_cast<OpenZWaveBackend*>(context);

    switch (notification->GetType()) {
    case OpenZWave::Notification::Type_ValueAdded: {
        OpenZWave::ValueID valueId = notification->GetValueID();
        QMetaObject::invokeMethod(self, "onValueAdded",
                                  Q_ARG(quint32, notification->GetHomeId()),
                                  Q_ARG(quint8, notification->GetNodeId()),
                                  Q_ARG(quint64, valueId.GetId()),
                                  Q_ARG(ZWaveValue::Genre, static_cast<ZWaveValue::Genre>(valueId.GetGenre())),
                                  Q_ARG(ZWaveValue::CommandClass, static_cast<ZWaveValue::CommandClass>(valueId.GetCommandClassId())),
                                  Q_ARG(quint8, valueId.GetInstance()),
                                  Q_ARG(quint16, valueId.GetIndex()),
                                  Q_ARG(ZWaveValue::Type, static_cast<ZWaveValue::Type>(valueId.GetType()))
                                  );
        break;
    }
    case OpenZWave::Notification::Type_ValueChanged: {
        OpenZWave::ValueID valueId = notification->GetValueID();
        QMetaObject::invokeMethod(self, "onValueChanged",
                                  Q_ARG(quint32, notification->GetHomeId()),
                                  Q_ARG(quint8, notification->GetNodeId()),
                                  Q_ARG(quint64, valueId.GetId()),
                                  Q_ARG(ZWaveValue::Genre, static_cast<ZWaveValue::Genre>(valueId.GetGenre())),
                                  Q_ARG(ZWaveValue::CommandClass, static_cast<ZWaveValue::CommandClass>(valueId.GetCommandClassId())),
                                  Q_ARG(quint8, valueId.GetInstance()),
                                  Q_ARG(quint16, valueId.GetIndex()),
                                  Q_ARG(ZWaveValue::Type, static_cast<ZWaveValue::Type>(valueId.GetType()))
                                  );

        break;
    }
    case OpenZWave::Notification::Type_ValueRefreshed: {
        // TODO: executeAction could use this as reply..
        OpenZWave::ValueID valueId = notification->GetValueID();
        QMetaObject::invokeMethod(self, "onValueChanged",
                                  Q_ARG(quint32, notification->GetHomeId()),
                                  Q_ARG(quint8, notification->GetNodeId()),
                                  Q_ARG(quint64, valueId.GetId()),
                                  Q_ARG(ZWaveValue::Genre, static_cast<ZWaveValue::Genre>(valueId.GetGenre())),
                                  Q_ARG(ZWaveValue::CommandClass, static_cast<ZWaveValue::CommandClass>(valueId.GetCommandClassId())),
                                  Q_ARG(quint8, valueId.GetInstance()),
                                  Q_ARG(quint16, valueId.GetIndex()),
                                  Q_ARG(ZWaveValue::Type, static_cast<ZWaveValue::Type>(valueId.GetType()))
                                  );

        break;
    }
    case OpenZWave::Notification::Type_ValueRemoved:
        QMetaObject::invokeMethod(self, "onValueRemoved", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()), Q_ARG(quint64, notification->GetValueID().GetId()));
        break;
    case OpenZWave::Notification::Type_Group:
        qCDebug(dcOpenZWave) << "Group information changed for home Id" << notification->GetHomeId();
        break;
    case OpenZWave::Notification::Type_NodeNaming:
        QMetaObject::invokeMethod(self, "onNodeNaming", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_DriverReady:
        QMetaObject::invokeMethod(self, "onDriverReady", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_DriverFailed:
#ifdef OZW_16
        QMetaObject::invokeMethod(self, "onDriverFailed", Q_ARG(QString, QString::fromStdString(notification->GetComPort())));
#else
        QMetaObject::invokeMethod(self, "onDriverFailed");
#endif
        break;
    case OpenZWave::Notification::Type_NodeNew:
        QMetaObject::invokeMethod(self, "onNewNode", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_NodeAdded:
        QMetaObject::invokeMethod(self, "onNodeAdded", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_NodeRemoved:
        QMetaObject::invokeMethod(self, "onNodeRemoved", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_NodeProtocolInfo:
        QMetaObject::invokeMethod(self, "onNodeProtocolInfoReceived", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_NodeEvent:
        qCWarning(dcOpenZWave()) << "Node event:" << notification->GetEvent() << QString::fromStdString(notification->GetAsString());
        break;
    case OpenZWave::Notification::Type_Notification:
        QMetaObject::invokeMethod(self, "onZWaveNotification", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()), Q_ARG(OpenZWaveBackend::NotificationCode, static_cast<NotificationCode>(notification->GetNotification())));
        break;
    case OpenZWave::Notification::Type_EssentialNodeQueriesComplete:
        QMetaObject::invokeMethod(self, "onEssentialNodeQueriesComplete", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_NodeQueriesComplete:
        QMetaObject::invokeMethod(self, "onNodeQueryComplete", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(quint8, notification->GetNodeId()));
        break;
    case OpenZWave::Notification::Type_AwakeNodesQueried:
        QMetaObject::invokeMethod(self, "onAwakeNodesQueried", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_AllNodesQueriedSomeDead:
        QMetaObject::invokeMethod(self, "onAllNodesQueried", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_AllNodesQueried:
        QMetaObject::invokeMethod(self, "onAllNodesQueried", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_DriverRemoved:
        QMetaObject::invokeMethod(self, "onDriverRemoved", Q_ARG(quint32, notification->GetHomeId()));
        break;
    case OpenZWave::Notification::Type_ControllerCommand:
        // OZW docs seem broken... They claim that GetEvent -> ControllerCommand, and GetNotification -> ControllerState
        // However, at least in 1.6, GetEvent seems to return the ControllerState while there is a GetCommand to retrieve the command
#ifdef OZW_16
        QMetaObject::invokeMethod(self, "onControllerCommand", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(OpenZWaveBackend::ControllerCommand, static_cast<ControllerCommand>(notification->GetCommand())), Q_ARG(OpenZWaveBackend::ControllerState, static_cast<ControllerState>(notification->GetEvent())));
#else
        // Prior to 1.6, there's no GetCommand, let's hope it actually does what the docs say...
        qCDebug(dcOpenZWave()) << "Controller command callback received: \n"
//                               << "Command:" << static_cast<OpenZWaveBackend::ControllerCommand>(notification->GetCommand()) << notification->GetCommand() << "\n"
                               << "Event:" << static_cast<OpenZWaveBackend::ControllerState>(notification->GetEvent()) << notification->GetEvent() << "\n"
                               << "Notification:" << notification->GetNotification();
        QMetaObject::invokeMethod(self, "onControllerCommand", Q_ARG(quint32, notification->GetHomeId()), Q_ARG(OpenZWaveBackend::ControllerCommand, static_cast<ControllerCommand>(notification->GetEvent())), Q_ARG(OpenZWaveBackend::ControllerState, static_cast<ControllerState>(notification->GetEvent())));
#endif
        break;
//    case OpenZWave::Notification::Type_ManufacturerSpecificDBReady:
//        qCDebug(dcOpenZWave()) << "OpenZWave Manufacturer specific DB is ready...";
//        break;
#ifdef OZW_16
    case OpenZWave::Notification::Type_UserAlerts:
        qCWarning(dcOpenZWave()) << "OpenZWave user alert:" << static_cast<UserAlertNotification>(notification->GetUserAlertType()) << QString::fromStdString(notification->GetAsString());
        break;
#endif
    default:
        qCWarning(dcOpenZWave()) << "Unhandled notification" << notification->GetType();

    }
}

void OpenZWaveBackend::onDriverReady(quint32 homeId)
{
    if (m_pendingNetworkSetups.isEmpty()) {
        qCWarning(dcOpenZWave) << "Received a driver ready callback but we're not waiting for one!";
        return;
    }

    // Note: OZW doesn't give us any way to match this callback with an AddDriver call ¯\_(ツ)_/¯
    // So we'll just use the first pending network uuid
    // If the user creates 2 new networks and callbacks return in a different order, this will fail...
    qCDebug(dcOpenZWave) << "Network ready with homeId" << homeId;
#ifdef OZW_16
    qCDebug(dcOpenZWave) << "Controller" << (m_manager->HasExtendedTxStatus(homeId) ? "supports" : "does not support") << "extended TxStatus reporting.";
#endif
    QUuid networkUuid = m_pendingNetworkSetups.takeFirst();
    m_homeIds.insert(networkUuid, homeId);
    emit networkStarted(m_homeIds.key(homeId));
}

#ifdef OZW_16
void OpenZWaveBackend::onDriverFailed(const QString &serialPort)
{
    if (!m_serialPorts.values().contains(serialPort)) {
        qCWarning(dcOpenZWave()) << "Received a driver failed callback for a serial port we don't know:" << serialPort;
        return;
    }
    qCWarning(dcOpenZWave()) << "Driver failed for serial port" << serialPort;
    emit networkFailed(m_serialPorts.key(serialPort));
}
#else
void OpenZWaveBackend::onDriverFailed()
{
    // Note: OZW < 1.6 doesn't give us any way to match this callback with an AddDriver call ¯\_(ツ)_/¯
    // So we'll just use the first pending network uuid
    // If the user creates 2 new networks and callbacks return in a different order, this will fail...
    qCDebug(dcOpenZWave) << "Driver failed";
    QUuid networkUuid = m_pendingNetworkSetups.takeFirst();
    emit networkFailed(networkUuid);
}
#endif

void OpenZWaveBackend::onDriverRemoved(quint32 homeId)
{
    qCInfo(dcOpenZWave()) << "Driver removed for network" << homeId;
}

// When a new node joins the network, we'll get onNewNode and eventually onNodeAdded.
// We'll also get onNodeAdded on every reboot. So in theory just reacting to one of the two
// would be enough however, we could miss onNewNode, for instance if nymea wasn't running while it
// joined (e.g. by button link with the ZWave stick). Also, if we do get onNewNode, we'll also get
// other callbacks before onNodeAdded. So we'll want to act on the first callback we get.
// As we don't keep track of the nodes in there, let's hope ZWaveManager deduplicates the nodeAdded signal
// properly.
void OpenZWaveBackend::onNewNode(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a new node callback for a network we don't know:" << homeId;
        return;
    }
    qCInfo(dcOpenZWave()) << "New node" << nodeId << "for network" << homeId;
    emit nodeAdded(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onNodeAdded(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node added callback for a network we don't know:" << homeId;
        return;
    }
    qCInfo(dcOpenZWave()) << "Node" << nodeId << "added to network" << homeId;
    emit nodeAdded(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onNodeNaming(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node naming callback for a network we don't know:" << homeId;
        return;
    }
    qCInfo(dcOpenZWave()) << "Node names changed for node" << nodeId << "in network" << homeId;
    emit nodeDataChanged(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onNodeRemoved(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node naming callback for a network we don't know:" << homeId;
        return;
    }
    qCInfo(dcOpenZWave()) << "Node" << nodeId << "removed from network" << homeId;
    emit nodeRemoved(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onValueAdded(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClass, quint8 instance, quint16 index, ZWaveValue::Type type)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a value added callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave()) << "Value" << id << "added to node" << nodeId << "in network" << homeId;
    emit valueAdded(m_homeIds.key(homeId), nodeId, readValue(homeId, nodeId, id, genre, commandClass, instance, index, type));
    updateNodeLinkQuality(homeId, nodeId);
}

void OpenZWaveBackend::onValueChanged(quint32 homeId, quint8 nodeId, quint64 id, ZWaveValue::Genre genre, ZWaveValue::CommandClass commandClass, quint8 instance, quint16 index, ZWaveValue::Type type)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a value changed callback for a network we don't know:" << homeId;
        return;
    }
    QUuid networkUuid = m_homeIds.key(homeId);
    qCDebug(dcOpenZWave()) << "Value" << id << "changed for node" << nodeId << "in network" << homeId;
    emit valueChanged(networkUuid, nodeId, readValue(homeId, nodeId, id, genre, commandClass, instance, index, type));

    // emitting node reachable because the appropriate notification doesn't always seem to come in, even if we're talking to the device
    emit nodeReachableStatus(networkUuid, nodeId, true);

    updateNodeLinkQuality(homeId, nodeId);
}

void OpenZWaveBackend::onValueRemoved(quint32 homeId, quint8 nodeId, quint64 id)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a value changed callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave()) << "Value" << id << "removed from node" << nodeId << "in network" << homeId;
    emit valueRemoved(m_homeIds.key(homeId), nodeId, id);
}

void OpenZWaveBackend::onNodeProtocolInfoReceived(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node proticol info callback for a network we don't know:" << homeId;
        return;
    }
    qCInfo(dcOpenZWave()) << "Protocol info changed for node" << nodeId << "in network" << homeId;
    emit nodeDataChanged(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onEssentialNodeQueriesComplete(quint32 homeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node queries complete callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave) << "Essential node queries complete for network" << homeId;
}

void OpenZWaveBackend::onNodeQueryComplete(quint32 homeId, quint8 nodeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a node query complete callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave()) << "Node query complete for node" << nodeId << "in network" << homeId;
    emit nodeInitialized(m_homeIds.key(homeId), nodeId);
    nodeIsSecureDevice(m_homeIds.key(homeId), nodeId);
}

void OpenZWaveBackend::onAwakeNodesQueried(quint32 homeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received an awake nodes queried callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave) << "Awake nodes queried for network" << homeId;
}

void OpenZWaveBackend::onAllNodesQueried(quint32 homeId)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received an all nodes queried callback for a network we don't know:" << homeId;
        return;
    }
    qCDebug(dcOpenZWave) << "All nodes queried in network" << homeId;
}

void OpenZWaveBackend::onZWaveNotification(quint32 homeId, quint8 nodeId, NotificationCode code)
{
    if (homeId == 0) {
        if (code == NotificationCodeTimeout && m_pendingNetworkSetups.count() > 0) {
            QUuid networkUuid = m_pendingNetworkSetups.takeFirst();
            qCWarning(dcOpenZWave()) << "AddDriver timed out for network" << networkUuid.toString();
            m_manager->RemoveDriver(m_serialPorts.value(networkUuid).toStdString());
            emit networkFailed(networkUuid);
            return;
        }
    }
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a zwave notification callback for a network we don't know:" << homeId << code;
        return;
    }

    switch (code) {
    case NotificationCodeDead:
        qCDebug(dcOpenZWave) << "Node" << nodeId << "in network" << homeId << "is dead";
        emit nodeFailedStatus(m_homeIds.key(homeId), nodeId, true);
        emit nodeReachableStatus(m_homeIds.key(homeId), nodeId, false);
        break;
    case NotificationCodeTimeout:
        qCDebug(dcOpenZWave) << "Node timeout for node" << nodeId << "in network" << homeId;
        emit nodeReachableStatus(m_homeIds.key(homeId), nodeId, false);
        break;
    case NotificationCodeAlive:
        qCDebug(dcOpenZWave) << "Node" << nodeId << "in network" << homeId << "is alive";
        emit nodeReachableStatus(m_homeIds.key(homeId), nodeId, true);
        break;
    case NotificationCodeNoOperation:
        qCDebug(dcOpenZWave()) << "NoOperation command sent to node:" << nodeId << "in network" << homeId;
        break;
    case NotificationCodeSleep:
        qCDebug(dcOpenZWave()) << "Node" << nodeId << "in network" << homeId << "is sleeping";
        emit nodeSleepStatus(m_homeIds.key(homeId), nodeId, true);
        break;
    case NotificationCodeAwake:
        qCDebug(dcOpenZWave()) << "Node" << nodeId << "in network" << homeId << "is awake";
        emit nodeSleepStatus(m_homeIds.key(homeId), nodeId, false);
        break;
    default:
        qCWarning(dcOpenZWave()) << "Unhandled ZWave notification code:" << code << "for node" << nodeId << "in network" << homeId;
    }
}

void OpenZWaveBackend::onControllerCommand(quint32 homeId, ControllerCommand command, ControllerState state)
{
    if (!m_homeIds.values().contains(homeId)) {
        qCWarning(dcOpenZWave()) << "Received a controller command callback for a network we don't know:" << homeId;
        return;
    }

    qCDebug(dcOpenZWave()) << "Controller command:"  << command << state;

#ifndef OZW_16
    // OZW prior to 1.6 is broken and doesn't give us the command (always set to None). So let's recall what we're waiting
    // for and hope it lines up...
    command = m_controllerCommand;
#endif


    switch (command) {
    case ControllerCommandAddDevice:
        if (state == ControllerStateError || state == ControllerStateFailed) {
            qCWarning(dcOpenZWave()) << "Adding node to network" << homeId << "failed";
            if (m_pendingControllerCommands.contains(homeId)) {
                finishReply(m_pendingControllerCommands.take(homeId), ZWave::ZWaveErrorBackendError);
            }
#ifndef OZW_16
            m_controllerCommand = ControllerCommandNone;
#endif
        } else if (state == ControllerStateWaiting || state == ControllerStateNormal) {
            qCInfo(dcOpenZWave()) << "Waiting for node addition in network" << homeId;
            if (m_pendingControllerCommands.contains(homeId)) {
                finishReply(m_pendingControllerCommands.take(homeId), ZWave::ZWaveErrorNoError);
            }
            emit waitingForNodeAdditionChanged(m_homeIds.key(homeId), true);
        } else if (state == ControllerStateCompleted) {
            qCInfo(dcOpenZWave()) << "Node addition completed in network" << homeId;
            emit waitingForNodeAdditionChanged(m_homeIds.key(homeId), false);
#ifndef OZW_16
            m_controllerCommand = ControllerCommandNone;
#endif
        } else {
            qCDebug(dcOpenZWave) << "Add node state changed to" << state << "for network" << homeId;
        }
        break;
    case ControllerCommandRemoveDevice:
        if (state == ControllerStateError || state == ControllerStateFailed) {
            qCWarning(dcOpenZWave()) << "Removing node from network" << homeId << "failed";
            if (m_pendingControllerCommands.contains(homeId)) {
                finishReply(m_pendingControllerCommands.take(homeId), ZWave::ZWaveErrorBackendError);
            }
#ifndef OZW_16
            m_controllerCommand = ControllerCommandNone;
#endif
        } else if (state == ControllerStateWaiting || state == ControllerStateNormal) {
            qCInfo(dcOpenZWave()) << "Waiting for node removal in network" << homeId;
            if (m_pendingControllerCommands.contains(homeId)) {
                finishReply(m_pendingControllerCommands.take(homeId), ZWave::ZWaveErrorNoError);
            }
            emit waitingForNodeRemovalChanged(m_homeIds.key(homeId), true);
        } else if (state == ControllerStateCompleted) {
            qCInfo(dcOpenZWave()) << "Node removal completed in network" << homeId;
            emit waitingForNodeRemovalChanged(m_homeIds.key(homeId), false);
#ifndef OZW_16
            m_controllerCommand = ControllerCommandNone;
#endif
        } else {
            qCDebug(dcOpenZWave) << "Remove node state changed to" << state << "for network" << homeId;
        }
        break;

    default:
        // Hack: sometimes we call add or remove, but we get other commands in return.
        // for example on a ControllerCommandRemoveDevice, sometimes the completed call comes with ControllerCommandReplaceFailedNode
        // Not sure if that's a bug ni OZW, or if there's some fancy Z-Wave specced mechanism to do this stuff.
        // In any case, once a Completed comes in, anything previously isn't valid any more. so let's reset stuff
        if (state == ControllerStateCompleted) {
            emit waitingForNodeAdditionChanged(m_homeIds.key(homeId), false);
            emit waitingForNodeRemovalChanged(m_homeIds.key(homeId), false);
        }
        qCWarning(dcOpenZWave()) << "Unhandled controller command"  << command << state;
    }
}

void OpenZWaveBackend::initOZW(const QString &networkKey)
{
    QString userPath = NymeaSettings::storagePath() + "/openzwave/";
    QDir dir(userPath);
    if (!dir.exists()) {
        dir.mkpath(userPath);
    }

    m_options = OpenZWave::Options::Create("/etc/openzwave/", userPath.toStdString(), "");

    m_options->AddOptionInt("SaveLogLevel", OpenZWave::LogLevel_Detail );
    m_options->AddOptionInt("QueueLogLevel", OpenZWave::LogLevel_Detail );
    m_options->AddOptionInt("DumpTrigger", OpenZWave::LogLevel_Detail );
    m_options->AddOptionBool("Logging", false);
    m_options->AddOptionBool("ConsoleOutput", false);

    m_options->AddOptionInt("PollInterval", 5);
    m_options->AddOptionBool("IntervalBetweenPolls", true);
    m_options->AddOptionBool("ValidateValueChanges", true);

    // OZW wants the format: "0x01, 0x02, 0x04..."
    QString key = networkKey;
    for (int i = 15; i > 0; i--) {
        key.insert(i*2, ", 0x");
    }
    key.prepend("0x");
    m_options->AddOptionString("NetworkKey", key.toStdString(), false);

    m_options->Lock();

    m_manager = OpenZWave::Manager::Create();
    m_manager->AddWatcher(ozwCallback, this);
}

void OpenZWaveBackend::deinitOZW()
{
    m_manager->Destroy();
    m_manager = nullptr;
    m_options->Destroy();
    m_options = nullptr;
}

