/*
 * Copyright (c) 2009, 2011 CTTC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Nicola Baldo <nbaldo@cttc.es>
 *         Giuseppe Piro  <g.piro@poliba.it>
 *         Marco Miozzo <marco.miozzo@cttc.es> (add physical error model)
 * Modified by: NIST // Contributions may not be subject to US copyright.
 */

#include "lte-spectrum-phy.h"

#include "lte-chunk-processor.h"
#include "lte-control-messages.h"
#include "lte-mi-error-model.h"
#include "lte-radio-bearer-tag.h"
#include "lte-sl-chunk-processor.h"
#include "lte-sl-header.h"
#include "lte-sl-tag.h"
#include "lte-spectrum-signal-parameters.h"
#include "lte-ue-phy.h"

#include "ns3/enum.h"
#include <ns3/antenna-model.h>
#include <ns3/boolean.h>
#include <ns3/config.h>
#include <ns3/double.h>
#include <ns3/log.h>
#include <ns3/node.h>
#include <ns3/object-factory.h>
#include <ns3/pointer.h>
#include <ns3/ptr.h>
#include <ns3/simulator.h>
#include <ns3/trace-source-accessor.h>

#include <cmath>
#include <queue>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("LteSpectrumPhy");

/// duration of SRS portion of UL subframe
/// = 1 symbol for SRS -1ns as margin to avoid overlapping simulator events
static const Time UL_SRS_DURATION = NanoSeconds(71429 - 1);

/// duration of the control portion of a subframe
/// = 0.001 / 14 * 3 (ctrl fixed to 3 symbols) -1ns as margin to avoid overlapping simulator events
static const Time DL_CTRL_DURATION = NanoSeconds(214286 - 1);

/// Effective coding rate
static const double EffectiveCodingRate[29] = {
    0.08, 0.1, 0.11, 0.15, 0.19, 0.24, 0.3, 0.37, 0.44, 0.51, 0.3, 0.33, 0.37, 0.42, 0.48,
    0.54, 0.6, 0.43, 0.45, 0.5,  0.55, 0.6, 0.65, 0.7,  0.75, 0.8, 0.85, 0.89, 0.92,
};

TbId_t::TbId_t()
{
}

TbId_t::TbId_t(const uint16_t a, const uint8_t b)
    : m_rnti(a),
      m_layer(b)
{
}

std::vector<uint32_t> temp1(50,0);
std::vector<std::vector<uint32_t>> SINRperRBtable(10,temp1);
std::vector<double> ContentionRatio(10,0);
std::vector<int> ackStatus(2000,0);
std::queue <int> ack;

// extern std::vector<uint32_t> SINRperCF;
std::vector<uint32_t> SINRperCF(5,0);
int collisioncount = 0;
/**
 * Equality operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if rnti and layer are equal
 */
bool
operator==(const TbId_t& a, const TbId_t& b)
{
    return ((a.m_rnti == b.m_rnti) && (a.m_layer == b.m_layer));
}

/**
 * Less than operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if rnti less than ro rnti equal and layer less than
 */
bool
operator<(const TbId_t& a, const TbId_t& b)
{
    return ((a.m_rnti < b.m_rnti) || ((a.m_rnti == b.m_rnti) && (a.m_layer < b.m_layer)));
}

SlTbId_t::SlTbId_t()
{
}

SlTbId_t::SlTbId_t(const uint16_t a, const uint8_t b)
    : m_rnti(a),
      m_l1dst(b)
{
}

/**
 * Equality operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the rnti and l1dst of the first
 * parameter are equal to the rnti and l1dst of the
 * second parameter
 */
bool
operator==(const SlTbId_t& a, const SlTbId_t& b)
{
    return ((a.m_rnti == b.m_rnti) && (a.m_l1dst == b.m_l1dst));
}

/**
 * Less than operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the rnti of the first
 * parameter is less than the rnti of the
 * second parameter. OR returns true if the
 * rntis are equal and l1dst of the first
 * parameter is less than the l1dst of the
 * second parameter
 */
bool
operator<(const SlTbId_t& a, const SlTbId_t& b)
{
    return ((a.m_rnti < b.m_rnti) || ((a.m_rnti == b.m_rnti) && (a.m_l1dst < b.m_l1dst)));
}

/**
 * Equality operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the SINR of the first
 * parameter is equal to the SINR of the
 * second parameter
 */
bool
operator==(const SlCtrlPacketInfo_t& a, const SlCtrlPacketInfo_t& b)
{
    return (a.sinr == b.sinr);
}

/**
 * Less than operator
 * Used as comparison rule for std::multiset
 * of SlCtrlPacketInfo_t objects
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the SINR of the first
 * parameter is greater than SINR of the
 * second parameter OR the index of the first
 * parameter is less than the index of the
 * second parameter
 */
bool
operator<(const SlCtrlPacketInfo_t& a, const SlCtrlPacketInfo_t& b)
{
    // we want by decreasing SINR. The second condition will make
    // sure that the two TBs with equal SINR are inserted in increasing
    // order of the index.
    return (a.sinr > b.sinr) || (a.index < b.index);
}

SlDiscTbId_t::SlDiscTbId_t()
{
}

SlDiscTbId_t::SlDiscTbId_t(const uint16_t a, const uint8_t b)
    : m_rnti(a),
      m_resPsdch(b)
{
}

/**
 * Equality operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the rnti and PSDCH resource index of the first
 * parameter are equal to the rnti and PSDCH resource index of the
 * second parameter
 */
bool
operator==(const SlDiscTbId_t& a, const SlDiscTbId_t& b)
{
    return ((a.m_rnti == b.m_rnti) && (a.m_resPsdch == b.m_resPsdch));
}

/**
 * Less than operator
 *
 * \param a lhs
 * \param b rhs
 * \returns true if the rnti of the first parameter is less than
 * the rnti of the second parameter. OR returns true if the
 * rntis are equal and PSDCH resource index of the first parameter
 * is less than the PSDCH resource index of the second parameter.
 */
bool
operator<(const SlDiscTbId_t& a, const SlDiscTbId_t& b)
{
    return ((a.m_rnti < b.m_rnti) || ((a.m_rnti == b.m_rnti) && (a.m_resPsdch < b.m_resPsdch)));
}

NS_OBJECT_ENSURE_REGISTERED(LteSpectrumPhy);

LteSpectrumPhy::LteSpectrumPhy()
    : m_state(IDLE),
      m_cellId(0),
      m_componentCarrierId(0),
      m_transmissionMode(0),
      m_layersNum(1),
      m_ulDataSlCheck(false),
      m_slssId(0)
{
    NS_LOG_FUNCTION(this);
    m_random = CreateObject<UniformRandomVariable>();
    m_random->SetAttribute("Min", DoubleValue(0.0));
    m_random->SetAttribute("Max", DoubleValue(1.0));
    m_interferenceData = CreateObject<LteInterference>();
    m_interferenceCtrl = CreateObject<LteInterference>();
    m_interferenceSl = CreateObject<LteSlInterference>();

    for (uint8_t i = 0; i < 7; i++)
    {
        m_txModeGain.push_back(1.0);
    }
}

LteSpectrumPhy::~LteSpectrumPhy()
{
    NS_LOG_FUNCTION(this);
    m_expectedTbs.clear();
    m_expectedSlTbs.clear();
    m_txModeGain.clear();
    m_slDiscTxCount.clear();
}

void
LteSpectrumPhy::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_channel = nullptr;
    m_mobility = nullptr;
    m_device = nullptr;
    m_interferenceData->Dispose();
    m_interferenceData = nullptr;
    m_interferenceCtrl->Dispose();
    m_interferenceCtrl = nullptr;
    m_interferenceSl->Dispose();
    m_interferenceSl = nullptr;
    m_ulDataSlCheck = false;
    m_ltePhyRxDataEndErrorCallback = MakeNullCallback<void>();
    m_ltePhyRxDataEndOkCallback = MakeNullCallback<void, Ptr<Packet>>();
    m_ltePhyRxCtrlEndOkCallback = MakeNullCallback<void, std::list<Ptr<LteControlMessage>>>();
    m_ltePhyRxCtrlEndErrorCallback = MakeNullCallback<void>();
    m_ltePhyDlHarqFeedbackCallback = MakeNullCallback<void, DlInfoListElement_s>();
    m_ltePhyUlHarqFeedbackCallback = MakeNullCallback<void, UlInfoListElement_s>();
    m_ltePhyRxPssCallback = MakeNullCallback<void, uint16_t, Ptr<SpectrumValue>>();
    m_ltePhyRxSlssCallback = MakeNullCallback<void, uint16_t, Ptr<SpectrumValue>>();
    SpectrumPhy::DoDispose();
}

/**
 * Output stream output operator
 *
 * \param os output stream
 * \param s state
 * \returns output stream
 */
std::ostream&
operator<<(std::ostream& os, LteSpectrumPhy::State s)
{
    switch (s)
    {
    case LteSpectrumPhy::IDLE:
        os << "IDLE";
        break;
    case LteSpectrumPhy::RX_DATA:
        os << "RX_DATA";
        break;
    case LteSpectrumPhy::RX_DL_CTRL:
        os << "RX_DL_CTRL";
        break;
    case LteSpectrumPhy::TX_DATA:
        os << "TX_DATA";
        break;
    case LteSpectrumPhy::TX_DL_CTRL:
        os << "TX_DL_CTRL";
        break;
    case LteSpectrumPhy::TX_UL_SRS:
        os << "TX_UL_SRS";
        break;
    default:
        os << "UNKNOWN";
        break;
    }
    return os;
}

TypeId
LteSpectrumPhy::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::LteSpectrumPhy")
            .SetParent<SpectrumPhy>()
            .SetGroupName("Lte")
            .AddTraceSource("TxStart",
                            "Trace fired when a new transmission is started",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_phyTxStartTrace),
                            "ns3::PacketBurst::TracedCallback")
            .AddTraceSource("TxEnd",
                            "Trace fired when a previously started transmission is finished",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_phyTxEndTrace),
                            "ns3::PacketBurst::TracedCallback")
            .AddTraceSource("RxStart",
                            "Trace fired when the start of a signal is detected",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_phyRxStartTrace),
                            "ns3::PacketBurst::TracedCallback")
            .AddTraceSource("RxEndOk",
                            "Trace fired when a previously started RX terminates successfully",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_phyRxEndOkTrace),
                            "ns3::Packet::TracedCallback")
            .AddTraceSource("RxEndError",
                            "Trace fired when a previously started RX terminates with an error",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_phyRxEndErrorTrace),
                            "ns3::Packet::TracedCallback")
            .AddAttribute("DataErrorModelEnabled",
                          "Activate/Deactivate the error model of data (TBs of PDSCH and PUSCH) "
                          "[by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_dataErrorModelEnabled),
                          MakeBooleanChecker())
            .AddAttribute("CtrlErrorModelEnabled",
                          "Activate/Deactivate the error model of control (PCFICH-PDCCH "
                          "decodification) [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_ctrlErrorModelEnabled),
                          MakeBooleanChecker())
            .AddTraceSource("DlPhyReception",
                            "DL reception PHY layer statistics.",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_dlPhyReception),
                            "ns3::PhyReceptionStatParameters::TracedCallback")
            .AddTraceSource("UlPhyReception",
                            "DL reception PHY layer statistics.",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_ulPhyReception),
                            "ns3::PhyReceptionStatParameters::TracedCallback")
            .AddAttribute("DropRbOnCollisionEnabled",
                          "Activate/Deactivate the dropping colliding RBs regardless SINR value "
                          "[by default is not active].",
                          BooleanValue(false),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_dropRbOnCollisionEnabled),
                          MakeBooleanChecker())
            .AddAttribute("SlDataErrorModelEnabled",
                          "Activate/Deactivate the error model for the Sidelink PSSCH "
                          "decodification [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_slDataErrorModelEnabled),
                          MakeBooleanChecker())
            .AddAttribute("SlCtrlErrorModelEnabled",
                          "Activate/Deactivate the error model for the Sidelink PSCCH "
                          "decodification [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_slCtrlErrorModelEnabled),
                          MakeBooleanChecker())
            .AddAttribute("SlDiscoveryErrorModelEnabled",
                          "Activate/Deactivate the error model for the Sidelink PSDCH "
                          "decodification [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&LteSpectrumPhy::m_slDiscoveryErrorModelEnabled),
                          MakeBooleanChecker())
            .AddAttribute("FadingModel",
                          "Fading model",
                          EnumValue(LteNistErrorModel::AWGN),
                          MakeEnumAccessor(&LteSpectrumPhy::m_fadingModel),
                          MakeEnumChecker(LteNistErrorModel::AWGN, "AWGN"))
            .AddAttribute("HalfDuplexPhy",
                          "A pointer to a UL LteSpectrumPhy object",
                          PointerValue(),
                          MakePointerAccessor(&LteSpectrumPhy::m_halfDuplexPhy),
                          MakePointerChecker<LteSpectrumPhy>())
            .AddAttribute(
                "CtrlFullDuplexEnabled",
                "Activate/Deactivate the full duplex in the PSCCH [by default is disable].",
                BooleanValue(false),
                MakeBooleanAccessor(&LteSpectrumPhy::m_ctrlFullDuplexEnabled),
                MakeBooleanChecker())
            .AddTraceSource("SlPhyReception",
                            "SL reception PHY layer statistics.",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_slPhyReception),
                            "ns3::PhyReceptionStatParameters::TracedCallback")
            .AddTraceSource("SlPscchReception",
                            "SL reception PSCCH PHY layer statistics.",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_slPscchReception),
                            "ns3::SlPhyReceptionStatParameters::TracedCallback")
            .AddTraceSource("SlStartRx",
                            "Trace fired when reception at Sidelink starts.",
                            MakeTraceSourceAccessor(&LteSpectrumPhy::m_slStartRx),
                            "ns3::LteSpectrumPhy::SlStartRxTracedCallback");
    return tid;
}

Ptr<NetDevice>
LteSpectrumPhy::GetDevice() const
{
    NS_LOG_FUNCTION(this);
    return m_device;
}

Ptr<MobilityModel>
LteSpectrumPhy::GetMobility() const
{
    NS_LOG_FUNCTION(this);
    return m_mobility;
}

void
LteSpectrumPhy::SetDevice(Ptr<NetDevice> d)
{
    NS_LOG_FUNCTION(this << d);
    m_device = d;
}

void
LteSpectrumPhy::SetMobility(Ptr<MobilityModel> m)
{
    NS_LOG_FUNCTION(this << m);
    m_mobility = m;
}

void
LteSpectrumPhy::SetChannel(Ptr<SpectrumChannel> c)
{
    NS_LOG_FUNCTION(this << c);
    m_channel = c;
}

Ptr<SpectrumChannel>
LteSpectrumPhy::GetChannel()
{
    return m_channel;
}

Ptr<const SpectrumModel>
LteSpectrumPhy::GetRxSpectrumModel() const
{
    return m_rxSpectrumModel;
}

void
LteSpectrumPhy::SetTxPowerSpectralDensity(Ptr<SpectrumValue> txPsd)
{
    NS_LOG_FUNCTION(this << txPsd);
    NS_ASSERT(txPsd);
    m_txPsd = txPsd;
}

void
LteSpectrumPhy::SetNoisePowerSpectralDensity(Ptr<const SpectrumValue> noisePsd)
{
    NS_LOG_FUNCTION(this << noisePsd);
    NS_ASSERT(noisePsd);
    m_rxSpectrumModel = noisePsd->GetSpectrumModel();
    m_interferenceData->SetNoisePowerSpectralDensity(noisePsd);
    m_interferenceCtrl->SetNoisePowerSpectralDensity(noisePsd);
    m_interferenceSl->SetNoisePowerSpectralDensity(noisePsd);
}

void
LteSpectrumPhy::Reset()
{
    NS_LOG_FUNCTION(this);
    m_cellId = 0;
    m_state = IDLE;
    m_transmissionMode = 0;
    m_layersNum = 1;
    m_endTxEvent.Cancel();
    m_endRxDataEvent.Cancel();
    m_endRxDlCtrlEvent.Cancel();
    m_endRxUlSrsEvent.Cancel();
    m_rxControlMessageList.clear();
    m_expectedTbs.clear();
    m_txControlMessageList.clear();
    m_rxPacketBurstList.clear();
    m_txPacketBurst = nullptr;
    m_rxSpectrumModel = nullptr;
    m_slssId = 0;
    m_halfDuplexPhy = nullptr;
    m_ulDataSlCheck = false;

    // Detach from the channel, because receiving any signal without
    // spectrum model is an error.
    if (m_channel)
    {
        m_channel->RemoveRx(this);
    }
}

void
LteSpectrumPhy::SetLtePhyRxDataEndErrorCallback(LtePhyRxDataEndErrorCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxDataEndErrorCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxDataEndOkCallback(LtePhyRxDataEndOkCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxDataEndOkCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxPscchEndOkCallback(LtePhyRxDataEndOkCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxPscchEndOkCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxPsdchEndOkCallback(LtePhyRxDataEndOkCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxPsdchEndOkCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxPsbchEndOkCallback(LtePhyRxPsbchEndOkCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxPsbchEndOkCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxCtrlEndOkCallback(LtePhyRxCtrlEndOkCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxCtrlEndOkCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxCtrlEndErrorCallback(LtePhyRxCtrlEndErrorCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxCtrlEndErrorCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxPssCallback(LtePhyRxPssCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxPssCallback = c;
}

void
LteSpectrumPhy::SetLtePhyDlHarqFeedbackCallback(LtePhyDlHarqFeedbackCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyDlHarqFeedbackCallback = c;
}

void
LteSpectrumPhy::SetLtePhyUlHarqFeedbackCallback(LtePhyUlHarqFeedbackCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyUlHarqFeedbackCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxSlssCallback(LtePhyRxSlssCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxSlssCallback = c;
}

void
LteSpectrumPhy::SetLtePhyRxPsdchSdRsrpCallback(LtePhyRxPsdchSdRsrpCallback c)
{
    NS_LOG_FUNCTION(this);
    m_ltePhyRxPsdchSdRsrpCallback = c;
}

Ptr<Object>
LteSpectrumPhy::GetAntenna() const
{
    return m_antenna;
}

void
LteSpectrumPhy::SetAntenna(Ptr<AntennaModel> a)
{
    NS_LOG_FUNCTION(this << a);
    m_antenna = a;
}

void
LteSpectrumPhy::SetState(State newState)
{
    NS_LOG_FUNCTION(this << m_state);
    ChangeState(newState);
}

void
LteSpectrumPhy::ChangeState(State newState)
{
    NS_LOG_FUNCTION(this << m_state);
    NS_LOG_LOGIC("State change: " << m_state << " -> " << newState);
    m_state = newState;
}

void
LteSpectrumPhy::SetHarqPhyModule(Ptr<LteHarqPhy> harq)
{
    m_harqPhyModule = harq;
}

void
LteSpectrumPhy::SetSlHarqPhyModule(Ptr<LteSlHarqPhy> harq)
{
    NS_LOG_FUNCTION(this);
    m_slHarqPhyModule = harq;
}

void
LteSpectrumPhy::ClearExpectedSlTb()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("Expected TBs: " << m_expectedSlTbs.size());
    m_expectedSlTbs.clear();
    NS_LOG_DEBUG("After clearing SL expected TBs map. Map size : " << m_expectedSlTbs.size());
}

void
LteSpectrumPhy::ClearExpectedDiscTb()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_DEBUG("Expected TBs: " << m_expectedDiscTbs.size());
    m_expectedDiscTbs.clear();
    m_slDiscTxCount.clear();
    NS_LOG_DEBUG(
        "After clearing expected discovery TB map. Map size : " << m_expectedDiscTbs.size());
}

bool
LteSpectrumPhy::StartTxDataFrame(Ptr<PacketBurst> pb,
                                 std::list<Ptr<LteControlMessage>> ctrlMsgList,
                                 Time duration)
{
    NS_LOG_FUNCTION(this << pb);
    NS_LOG_LOGIC(this << " state: " << m_state);

    m_phyTxStartTrace(pb);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        m_txPacketBurst = pb;

        std::cout<<"hi1"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the ctrlMsgList parameter of
        // LteSpectrumSignalParametersDataFrame
        ChangeState(TX_DATA);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersDataFrame> txParams =
            Create<LteSpectrumSignalParametersDataFrame>();
        txParams->duration = duration;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->packetBurst = pb;
        txParams->ctrlMsgList = ctrlMsgList;
        txParams->cellId = m_cellId;
        if (pb)
        {
            m_ulDataSlCheck = true;
        }
        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(duration, &LteSpectrumPhy::EndTxData, this);
    }
        return false;
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
        break;
    }
}

bool
LteSpectrumPhy::StartTxSlMibFrame(Ptr<PacketBurst> pb, Time duration)
{
    NS_LOG_FUNCTION(this << pb << " ID:" << GetDevice()->GetNode()->GetId()
                         << " State: " << m_state);

    m_phyTxStartTrace(pb);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        m_txPacketBurst = pb;

        // std::cout<<"hi1"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the ctrlMsgList parameter of
        // LteSpectrumSignalParametersDataFrame
        ChangeState(TX_DATA);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersSlMibFrame> txParams =
            Create<LteSpectrumSignalParametersSlMibFrame>();
        txParams->duration = duration;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->nodeId = GetDevice()->GetNode()->GetId();
        txParams->slssId = m_slssId;
        txParams->packetBurst = pb;
        m_ulDataSlCheck = true;

        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(duration, &LteSpectrumPhy::EndTxData, this);
        std::vector<int> rbMap;
                    int rbI = 0;
                    for (auto it = txParams->psd->ConstValuesBegin();
                         it != txParams->psd->ConstValuesEnd();
                         it++, rbI++)
                    {
                        if (*it != 0)
                        {
                            NS_LOG_INFO("Sidelink message arriving on RB " << rbI);
                            {
                                // std::cout<<rbI<<std::endl;
                            }
                            rbMap.push_back(rbI);
                        }
                    }
    }
        return false;
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
        break;
    }
}

bool
LteSpectrumPhy::StartTxSlCtrlFrame(Ptr<PacketBurst> pb, Time duration)
{
    NS_LOG_FUNCTION(this << pb << " ID:" << GetDevice()->GetNode()->GetId()
                         << " State: " << m_state);

    m_phyTxStartTrace(pb);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        m_txPacketBurst = pb;

        // std::cout<<"hi2"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the ctrlMsgList parameter of
        // LteSpectrumSignalParametersDataFrame
        ChangeState(TX_DATA);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersSlCtrlFrame> txParams =
            Create<LteSpectrumSignalParametersSlCtrlFrame>();
        txParams->duration = duration;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->nodeId = GetDevice()->GetNode()->GetId();
        txParams->slssId = m_slssId;
        txParams->packetBurst = pb;
        m_ulDataSlCheck = true;

        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(duration, &LteSpectrumPhy::EndTxData, this);
        // std::cout<<"HI"<<std::endl;
        std::vector<int> rbMap;
                    int rbI = 0;
                    for (auto it = txParams->psd->ConstValuesBegin();
                         it != txParams->psd->ConstValuesEnd();
                         it++, rbI++)
                    {
                        if (*it != 0)
                        {
                            NS_LOG_INFO("Sidelink message arriving on RB " << rbI);
                            {
                                // std::cout<<rbI<<std::endl;
                            }
                            rbMap.push_back(rbI);
                        }
                    }
    }
        return false;
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
        break;
    }
}

bool
LteSpectrumPhy::StartTxSlDataFrame(Ptr<PacketBurst> pb, Time duration, uint8_t groupId)
{
    NS_LOG_FUNCTION(this << pb << " ID:" << GetDevice()->GetNode()->GetId()
                         << " State: " << m_state);

    m_phyTxStartTrace(pb);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        m_txPacketBurst = pb;

        // std::cout<<"hi3"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the ctrlMsgList parameter of
        // LteSpectrumSignalParametersDataFrame
        ChangeState(TX_DATA);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersSlDataFrame> txParams =
            Create<LteSpectrumSignalParametersSlDataFrame>();
        txParams->duration = duration;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->nodeId = GetDevice()->GetNode()->GetId();
        txParams->groupId = groupId;
        txParams->slssId = m_slssId;
        txParams->packetBurst = pb;
        m_ulDataSlCheck = true;

        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(duration, &LteSpectrumPhy::EndTxData, this);

        // std::cout<<"hi"<<std::endl;
        std::vector<int> rbMap;
                    int rbI = 0;
                    for (auto it = txParams->psd->ConstValuesBegin();
                         it != txParams->psd->ConstValuesEnd();
                         it++, rbI++)
                    {
                        if (*it != 0)
                        {
                            NS_LOG_INFO("Sidelink message arriving on RB " << rbI);
                            {
                                // std::cout<<rbI<<std::endl;
                            }
                            rbMap.push_back(rbI);
                        }
                    }

        m_rxControlMessageList = txParams->ctrlMsgList;

                  
                  /*********************************MODIFIED************************************/


                  for(auto it=m_rxControlMessageList.begin(); it!=m_rxControlMessageList.end(); it++){
                    Ptr<LteControlMessage> msg = (*it);

                    if (msg->GetMessageType () == LteControlMessage::DL_DCI)
                    {
                      Ptr<DlDciLteControlMessage> msg2 = DynamicCast<DlDciLteControlMessage> (msg);

                      DlDciListElement_s dci = msg2->GetDci ();
                    
                      //LteRadioBearerTag tag;
                      //msg->PeekPacketTag (tag);
                      // u_int16_t m_rnti = tag.GetRnti();
                      // std::cout<<"\nSpectrum RNTI: "<<m_rnti;
                      // if (dci.m_rnti != m_rnti)
                      // {
                      //   // DCI not for me
                      //   continue;
                      // }
                    
                  

                      std::cout<<"\nRB bitmap for UE with node ID "<<GetDevice()->GetNode()->GetId()<<"is: "<< dci.m_rbBitmap;
                    }

                  }
    }
        return false;
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
        break;
    }
}

bool
LteSpectrumPhy::StartTxSlDiscFrame(Ptr<PacketBurst> pb, uint32_t resNo, uint8_t rv, Time duration)
{
    NS_LOG_FUNCTION(this << pb << " ID:" << GetDevice()->GetNode()->GetId()
                         << " State: " << m_state);

    m_phyTxStartTrace(pb);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        m_txPacketBurst = pb;

        // std::cout<<"hi4"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the ctrlMsgList parameter of
        // LteSpectrumSignalParametersDataFrame
        ChangeState(TX_DATA);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersSlDiscFrame> txParams =
            Create<LteSpectrumSignalParametersSlDiscFrame>();
        txParams->duration = duration;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->nodeId = GetDevice()->GetNode()->GetId();
        txParams->slssId = m_slssId;
        txParams->packetBurst = pb;
        txParams->resNo = resNo;
        txParams->rv = rv;
        m_ulDataSlCheck = true;

        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(duration, &LteSpectrumPhy::EndTxData, this);
        std::vector<int> rbMap;
                    int rbI = 0;
                    for (auto it = txParams->psd->ConstValuesBegin();
                         it != txParams->psd->ConstValuesEnd();
                         it++, rbI++)
                    {
                        if (*it != 0)
                        {
                            NS_LOG_INFO("Sidelink message arriving on RB " << rbI);
                            {
                                // std::cout<<rbI<<std::endl;
                            }
                            rbMap.push_back(rbI);
                        }
                    }
    }
        return false;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
    }
}

bool
LteSpectrumPhy::StartTxDlCtrlFrame(std::list<Ptr<LteControlMessage>> ctrlMsgList, bool pss)
{
    NS_LOG_FUNCTION(this << " PSS " << (uint16_t)pss);
    NS_LOG_LOGIC(this << " state: " << m_state);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the cellId parameter of
        // LteSpectrumSignalParametersDlCtrlFrame
        ChangeState(TX_DL_CTRL);
        NS_ASSERT(m_channel);

        // std::cout<<"hi5"<<std::endl;

        Ptr<LteSpectrumSignalParametersDlCtrlFrame> txParams =
            Create<LteSpectrumSignalParametersDlCtrlFrame>();
        txParams->duration = DL_CTRL_DURATION;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->cellId = m_cellId;
        txParams->pss = pss;
        txParams->ctrlMsgList = ctrlMsgList;
        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(DL_CTRL_DURATION, &LteSpectrumPhy::EndTxDlCtrl, this);
    }
        return false;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
    }
}

bool
LteSpectrumPhy::StartTxUlSrsFrame()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    switch (m_state)
    {
    case RX_DATA:
    case RX_DL_CTRL:
    case RX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while RX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case TX_DL_CTRL:
    case TX_DATA:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot TX while already TX: the MAC should avoid this");
        break;

    case IDLE: {
        /*
        m_txPsd must be set by the device, according to
        (i) the available subchannel for transmission
        (ii) the power transmission
        */
        NS_ASSERT(m_txPsd);
        NS_LOG_LOGIC(this << " m_txPsd: " << *m_txPsd);

        // std::cout<<"hi6"<<std::endl;

        // we need to convey some PHY meta information to the receiver
        // to be used for simulation purposes (e.g., the CellId). This
        // is done by setting the cellId parameter of
        // LteSpectrumSignalParametersDlCtrlFrame
        ChangeState(TX_UL_SRS);
        NS_ASSERT(m_channel);
        Ptr<LteSpectrumSignalParametersUlSrsFrame> txParams =
            Create<LteSpectrumSignalParametersUlSrsFrame>();
        txParams->duration = UL_SRS_DURATION;
        txParams->txPhy = GetObject<SpectrumPhy>();
        txParams->txAntenna = m_antenna;
        txParams->psd = m_txPsd;
        txParams->cellId = m_cellId;
        m_channel->StartTx(txParams);
        m_endTxEvent = Simulator::Schedule(UL_SRS_DURATION, &LteSpectrumPhy::EndTxUlSrs, this);
    }
        return false;

    default:
        NS_FATAL_ERROR("unknown state");
        return true;
    }
}

void
LteSpectrumPhy::EndTxData()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    NS_ASSERT(m_state == TX_DATA);
    m_phyTxEndTrace(m_txPacketBurst);
    m_txPacketBurst = nullptr;
    ChangeState(IDLE);
}

void
LteSpectrumPhy::EndTxDlCtrl()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    NS_ASSERT(m_state == TX_DL_CTRL);
    NS_ASSERT(!m_txPacketBurst);
    ChangeState(IDLE);
}

void
LteSpectrumPhy::EndTxUlSrs()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    NS_ASSERT(m_state == TX_UL_SRS);
    NS_ASSERT(!m_txPacketBurst);
    ChangeState(IDLE);
}

void
LteSpectrumPhy::StartRx(Ptr<SpectrumSignalParameters> spectrumRxParams)
{
    NS_LOG_FUNCTION(this << spectrumRxParams);
    NS_LOG_LOGIC(this << " state: " << m_state);

    // std::cout<<"rec2"<<std::endl;

    Ptr<const SpectrumValue> rxPsd = spectrumRxParams->psd;
    Time duration = spectrumRxParams->duration;

    // the device might start RX only if the signal is of a type
    // understood by this device - in this case, an LTE signal.
    Ptr<LteSpectrumSignalParametersDataFrame> lteDataRxParams =
        DynamicCast<LteSpectrumSignalParametersDataFrame>(spectrumRxParams);
    Ptr<LteSpectrumSignalParametersDlCtrlFrame> lteDlCtrlRxParams =
        DynamicCast<LteSpectrumSignalParametersDlCtrlFrame>(spectrumRxParams);
    Ptr<LteSpectrumSignalParametersUlSrsFrame> lteUlSrsRxParams =
        DynamicCast<LteSpectrumSignalParametersUlSrsFrame>(spectrumRxParams);
    Ptr<LteSpectrumSignalParametersSlFrame> lteSlRxParams =
        DynamicCast<LteSpectrumSignalParametersSlFrame>(spectrumRxParams);
    if (lteDataRxParams)
    {
        m_interferenceData->AddSignal(rxPsd, duration);
        StartRxData(lteDataRxParams);
    }
    else if (lteDlCtrlRxParams)
    {
        m_interferenceCtrl->AddSignal(rxPsd, duration);
        StartRxDlCtrl(lteDlCtrlRxParams);
    }
    else if (lteUlSrsRxParams)
    {
        m_interferenceCtrl->AddSignal(rxPsd, duration);
        StartRxUlSrs(lteUlSrsRxParams);
    }
    else if (lteSlRxParams != nullptr)
    {
        m_interferenceSl->AddSignal(rxPsd, duration);
        m_interferenceData->AddSignal(rxPsd, duration); // to compute UL/SL interference
        m_slStartRx(m_halfDuplexPhy);
        if (m_ctrlFullDuplexEnabled && !lteSlRxParams->ctrlMsgList.empty())
        {
            StartRxSlFrame(lteSlRxParams);
        }
        else if (m_halfDuplexPhy != nullptr)
        {
            if (m_halfDuplexPhy->GetState() == IDLE || !(m_halfDuplexPhy->m_ulDataSlCheck))
            {
                NS_LOG_DEBUG(this << " Received Sidelink Data " << m_halfDuplexPhy);
                StartRxSlFrame(lteSlRxParams);
            }
        }
    }
    else
    {
        // other type of signal (could be 3G, GSM, whatever) -> interference
        m_interferenceData->AddSignal(rxPsd, duration);
        m_interferenceCtrl->AddSignal(rxPsd, duration);
    }
}

void
LteSpectrumPhy::StartRxData(Ptr<LteSpectrumSignalParametersDataFrame> params)
{
    NS_LOG_FUNCTION(this);
    switch (m_state)
    {
    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot RX while TX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;
    case RX_DL_CTRL:
        NS_FATAL_ERROR("cannot RX Data while receiving control");
        break;
    case IDLE:
    case RX_DATA:

    // std::cout<<"rec3"<<std::endl;
        // the behavior is similar when
        // we're IDLE or RX because we can receive more signals
        // simultaneously (e.g., at the eNB).
        {
            // To check if we're synchronized to this signal, we check
            // for the CellId which is reported in the
            //  LteSpectrumSignalParametersDataFrame
            if (params->cellId == m_cellId)
            {
                NS_LOG_LOGIC(this << " synchronized with this signal (cellId=" << params->cellId
                                  << ")");
                if ((m_rxPacketBurstList.empty()) && (m_rxControlMessageList.empty()))
                {
                    NS_ASSERT(m_state == IDLE);
                    // first transmission, i.e., we're IDLE and we
                    // start RX
                    m_firstRxStart = Simulator::Now();
                    m_firstRxDuration = params->duration;
                    NS_LOG_LOGIC(this << " scheduling EndRx with delay "
                                      << params->duration.As(Time::S));
                    m_endRxDataEvent =
                        Simulator::Schedule(params->duration, &LteSpectrumPhy::EndRxData, this);
                }
                else
                {
                    NS_ASSERT(m_state == RX_DATA);
                    // sanity check: if there are multiple RX events, they
                    // should occur at the same time and have the same
                    // duration, otherwise the interference calculation
                    // won't be correct
                    NS_ASSERT((m_firstRxStart == Simulator::Now()) &&
                              (m_firstRxDuration == params->duration));
                }

                ChangeState(RX_DATA);
                if (params->packetBurst)
                {
                    m_rxPacketBurstList.push_back(params->packetBurst);
                    m_interferenceData->StartRx(params->psd);

                    m_phyRxStartTrace(params->packetBurst);
                }
                NS_LOG_DEBUG(this << " insert msgs " << params->ctrlMsgList.size());
                m_rxControlMessageList.insert(m_rxControlMessageList.end(),
                                              params->ctrlMsgList.begin(),
                                              params->ctrlMsgList.end());

                NS_LOG_LOGIC(this << " numSimultaneousRxEvents = " << m_rxPacketBurstList.size());
            }
            else
            {
                NS_LOG_LOGIC(this << " not in sync with this signal (cellId=" << params->cellId
                                  << ", m_cellId=" << m_cellId << ")");
            }
        }
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        break;
    }

    NS_LOG_LOGIC(this << " state: " << m_state);
}

void
LteSpectrumPhy::StartRxSlFrame(Ptr<LteSpectrumSignalParametersSlFrame> params)
{
    NS_LOG_LOGIC(this << " Cell ID: " << m_cellId << " Node ID: " << GetDevice()->GetNode()->GetId()
                      << " State: " << m_state);

    switch (m_state)
    {
    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot RX while TX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;
    case RX_DL_CTRL:
        NS_FATAL_ERROR("cannot RX Data while receiving control");
        break;
    case IDLE:
    case RX_DATA:
        // the behavior is similar when
        // we're IDLE or RX because we can receive more signals
        // simultaneously (e.g., at the eNB).
        {
            // std::cout<<"rec1"<<std::endl;
            // check it is not an eNB and not the same sending node (Sidelink : discovery &
            // communication )
            if (m_cellId == 0 && params->nodeId != GetDevice()->GetNode()->GetId())
            {
                NS_LOG_LOGIC("The signal is neither from eNodeB nor from this UE");
                NS_LOG_DEBUG("Signal is from Node id = " << params->nodeId);

                // std::cout<<params->nodeId<<std::endl;

                // SLSSs (PSBCH) should be received by all UEs
                // Checking if it is a SLSS, and if it is: measure S-RSRP and receive MIB-SL

                // Receive PSCCH, PSSCH and PSDCH only if synchronized to the transmitter (having
                // the same SLSSID) and belonging to the destination group
                Ptr<LteSpectrumSignalParametersSlMibFrame> lteSlMibRxParams =
                    DynamicCast<LteSpectrumSignalParametersSlMibFrame>(params);

                if (lteSlMibRxParams != nullptr || params->slssId == m_slssId /*&& (params->groupId == 0 || m_l1GroupIds.find (params->groupId) != m_l1GroupIds.end())*/)
                {
                    if (m_rxPacketInfo.empty())
                    {
                        NS_ASSERT(m_state == IDLE);
                        // first transmission, i.e., we're IDLE and we start RX
                        m_firstRxStart = Simulator::Now();
                        m_firstRxDuration = params->duration;
                        NS_LOG_LOGIC("Scheduling EndRxSl with delay "
                                     << params->duration.GetSeconds() << "s");
                        m_endRxDataEvent = Simulator::Schedule(params->duration,
                                                               &LteSpectrumPhy::EndRxSlFrame,
                                                               this);
                    }
                    else
                    {
                        NS_ASSERT(m_state == RX_DATA);
                        // sanity check: if there are multiple RX events, they
                        // should occur at the same time and have the same
                        // duration, otherwise the interference calculation
                        // won't be correct
                        NS_ASSERT((m_firstRxStart == Simulator::Now()) &&
                                  (m_firstRxDuration == params->duration));
                    }
                    ChangeState(RX_DATA);
                    m_interferenceSl->StartRx(params->psd);

                    std::vector<int> rbMap;
                    int rbI = 0;
                    for (auto it = params->psd->ConstValuesBegin();
                         it != params->psd->ConstValuesEnd();
                         it++, rbI++)
                    {
                        if (*it != 0)
                        {
                            NS_LOG_INFO("Sidelink message arriving on RB " << rbI);
                            {
                                // std::cout<<rbI<<std::endl;
                            }
                            rbMap.push_back(rbI);
                        }
                    }

                    if(ack.size()==100){
                        if(ack.front()==1)rec--;
                        ack.pop();
                    }
                    ack.push(1);
                    rec++;

                    SlRxPacketInfo_t packetInfo;
                    packetInfo.params = params;
                    packetInfo.rbBitmap = rbMap;

                    // for(auto i:rbMap){
                    //     std::cout<<i<<" ";
                    // }

                    // auto currid = GetDevice()->GetNode()->GetId();

                    // if(currid==5 || currid==3 || currid==4){
                    //     std::cout<<Simulator::Now()<<" Node"<<currid<<": ";
                    //     for(int i=0;i<50;i++)std::cout<<SINRperRBtable[currid][i]<<" ";
                    //     std::cout<<std::endl;
                    // }

                    // std::cout<<std::endl;
                    m_rxPacketInfo.push_back(packetInfo);
                    if (params->packetBurst)
                    {
                        m_phyRxStartTrace(params->packetBurst);
                        NS_LOG_DEBUG("RX Burst containing " << params->packetBurst->GetNPackets()
                                                            << " packets");
                    }
                    NS_LOG_DEBUG("Insert Sidelink ctrl msgs " << params->ctrlMsgList.size());
                    NS_LOG_LOGIC("numSimultaneousRxEvents = " << m_rxPacketInfo.size());
                }
                else
                {
                    NS_LOG_LOGIC("Not in sync with this Sidelink signal (Tx slssId="
                                 << params->slssId << ", Rx slssId=" << m_slssId << ")...Ignoring");
                }
            }
            else
            {
                NS_LOG_LOGIC(
                    this << " the signal is from eNodeB or from this UE... Ignoring. Cell id "
                         << m_cellId);
                NS_LOG_DEBUG(this << " Node Id from signal " << params->nodeId
                                  << " My node ID = " << GetDevice()->GetNode()->GetId());
            }
        }
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        break;
    }
    NS_LOG_LOGIC(" Exiting StartRxSlFrame. State: " << m_state);
}

void
LteSpectrumPhy::StartRxDlCtrl(Ptr<LteSpectrumSignalParametersDlCtrlFrame> lteDlCtrlRxParams)
{
    NS_LOG_FUNCTION(this);

    // To check if we're synchronized to this signal, we check
    // for the CellId which is reported in the
    // LteSpectrumSignalParametersDlCtrlFrame
    uint16_t cellId;
    NS_ASSERT(lteDlCtrlRxParams);
    cellId = lteDlCtrlRxParams->cellId;

    switch (m_state)
    {
    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
    case RX_DATA:
    case RX_UL_SRS:
        NS_FATAL_ERROR("unexpected event in state " << m_state);
        break;

    case RX_DL_CTRL:
    case IDLE:

        // common code for the two states
        // check presence of PSS for UE measuerements
        if (lteDlCtrlRxParams->pss)
        {
            if (!m_ltePhyRxPssCallback.IsNull())
            {
                m_ltePhyRxPssCallback(cellId, lteDlCtrlRxParams->psd);
            }
        }

        // differentiated code for the two states
        switch (m_state)
        {
        case RX_DL_CTRL:
            NS_ASSERT_MSG(m_cellId != cellId, "any other DlCtrl should be from a different cell");
            NS_LOG_LOGIC(this << " ignoring other DlCtrl (cellId=" << cellId
                              << ", m_cellId=" << m_cellId << ")");
            break;

        case IDLE:
            if (cellId == m_cellId)
            {
                NS_LOG_LOGIC(this << " synchronized with this signal (cellId=" << cellId << ")");

                NS_ASSERT(m_rxControlMessageList.empty());
                m_firstRxStart = Simulator::Now();
                m_firstRxDuration = lteDlCtrlRxParams->duration;
                NS_LOG_LOGIC(this << " scheduling EndRx with delay "
                                  << lteDlCtrlRxParams->duration);

                // store the DCIs
                m_rxControlMessageList = lteDlCtrlRxParams->ctrlMsgList;
                m_endRxDlCtrlEvent = Simulator::Schedule(lteDlCtrlRxParams->duration,
                                                         &LteSpectrumPhy::EndRxDlCtrl,
                                                         this);
                ChangeState(RX_DL_CTRL);
                m_interferenceCtrl->StartRx(lteDlCtrlRxParams->psd);
            }
            else
            {
                NS_LOG_LOGIC(this << " not synchronizing with this signal (cellId=" << cellId
                                  << ", m_cellId=" << m_cellId << ")");
            }
            break;

        default:
            NS_FATAL_ERROR("unexpected event in state " << m_state);
            break;
        }
        break; // case RX_DL_CTRL or IDLE

    default:
        NS_FATAL_ERROR("unknown state");
        break;
    }

    NS_LOG_LOGIC(this << " state: " << m_state);
}

void
LteSpectrumPhy::StartRxUlSrs(Ptr<LteSpectrumSignalParametersUlSrsFrame> lteUlSrsRxParams)
{
    NS_LOG_FUNCTION(this);
    switch (m_state)
    {
    case TX_DATA:
    case TX_DL_CTRL:
    case TX_UL_SRS:
        NS_FATAL_ERROR("cannot RX while TX: according to FDD channel access, the physical layer "
                       "for transmission cannot be used for reception");
        break;

    case RX_DATA:
    case RX_DL_CTRL:
        NS_FATAL_ERROR("cannot RX SRS while receiving something else");
        break;

    case IDLE:
    case RX_UL_SRS:
        // the behavior is similar when
        // we're IDLE or RX_UL_SRS because we can receive more signals
        // simultaneously at the eNB
        {
            // To check if we're synchronized to this signal, we check
            // for the CellId which is reported in the
            // LteSpectrumSignalParametersDlCtrlFrame
            uint16_t cellId;
            cellId = lteUlSrsRxParams->cellId;
            if (cellId == m_cellId)
            {
                NS_LOG_LOGIC(this << " synchronized with this signal (cellId=" << cellId << ")");
                if (m_state == IDLE)
                {
                    // first transmission, i.e., we're IDLE and we
                    // start RX
                    NS_ASSERT(m_rxControlMessageList.empty());
                    m_firstRxStart = Simulator::Now();
                    m_firstRxDuration = lteUlSrsRxParams->duration;
                    NS_LOG_LOGIC(this << " scheduling EndRx with delay "
                                      << lteUlSrsRxParams->duration);

                    m_endRxUlSrsEvent = Simulator::Schedule(lteUlSrsRxParams->duration,
                                                            &LteSpectrumPhy::EndRxUlSrs,
                                                            this);
                }
                else if (m_state == RX_UL_SRS)
                {
                    // sanity check: if there are multiple RX events, they
                    // should occur at the same time and have the same
                    // duration, otherwise the interference calculation
                    // won't be correct
                    NS_ASSERT((m_firstRxStart == Simulator::Now()) &&
                              (m_firstRxDuration == lteUlSrsRxParams->duration));
                }
                ChangeState(RX_UL_SRS);
                m_interferenceCtrl->StartRx(lteUlSrsRxParams->psd);
            }
            else
            {
                NS_LOG_LOGIC(this << " not in sync with this signal (cellId=" << cellId
                                  << ", m_cellId=" << m_cellId << ")");
            }
        }
        break;

    default:
        NS_FATAL_ERROR("unknown state");
        break;
    }

    NS_LOG_LOGIC(this << " state: " << m_state);
}

void
LteSpectrumPhy::UpdateSinrPerceived(const SpectrumValue& sinr)
{
    NS_LOG_FUNCTION(this << sinr);
    m_sinrPerceived = sinr;
}


void
LteSpectrumPhy::UpdateSlSinrPerceived (std::vector<SpectrumValue> sinr)
{
  NS_LOG_FUNCTION (this);
  
  uint32_t index = GetDevice ()->GetNode ()->GetId ();
  for (uint32_t i = 0; i < sinr.size (); i++)
    {
      SpectrumValue temp = sinr[i];

      for (uint32_t j = 0; j < temp.GetValuesN (); j++) 
        {
          if (temp.ValuesAt (j) != 0)
            {
              SINRperRBtable[index][j]=temp.ValuesAt (j);
              SINRperCF[index/2]=temp.ValuesAt (j);
            }
        }
      
    }
    
  for (uint32_t i = 0; i < SINRperRBtable.size (); i++)
    {
      double count_of_busy_rb = 0;

      for (uint32_t j = 0; j < SINRperRBtable[i].size(); j++)
        {
          if(SINRperRBtable[i][j]<=4)count_of_busy_rb++;
          
        }
        ContentionRatio[i] = count_of_busy_rb/double(SINRperRBtable[i].size());
        // std::cout<<ContentionRatio[i]<<" ";
      
    }

    // for (uint32_t i = 0; i < SINRperRBtable.size (); i++)
    // {
    //     std::cout<<"node "<<i<<" ////////"<<std::endl;
    //   for (uint32_t j = 0; j < SINRperRBtable[0].size(); j++)
    //     {
    //       std::cout<<SINRperRBtable[i][j]<<" ";
    //     }
    //     std::cout<<std::endl;
      
    // }
  // for (uint32_t i = 0; i < SINRperCF.size (); i++)
  //   {  
  //     std::cout<<SINRperCF[i]<<" "; 
  //   }
//   std::cout<<std::endl;
    
  m_slSinrPerceived = sinr;
}

bool isChannelIdle(uint16_t node){
    int freeRb = 0;
    for(uint32_t i=0;i<SINRperRBtable[node].size();i++){
        if(SINRperRBtable[node][i]==0)freeRb++;
    }
    return freeRb > 5;
}

// void
// LteSpectrumPhy::UpdateSlSinrPerceived(std::vector<SpectrumValue> sinr)
// {
//     NS_LOG_FUNCTION(this);
//     m_slSinrPerceived = sinr;
// }

void
LteSpectrumPhy::UpdateSlSigPerceived(std::vector<SpectrumValue> signal)
{
    NS_LOG_FUNCTION(this);
    m_slSignalPerceived = signal;
}

void
LteSpectrumPhy::UpdateSlIntPerceived(std::vector<SpectrumValue> interference)
{
    NS_LOG_FUNCTION(this);
    m_slInterferencePerceived = interference;
}

void
LteSpectrumPhy::AddExpectedTb(uint16_t rnti,
                              uint8_t ndi,
                              uint16_t size,
                              uint8_t mcs,
                              std::vector<int> map,
                              uint8_t layer,
                              uint8_t harqId,
                              uint8_t rv,
                              bool downlink)
{
    NS_LOG_FUNCTION(this << " rnti: " << rnti << " NDI " << (uint16_t)ndi << " size " << size
                         << " mcs " << (uint16_t)mcs << " layer " << (uint16_t)layer << " rv "
                         << (uint16_t)rv);
    TbId_t tbId;
    tbId.m_rnti = rnti;
    tbId.m_layer = layer;
    auto it = m_expectedTbs.find(tbId);
    if (it != m_expectedTbs.end())
    {
        // might be a TB of an unreceived packet (due to high propagation losses)
        m_expectedTbs.erase(it);
    }
    // insert new entry
    tbInfo_t tbInfo = {ndi, size, mcs, map, harqId, rv, 0.0, downlink, false, false};
    m_expectedTbs.insert(std::pair<TbId_t, tbInfo_t>(tbId, tbInfo));
}

void
LteSpectrumPhy::AddExpectedTb(uint16_t rnti,
                              uint8_t l1dst,
                              uint8_t ndi,
                              uint16_t size,
                              uint8_t mcs,
                              std::vector<int> map,
                              uint8_t rv)
{
    NS_LOG_FUNCTION(this << " RNTI: " << rnti << " Group " << (uint16_t)l1dst << " NDI "
                         << (uint16_t)ndi << " Size " << size << " MCS " << (uint16_t)mcs << " RV "
                         << (uint16_t)rv);
    SlTbId_t tbId;
    tbId.m_rnti = rnti;
    tbId.m_l1dst = l1dst;
    expectedSlTbs_t::iterator it;
    it = m_expectedSlTbs.find(tbId);
    if (it != m_expectedSlTbs.end())
    {
        // might be a TB of an unreceived packet (due to high path loss)
        m_expectedSlTbs.erase(it);
    }
    // insert new entry
    SltbInfo_t tbInfo = {ndi, size, mcs, map, rv, 0.0, false, false};
    m_expectedSlTbs.insert(std::pair<SlTbId_t, SltbInfo_t>(tbId, tbInfo));

    // if it is for new data, reset the HARQ process
    if (ndi)
    {
        m_slHarqPhyModule->ResetSlHarqProcessStatus(rnti, l1dst);
        m_slHarqPhyModule->ResetPrevDecoded(rnti, l1dst);
        m_slHarqPhyModule->ResetTbIdx(rnti, l1dst);
    }
}

void
LteSpectrumPhy::AddExpectedTb(uint16_t rnti,
                              uint8_t resPsdch,
                              uint8_t ndi,
                              std::vector<int> map,
                              uint8_t rv,
                              int index)
{
    NS_LOG_FUNCTION(this << " RNTI: " << rnti << " resPsdch " << resPsdch << " NDI "
                         << (uint16_t)ndi << " RV " << (uint16_t)rv);

    SlDiscTbId_t tbId;
    tbId.m_rnti = rnti;
    tbId.m_resPsdch = resPsdch;
    expectedDiscTbs_t::iterator it;
    it = m_expectedDiscTbs.find(tbId);
    if (it != m_expectedDiscTbs.end())
    {
        // might be a TB of an unreceived packet (due to high path loss)
        m_expectedDiscTbs.erase(it);
    }
    // insert new entry.
    // SINR value of -100 is used just to initialize the sinr member of SlDisctbInfo_t.
    // It is updated when the SINR is computed for the received discovery TB.
    SlDisctbInfo_t tbInfo = {ndi, resPsdch, map, rv, 0.0, false, false, -100, index};

    m_expectedDiscTbs.insert(std::pair<SlDiscTbId_t, SlDisctbInfo_t>(tbId, tbInfo));

    // if it is for new data, reset the HARQ process
    if (ndi)
    {
        m_slHarqPhyModule->ResetDiscHarqProcessStatus(rnti, resPsdch);
        m_slHarqPhyModule->ResetDiscTbPrevDecoded(rnti, resPsdch);
    }
}

void
LteSpectrumPhy::RemoveExpectedTb(uint16_t rnti)
{
    NS_LOG_FUNCTION(this << rnti);
    TbId_t tbId;
    tbId.m_rnti = rnti;
    // Remove TB of both the layers
    for (uint8_t i = 0; i < 2; i++)
    {
        tbId.m_layer = i;
        auto it = m_expectedTbs.find(tbId);
        if (it != m_expectedTbs.end())
        {
            m_expectedTbs.erase(it);
        }
    }
}

void
LteSpectrumPhy::EndRxData()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    NS_ASSERT(m_state == RX_DATA);

    // this will trigger CQI calculation and Error Model evaluation
    // as a side effect, the error model should update the error status of all TBs
    m_interferenceData->EndRx();
    NS_LOG_DEBUG(this << " No. of burts " << m_rxPacketBurstList.size());
    NS_LOG_DEBUG(this << " Expected TBs " << m_expectedTbs.size());
    auto itTb = m_expectedTbs.begin();

    // apply transmission mode gain
    NS_LOG_DEBUG(this << " txMode " << (uint16_t)m_transmissionMode << " gain "
                      << m_txModeGain.at(m_transmissionMode));
    NS_ASSERT(m_transmissionMode < m_txModeGain.size());
    m_sinrPerceived *= m_txModeGain.at(m_transmissionMode);

    while (itTb != m_expectedTbs.end())
    {
        if (m_dataErrorModelEnabled &&
            !m_rxPacketBurstList
                 .empty()) // avoid to check for errors when there is no actual data transmitted
        {
            // retrieve HARQ info
            HarqProcessInfoList_t harqInfoList;
            if ((*itTb).second.ndi == 0)
            {
                // TB retxed: retrieve HARQ history
                uint16_t ulHarqId = 0;
                if ((*itTb).second.downlink)
                {
                    harqInfoList =
                        m_harqPhyModule->GetHarqProcessInfoDl((*itTb).second.harqProcessId,
                                                              (*itTb).first.m_layer);
                }
                else
                {
                    harqInfoList =
                        m_harqPhyModule->GetHarqProcessInfoUl((*itTb).first.m_rnti, ulHarqId);
                }
            }
            TbStats_t tbStats = LteMiErrorModel::GetTbDecodificationStats(m_sinrPerceived,
                                                                          (*itTb).second.rbBitmap,
                                                                          (*itTb).second.size,
                                                                          (*itTb).second.mcs,
                                                                          harqInfoList);
            (*itTb).second.mi = tbStats.mi;
            (*itTb).second.corrupt = !(m_random->GetValue() > tbStats.tbler);
            NS_LOG_DEBUG(this << "RNTI " << (*itTb).first.m_rnti << " size " << (*itTb).second.size
                              << " mcs " << (uint32_t)(*itTb).second.mcs << " bitmap "
                              << (*itTb).second.rbBitmap.size() << " layer "
                              << (uint16_t)(*itTb).first.m_layer << " TBLER " << tbStats.tbler
                              << " corrupted " << (*itTb).second.corrupt);
            // fire traces on DL/UL reception PHY stats
            PhyReceptionStatParameters params;
            params.m_timestamp = Simulator::Now().GetMilliSeconds();
            params.m_cellId = m_cellId;
            params.m_imsi = 0; // it will be set by DlPhyTransmissionCallback in LteHelper
            params.m_rnti = (*itTb).first.m_rnti;
            params.m_txMode = m_transmissionMode;
            params.m_layer = (*itTb).first.m_layer;
            params.m_mcs = (*itTb).second.mcs;
            params.m_size = (*itTb).second.size;
            params.m_rv = (*itTb).second.rv;
            params.m_ndi = (*itTb).second.ndi;
            params.m_correctness = (uint8_t) !(*itTb).second.corrupt;
            params.m_ccId = m_componentCarrierId;
            SpectrumValue sinrCopy = m_sinrPerceived;
            std::vector<int> map = (*itTb).second.rbBitmap;
            double sum = 0.0;
            for (uint32_t i = 0; i < map.size(); ++i)
            {
                double sinrLin = sinrCopy[map.at(i)];
                sum = sum + sinrLin;
            }
            params.m_sinrPerRb = sum / map.size();

            if ((*itTb).second.downlink)
            {
                // DL
                m_dlPhyReception(params);
            }
            else
            {
                // UL
                params.m_rv = harqInfoList.size();
                m_ulPhyReception(params);
            }
        }

        itTb++;
    }
    std::map<uint16_t, DlInfoListElement_s> harqDlInfoMap;
    for (auto i = m_rxPacketBurstList.begin(); i != m_rxPacketBurstList.end(); ++i)
    {
        for (auto j = (*i)->Begin(); j != (*i)->End(); ++j)
        {
            // retrieve TB info of this packet
            LteRadioBearerTag tag;
            (*j)->PeekPacketTag(tag);
            TbId_t tbId;
            tbId.m_rnti = tag.GetRnti();
            tbId.m_layer = tag.GetLayer();
            itTb = m_expectedTbs.find(tbId);
            NS_LOG_INFO(this << " Packet of " << tbId.m_rnti << " layer "
                             << (uint16_t)tag.GetLayer());
            if (itTb != m_expectedTbs.end())
            {
                if (!(*itTb).second.corrupt)
                {
                    m_phyRxEndOkTrace(*j);

                    if (!m_ltePhyRxDataEndOkCallback.IsNull())
                    {
                        m_ltePhyRxDataEndOkCallback(*j);
                    }
                }
                else
                {
                    // TB received with errors
                    m_phyRxEndErrorTrace(*j);
                }

                // send HARQ feedback (if not already done for this TB)
                if (!(*itTb).second.harqFeedbackSent)
                {
                    (*itTb).second.harqFeedbackSent = true;
                    if (!(*itTb).second.downlink)
                    {
                        UlInfoListElement_s harqUlInfo;
                        harqUlInfo.m_rnti = tbId.m_rnti;
                        harqUlInfo.m_tpc = 0;
                        if ((*itTb).second.corrupt)
                        {
                            harqUlInfo.m_receptionStatus = UlInfoListElement_s::NotOk;
                            NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " send UL-HARQ-NACK");
                            m_harqPhyModule->UpdateUlHarqProcessStatus(
                                tbId.m_rnti,
                                (*itTb).second.mi,
                                (*itTb).second.size,
                                (*itTb).second.size / EffectiveCodingRate[(*itTb).second.mcs]);
                        }
                        else
                        {
                            harqUlInfo.m_receptionStatus = UlInfoListElement_s::Ok;
                            NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " send UL-HARQ-ACK");
                            m_harqPhyModule->ResetUlHarqProcessStatus(tbId.m_rnti,
                                                                      (*itTb).second.harqProcessId);
                        }
                        if (!m_ltePhyUlHarqFeedbackCallback.IsNull())
                        {
                            m_ltePhyUlHarqFeedbackCallback(harqUlInfo);
                        }
                    }
                    else
                    {
                        auto itHarq = harqDlInfoMap.find(tbId.m_rnti);
                        if (itHarq == harqDlInfoMap.end())
                        {
                            DlInfoListElement_s harqDlInfo;
                            harqDlInfo.m_harqStatus.resize(m_layersNum, DlInfoListElement_s::ACK);
                            harqDlInfo.m_rnti = tbId.m_rnti;
                            harqDlInfo.m_harqProcessId = (*itTb).second.harqProcessId;
                            if ((*itTb).second.corrupt)
                            {
                                harqDlInfo.m_harqStatus.at(tbId.m_layer) =
                                    DlInfoListElement_s::NACK;
                                NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " harqId "
                                                  << (uint16_t)(*itTb).second.harqProcessId
                                                  << " layer " << (uint16_t)tbId.m_layer
                                                  << " send DL-HARQ-NACK");
                                m_harqPhyModule->UpdateDlHarqProcessStatus(
                                    (*itTb).second.harqProcessId,
                                    tbId.m_layer,
                                    (*itTb).second.mi,
                                    (*itTb).second.size,
                                    (*itTb).second.size / EffectiveCodingRate[(*itTb).second.mcs]);
                            }
                            else
                            {
                                harqDlInfo.m_harqStatus.at(tbId.m_layer) = DlInfoListElement_s::ACK;
                                NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " harqId "
                                                  << (uint16_t)(*itTb).second.harqProcessId
                                                  << " layer " << (uint16_t)tbId.m_layer << " size "
                                                  << (*itTb).second.size << " send DL-HARQ-ACK");
                                m_harqPhyModule->ResetDlHarqProcessStatus(
                                    (*itTb).second.harqProcessId);
                            }
                            harqDlInfoMap.insert(
                                std::pair<uint16_t, DlInfoListElement_s>(tbId.m_rnti, harqDlInfo));
                        }
                        else
                        {
                            if ((*itTb).second.corrupt)
                            {
                                (*itHarq).second.m_harqStatus.at(tbId.m_layer) =
                                    DlInfoListElement_s::NACK;
                                NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " harqId "
                                                  << (uint16_t)(*itTb).second.harqProcessId
                                                  << " layer " << (uint16_t)tbId.m_layer << " size "
                                                  << (*itHarq).second.m_harqStatus.size()
                                                  << " send DL-HARQ-NACK");
                                m_harqPhyModule->UpdateDlHarqProcessStatus(
                                    (*itTb).second.harqProcessId,
                                    tbId.m_layer,
                                    (*itTb).second.mi,
                                    (*itTb).second.size,
                                    (*itTb).second.size / EffectiveCodingRate[(*itTb).second.mcs]);
                            }
                            else
                            {
                                NS_ASSERT_MSG(tbId.m_layer < (*itHarq).second.m_harqStatus.size(),
                                              " layer " << (uint16_t)tbId.m_layer);
                                (*itHarq).second.m_harqStatus.at(tbId.m_layer) =
                                    DlInfoListElement_s::ACK;
                                NS_LOG_DEBUG(this << " RNTI " << tbId.m_rnti << " harqId "
                                                  << (uint16_t)(*itTb).second.harqProcessId
                                                  << " layer " << (uint16_t)tbId.m_layer << " size "
                                                  << (*itHarq).second.m_harqStatus.size()
                                                  << " send DL-HARQ-ACK");
                                m_harqPhyModule->ResetDlHarqProcessStatus(
                                    (*itTb).second.harqProcessId);
                            }
                        }
                    } // end if ((*itTb).second.downlink) HARQ
                }     // end if (!(*itTb).second.harqFeedbackSent)
            }
        }
    }

    // send DL HARQ feedback to LtePhy
    std::map<uint16_t, DlInfoListElement_s>::iterator itHarq;
    for (itHarq = harqDlInfoMap.begin(); itHarq != harqDlInfoMap.end(); itHarq++)
    {
        if (!m_ltePhyDlHarqFeedbackCallback.IsNull())
        {
            m_ltePhyDlHarqFeedbackCallback((*itHarq).second);
        }
    }
    // forward control messages of this frame to LtePhy
    if (!m_rxControlMessageList.empty())
    {
        if (!m_ltePhyRxCtrlEndOkCallback.IsNull())
        {
            m_ltePhyRxCtrlEndOkCallback(m_rxControlMessageList);
        }
    }
    ChangeState(IDLE);
    m_rxPacketBurstList.clear();
    m_rxControlMessageList.clear();
    m_expectedTbs.clear();
}

void
LteSpectrumPhy::EndRxSlFrame()
{
    NS_LOG_FUNCTION(this);
    // this will trigger CQI calculation and Error Model evaluation
    // as a side effect, the error model should update the error status of all TBs
    m_interferenceSl->EndRx();

    // Extract the various types of sidelink messages received
    std::vector<uint32_t> pscchIndexes;
    std::vector<uint32_t> psschIndexes;
    std::vector<uint32_t> psdchIndexes;
    std::vector<uint32_t> psbchIndexes;

    for (uint32_t i = 0; i < m_rxPacketInfo.size(); i++)
    {
        Ptr<LteSpectrumSignalParametersSlFrame> pktParams = m_rxPacketInfo.at(i).params;
        Ptr<LteSpectrumSignalParametersSlCtrlFrame> lteSlCtrlRxParams =
            DynamicCast<LteSpectrumSignalParametersSlCtrlFrame>(pktParams);
        Ptr<LteSpectrumSignalParametersSlDataFrame> lteSlDataRxParams =
            DynamicCast<LteSpectrumSignalParametersSlDataFrame>(pktParams);
        Ptr<LteSpectrumSignalParametersSlDiscFrame> lteSlDiscRxParams =
            DynamicCast<LteSpectrumSignalParametersSlDiscFrame>(pktParams);
        Ptr<LteSpectrumSignalParametersSlMibFrame> lteSlMibRxParams =
            DynamicCast<LteSpectrumSignalParametersSlMibFrame>(pktParams);

        if (lteSlCtrlRxParams != nullptr)
        {
            pscchIndexes.push_back(i);
        }
        else if (lteSlDataRxParams != nullptr)
        {
            psschIndexes.push_back(i);
        }
        else if (lteSlDiscRxParams != nullptr)
        {
            psdchIndexes.push_back(i);
        }
        else if (lteSlMibRxParams != nullptr)
        {
            psbchIndexes.push_back(i);
        }
        else
        {
            NS_ABORT_MSG("Invalid sidelink packet type");
        }
    }

    if (!pscchIndexes.empty())
    {
        RxSlPscch(pscchIndexes);
    }
    if (!psschIndexes.empty())
    {
        RxSlPssch(psschIndexes);
    }
    if (!psdchIndexes.empty())
    {
        RxSlPsdch(psdchIndexes);
    }
    if (!psbchIndexes.empty())
    {
        RxSlPsbch(psbchIndexes);
    }

    // clear received packets
    ChangeState(IDLE);
    m_rxPacketBurstList.clear();
    m_rxControlMessageList.clear();
    m_rxPacketInfo.clear();
    m_expectedSlTbs.clear();
    m_expectedDiscTbs.clear();
}

void
LteSpectrumPhy::RxSlPscch(std::vector<uint32_t> pktIndexes)
{
    NS_LOG_FUNCTION(this << "Nb PSCCH messages:" << pktIndexes.size());

    // When control messages collide in the PSCCH, the receiver cannot know how many transmissions
    // occurred we sort the messages by SINR and try to decode the ones with highest average SINR
    // per RB first only one message per RB can be decoded

    std::list<Ptr<Packet>> rxControlMessageOkList;
    bool error = true;
    std::multiset<SlCtrlPacketInfo_t> sortedControlMessages;
    // container to store the RB indices of the collided TBs
    std::set<int> collidedRbBitmap;
    // container to store the RB indices of the decoded TBs
    std::set<int> rbDecodedBitmap;

    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        uint32_t pktIndex = pktIndexes[i];
        Ptr<LteSpectrumSignalParametersSlCtrlFrame> params =
            DynamicCast<LteSpectrumSignalParametersSlCtrlFrame>(m_rxPacketInfo.at(pktIndex).params);
        NS_ASSERT(params);
        Ptr<PacketBurst> pb = params->packetBurst;
        NS_ASSERT_MSG(pb->GetNPackets() == 1, "Received PSCCH burst with more than one packet");

        double meanSinr =
            GetMeanSinr(m_slSinrPerceived[pktIndexes[i]], m_rxPacketInfo.at(pktIndex).rbBitmap);
        SlCtrlPacketInfo_t pInfo;
        pInfo.sinr = meanSinr;
        pInfo.index = pktIndex;
        sortedControlMessages.insert(pInfo);
    }

    if (m_dropRbOnCollisionEnabled)
    {
        NS_LOG_DEBUG(this << "Ctrl DropOnCollisionEnabled");
        // Add new loop to make one pass and identify which RB have collisions
        std::set<int> collidedRbBitmapTemp;

        for (auto it = sortedControlMessages.begin(); it != sortedControlMessages.end(); it++)
        {
            uint32_t pktIndex = (*it).index;
            for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                 rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                 rbIt++)
            {
                if (collidedRbBitmapTemp.find(*rbIt) != collidedRbBitmapTemp.end())
                {
                    // collision, update the bitmap
                    collisioncount++;
                    // std::cout<<collisioncount<<std::endl;
                    collidedRbBitmap.insert(*rbIt);
                    break;
                }
                else
                {
                    // store resources used by the packet to detect collision
                    collidedRbBitmapTemp.insert((*rbIt));
                }
            }
        }
    }

    for (auto it = sortedControlMessages.begin(); it != sortedControlMessages.end(); it++)
    {
        uint32_t pktIndex = (*it).index;

        bool corrupt = false;

        if (m_slCtrlErrorModelEnabled)
        {
            for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                 rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                 rbIt++)
            {
                // if m_dropRbOnCollisionEnabled == false, collidedRbBitmap will remain empty
                // and we move to the second "if" to check if the TB with similar RBs has already
                // been decoded. If m_dropRbOnCollisionEnabled == true, all the collided TBs
                // are marked corrupt and this for loop will break in the first "if" condition
                if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                {
                    corrupt = true;
                    NS_LOG_DEBUG(this << " RB " << *rbIt << " has collided");
                    break;
                }
                if (rbDecodedBitmap.find(*rbIt) != rbDecodedBitmap.end())
                {
                    NS_LOG_DEBUG(*rbIt << " TB with the similar RB has already been decoded. Avoid "
                                          "to decode it again!");
                    corrupt = true;
                    break;
                }
            }

            if (!corrupt)
            {
                double errorRate;
                // Average gain for SIMO based on [CatreuxMIMO] --> m_slSinrPerceived[i] * 2.51189
                NS_LOG_DEBUG(this << " Average gain for SIMO = " << m_slRxGain << " Watts");
                errorRate = LteNistErrorModel::GetPscchBler(
                                m_fadingModel,
                                LteNistErrorModel::SISO,
                                GetMeanSinr(m_slSinrPerceived[pktIndex] * m_slRxGain,
                                            m_rxPacketInfo.at(pktIndex).rbBitmap))
                                .tbler;
                corrupt = !(m_random->GetValue() > errorRate);
                NS_LOG_DEBUG(this << " PSCCH Decoding, errorRate " << errorRate << " error "
                                  << corrupt);
            }
        }
        else
        {
            // No error model enabled. If m_dropRbOnCollisionEnabled == true, it will just label the
            // TB as corrupted if the two TBs received at the same time use same RB. Note: PSCCH
            // occupies one RB. On the other hand, if m_dropRbOnCollisionEnabled == false, all the
            // TBs are considered as not corrupted.
            if (m_dropRbOnCollisionEnabled)
            {
                for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                     rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                     rbIt++)
                {
                    if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                    {
                        corrupt = true;
                        NS_LOG_DEBUG(this << " RB " << *rbIt << " has collided");
                        break;
                    }
                }
            }
        }

        if (!corrupt)
        {
            error = false; // at least one control packet is OK
            rxControlMessageOkList.push_back(
                m_rxPacketInfo.at(pktIndex).params->packetBurst->GetPackets().front());
            // Store the indices of the decoded RBs
            rbDecodedBitmap.insert(m_rxPacketInfo.at(pktIndex).rbBitmap.begin(),
                                   m_rxPacketInfo.at(pktIndex).rbBitmap.end());
        }

        // Add PSCCH trace.
        Ptr<Packet> pkt = m_rxPacketInfo[pktIndex].params->packetBurst->GetPackets().front();
        LteSlSciHeader sciHeader;
        pkt->PeekHeader(sciHeader);
        LteSlSciTag tag;
        pkt->PeekPacketTag(tag);
        SlPhyReceptionStatParameters params;
        params.m_timestamp = Simulator::Now().GetMilliSeconds();
        params.m_cellId = m_cellId;
        params.m_imsi = 0; // it will be set by DlPhyTransmissionCallback in LteHelper
        params.m_rnti = tag.GetRnti();
        params.m_resPscch = tag.GetResNo();
        params.m_size = pkt->GetSize();
        params.m_hopping = sciHeader.IsHopping();
        params.m_hoppingInfo = sciHeader.GetHoppingInfo();
        params.m_rbLen = sciHeader.GetRbLen();
        params.m_rbStart = sciHeader.GetRbStart();
        params.m_iTrp = sciHeader.GetTrp();
        params.m_mcs = sciHeader.GetMcs();
        params.m_groupDstId = sciHeader.GetGroupDstId();
        params.m_correctness = (uint8_t)!corrupt;

        // std::cout<<"RNTI "<<params.m_rnti<<" GroupDstId "<<int(params.m_groupDstId)<<" RbStart "<<int(params.m_rbStart)<<" RbLen "<<int(params.m_rbLen)<<std::endl;
        // Call trace
        m_slPscchReception(params);
    }

    if (!pktIndexes.empty())
    {
        if (!error)
        {
            if (!m_ltePhyRxCtrlEndOkCallback.IsNull())
            {
                NS_LOG_DEBUG(this << " PSCCH OK");
                std::list<Ptr<Packet>>::iterator it;
                for (it = rxControlMessageOkList.begin(); it != rxControlMessageOkList.end(); it++)
                {
                    m_ltePhyRxPscchEndOkCallback(*it);
                }
            }
        }
        else
        {
            if (!m_ltePhyRxCtrlEndErrorCallback.IsNull())
            {
                NS_LOG_DEBUG(this << " PSCCH Error");
                m_ltePhyRxDataEndErrorCallback();
            }
        }
    }
}

void
LteSpectrumPhy::RxSlPssch(std::vector<uint32_t> pktIndexes)
{
    NS_LOG_FUNCTION(this << "Nb PSSCH messages:" << pktIndexes.size());

    NS_LOG_FUNCTION(this << " Node ID:" << GetDevice()->GetNode()->GetId()
                         << " State: " << m_state);

    NS_ASSERT(m_state == RX_DATA);

    NS_LOG_DEBUG("No. of SL burts " << m_rxPacketInfo.size());
    NS_LOG_DEBUG("Expected TBs (communication) " << m_expectedSlTbs.size());
    NS_LOG_DEBUG("No Ctrl messages " << m_rxControlMessageList.size());

    NS_ASSERT(m_transmissionMode < m_txModeGain.size());

    // Compute error on PSSCH
    // Create a mapping between the packet tag and the index of the packet bursts. We need this
    // information to access the right SINR measurement.
    std::map<SlTbId_t, uint32_t> expectedTbToSinrIndex;
    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        uint32_t pktIndex = pktIndexes[i];
        // even though there may be multiple packets, they all have
        // the same tag
        auto j = m_rxPacketInfo.at(pktIndex).params->packetBurst->Begin();
        // retrieve TB info of this packet
        LteRadioBearerTag tag;
        (*j)->PeekPacketTag(tag);
        SlTbId_t tbId;
        tbId.m_rnti = tag.GetRnti();
        tbId.m_l1dst = tag.GetDestinationL2Id() & 0xFF;
        // ackStatus[tag.GetSequenceNo()]=1;
        int temp = 0;
        // for(uint16_t i=tag.GetSequenceNo();i>=std::max((0),tag.GetSequenceNo()-100);i++){
        //     if(ackStatus[i])temp++;
        // }
        rec = temp;
        expectedTbToSinrIndex.insert(std::pair<SlTbId_t, uint32_t>(tbId, pktIndex));
    }

    std::set<int> collidedRbBitmap;
    // std::cout<<"Hi";
    if (m_dropRbOnCollisionEnabled)
    {
        // std::cout<<"Hi";
        NS_LOG_DEBUG(this << " PSSCH DropOnCollisionEnabled: Identifying RB Collisions");
        std::set<int> collidedRbBitmapTemp;
        for (auto itTb = m_expectedSlTbs.begin(); itTb != m_expectedSlTbs.end(); itTb++)
        {
            for (auto rbIt = (*itTb).second.rbBitmap.begin(); rbIt != (*itTb).second.rbBitmap.end();
                 rbIt++)
            {
                if (collidedRbBitmapTemp.find(*rbIt) != collidedRbBitmapTemp.end())
                {
                    // collision, update the bitmap
                    collidedRbBitmap.insert(*rbIt);
                }
                else
                {
                    // store resources used by the packet to detect collision
                    collidedRbBitmapTemp.insert(*rbIt);
                    // std::cout<<"Hi";
                }
            }
        }
    }

    // Compute the error and check for collision for each expected Tb
    auto itTb = m_expectedSlTbs.begin();
    std::map<SlTbId_t, uint32_t>::iterator itSinr;
    while (itTb != m_expectedSlTbs.end())
    {
        itSinr = expectedTbToSinrIndex.find((*itTb).first);
        // avoid to check for errors and collisions when there is no actual data transmitted
        if ((!m_rxPacketInfo.empty()) && (itSinr != expectedTbToSinrIndex.end()))
        {
            HarqProcessInfoList_t harqInfoList;
            bool rbCollided = false;
            if (m_slDataErrorModelEnabled)
            {
                // retrieve HARQ info
                if ((*itTb).second.ndi == 0)
                {
                    harqInfoList = m_slHarqPhyModule->GetHarqProcessInfoSl((*itTb).first.m_rnti,
                                                                           (*itTb).first.m_l1dst);
                    NS_LOG_DEBUG("Nb Retx=" << harqInfoList.size());
                }

                NS_LOG_DEBUG("Time: " << Simulator::Now().GetMilliSeconds()
                                      << "msec From: " << (*itTb).first.m_rnti
                                      << " Corrupt: " << (*itTb).second.corrupt);

                if (m_dropRbOnCollisionEnabled)
                {
                    NS_LOG_DEBUG(this << " PSSCH DropOnCollisionEnabled: Labeling Corrupted TB");
                    // Check if any of the RBs have been decoded
                    for (auto rbIt = (*itTb).second.rbBitmap.begin();
                         rbIt != (*itTb).second.rbBitmap.end();
                         rbIt++)
                    {
                        if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                        {
                            NS_LOG_DEBUG(*rbIt << " collided, labeled as corrupted!");
                            // {std::cout<<*rbIt<<" collided"<<std::endl;
                            collisioncount++;
                            // std::cout<<collisioncount<<std::endl;
                            rbCollided = true;
                            (*itTb).second.corrupt = true;
                            break;
                        }
                    }
                }
                TbErrorStats_t tbStats = LteNistErrorModel::GetPsschBler(
                    m_fadingModel,
                    LteNistErrorModel::SISO,
                    (*itTb).second.mcs,
                    GetMeanSinr(m_slSinrPerceived[(*itSinr).second] * m_slRxGain,
                                (*itTb).second.rbBitmap),
                    harqInfoList);
                (*itTb).second.sinr = tbStats.sinr;
                if (!rbCollided)
                {
                    if (m_slHarqPhyModule->IsPrevDecoded((*itTb).first.m_rnti,
                                                         (*itTb).first.m_l1dst))
                    {
                        (*itTb).second.corrupt = false;
                    }
                    else
                    {
                        double rndVal = m_random->GetValue();
                        (*itTb).second.corrupt = !(rndVal > tbStats.tbler);
                    }
                }

                NS_LOG_DEBUG("From RNTI " << (*itTb).first.m_rnti << " TB size "
                                          << (*itTb).second.size << " MCS "
                                          << (uint32_t)(*itTb).second.mcs);
                NS_LOG_DEBUG("RB bitmap size "
                             << (*itTb).second.rbBitmap.size() << " TBLER " << tbStats.tbler
                             << " corrupted " << (*itTb).second.corrupt << " prevDecoded"
                             << m_slHarqPhyModule->IsPrevDecoded((*itTb).first.m_rnti,
                                                                 (*itTb).first.m_l1dst));
            }
            else
            {
                if (m_dropRbOnCollisionEnabled)
                {
                    NS_LOG_DEBUG(this << " PSSCH DropOnCollisionEnabled: Labeling Corrupted TB");
                    // Check if any of the RBs have been decoded
                    for (auto rbIt = (*itTb).second.rbBitmap.begin();
                         rbIt != (*itTb).second.rbBitmap.end();
                         rbIt++)
                    {
                        if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                        {
                            NS_LOG_DEBUG(*rbIt << " collided, labeled as corrupted!");
                            // std::cout<<*rbIt<<" collided"<<std::endl;
                            collisioncount++;
                            // std::cout<<collisioncount<<std::endl;
                            rbCollided = true;
                            (*itTb).second.corrupt = true;
                            break;
                        }
                    }
                }

                if (!rbCollided)
                {
                    (*itTb).second.corrupt = false;
                }
            }

            NS_LOG_DEBUG(this << " HARQ buffer size = " << harqInfoList.size());

            // fire traces on SL reception PHY stats
            PhyReceptionStatParameters params;
            params.m_timestamp = Simulator::Now().GetMilliSeconds();
            params.m_cellId = m_cellId;
            params.m_imsi = 0;
            params.m_rnti = (*itTb).first.m_rnti;
            params.m_txMode = m_transmissionMode;
            params.m_layer = 0;
            params.m_mcs = (*itTb).second.mcs;
            params.m_size = (*itTb).second.size;
            params.m_rv = (*itTb).second.rv;
            params.m_ndi = (*itTb).second.ndi;
            params.m_ccId = m_componentCarrierId;
            params.m_correctness = (uint8_t) !(*itTb).second.corrupt;
            params.m_sinrPerRb = GetMeanSinr(m_slSinrPerceived[(*itSinr).second] * m_slRxGain,
                                             (*itTb).second.rbBitmap);
            m_slPhyReception(params);
        }

        itTb++;
    }

    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        // TODO: We need to extract each packet from the burst to send it up but why
        // should we increase the HARQ process for each packet? Shouldn't it be done
        // just for each packet burst?
        uint32_t pktIndex = pktIndexes[i];
        for (auto j = m_rxPacketInfo[pktIndex].params->packetBurst->Begin();
             j != m_rxPacketInfo[pktIndex].params->packetBurst->End();
             ++j)
        {
            // retrieve TB info of this packet
            LteRadioBearerTag tag;
            (*j)->PeekPacketTag(tag);
            SlTbId_t tbId;
            tbId.m_rnti = tag.GetRnti();
            tbId.m_l1dst = tag.GetDestinationL2Id() & 0xFF;
            itTb = m_expectedSlTbs.find(tbId);
            NS_LOG_INFO("Packet of " << tbId.m_rnti << " group " << (uint16_t)tbId.m_l1dst);
            if (itTb != m_expectedSlTbs.end())
            {
                m_slHarqPhyModule->IncreaseTbIdx((*itTb).first.m_rnti, (*itTb).first.m_l1dst);
                if (!(*itTb).second.corrupt &&
                    !m_slHarqPhyModule->IsPrevDecoded((*itTb).first.m_rnti, (*itTb).first.m_l1dst))
                {
                    m_slHarqPhyModule->IndicatePrevDecoded((*itTb).first.m_rnti,
                                                           (*itTb).first.m_l1dst);
                    m_phyRxEndOkTrace(*j);

                    if (!m_ltePhyRxDataEndOkCallback.IsNull())
                    {
                        m_ltePhyRxDataEndOkCallback(*j);
                    }
                }
                else
                {
                    // TB received with errors
                    m_phyRxEndErrorTrace(*j);
                }

                // update HARQ information
                // because we do not have feedbacks we do not reset HARQ now, even if packet was
                // Successfully received
                m_slHarqPhyModule->UpdateSlHarqProcessStatus(tbId.m_rnti,
                                                             tbId.m_l1dst,
                                                             (*itTb).second.sinr);
            }
        }
    }
}

void
LteSpectrumPhy::RxSlPsdch(std::vector<uint32_t> pktIndexes)
{
    NS_LOG_FUNCTION(this << "Number of PSDCH messages: " << pktIndexes.size());

    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        uint32_t pktIndex = pktIndexes[i];
        Ptr<LteSpectrumSignalParametersSlDiscFrame> params =
            DynamicCast<LteSpectrumSignalParametersSlDiscFrame>(m_rxPacketInfo.at(pktIndex).params);
        NS_ABORT_MSG_IF(params == nullptr,
                        "No Sidelink discovery parameters associated with the received packet");
        Ptr<PacketBurst> pb = params->packetBurst;
        NS_ABORT_MSG_IF(pb->GetNPackets() > 1, "Received PSDCH burst with more than one packet");
        Ptr<Packet> pkt = pb->GetPackets().front();
        LteRadioBearerTag tag;
        pkt->PeekPacketTag(tag);

        // retrieve TB info of this packet
        std::list<Ptr<SidelinkDiscResourcePool>>::iterator discIt;
        std::vector<int> rbMap;
        for (discIt = m_discRxPools.begin(); discIt != m_discRxPools.end(); discIt++)
        {
            std::list<SidelinkDiscResourcePool::SidelinkTransmissionInfo> m_psdchTx =
                (*discIt)->GetPsdchTransmissions(params->resNo);
            NS_LOG_DEBUG(this << " Total number of discovery transmissions = " << m_psdchTx.size());
            auto txIt = m_psdchTx.begin();
            if (txIt != m_psdchTx.end())
            {
                // There can be more than one (max 4) PSDCH transmissions, therefore, we need to
                // match the RBs of all the possible PSDCH with the RBs of the received discovery
                // message. This way will make sure that we construct the correct RB map
                while (txIt != m_psdchTx.end())
                {
                    for (int i = txIt->rbStart; i < txIt->rbStart + txIt->nbRb; i++)
                    {
                        NS_LOG_LOGIC(this << " Checking PSDCH RB " << i);
                        rbMap.push_back(i);
                    }
                    if (m_rxPacketInfo.at(pktIndex).rbBitmap == rbMap)
                    {
                        NS_LOG_DEBUG(this << " PSDCH RB matched");
                        NS_ABORT_MSG_IF(params->rv > m_psdchTx.size(),
                                        "RV number can not be greater than the total "
                                        "number of transmissions");
                        uint8_t ndi = params->rv == 0 ? 1 : 0;
                        NS_LOG_DEBUG(this << " Adding expected TB.");
                        AddExpectedTb(tag.GetRnti(),
                                      params->resNo,
                                      ndi,
                                      rbMap,
                                      params->rv,
                                      pktIndex);
                        break;
                    }
                    else
                    {
                        rbMap.clear();
                    }
                    txIt++;
                }
                // It is needed if in future the model would support more than one RX pool
                rbMap.clear();
            }
        }
    }

    // container to store the RB indices of the collided TBs
    std::set<int> collidedRbBitmap;
    // container to store the RB indices of the decoded TBs
    std::set<int> rbDecodedBitmap;
    std::set<SlCtrlPacketInfo_t> sortedDiscMessages;

    for (auto it = m_expectedDiscTbs.begin(); it != m_expectedDiscTbs.end(); it++)
    {
        double meanSinr = GetMeanSinr(m_slSinrPerceived[(*it).second.index], (*it).second.rbBitmap);
        SlCtrlPacketInfo_t pInfo;
        pInfo.sinr = meanSinr;
        pInfo.index = (*it).second.index;
        sortedDiscMessages.insert(pInfo);
        NS_LOG_DEBUG("sortedDiscMessages size = " << sortedDiscMessages.size() << " SINR = "
                                                  << pInfo.sinr << " Index = " << pInfo.index);
    }

    if (m_dropRbOnCollisionEnabled)
    {
        NS_LOG_DEBUG(this << " PSDCH DropOnCollisionEnabled: Identifying RB Collisions");
        std::set<int> collidedRbBitmapTemp;
        for (auto itDiscTb = m_expectedDiscTbs.begin(); itDiscTb != m_expectedDiscTbs.end();
             itDiscTb++)
        {
            for (auto rbIt = (*itDiscTb).second.rbBitmap.begin();
                 rbIt != (*itDiscTb).second.rbBitmap.end();
                 rbIt++)
            {
                if (collidedRbBitmapTemp.find(*rbIt) != collidedRbBitmapTemp.end())
                {
                    // collision, update the bitmap
                    NS_LOG_DEBUG("Collided RB " << (*rbIt));
                    collidedRbBitmap.insert(*rbIt);
                }
                else
                {
                    // store resources used by the packet to detect collision
                    collidedRbBitmapTemp.insert(*rbIt);
                }
            }
        }
    }

    std::list<Ptr<Packet>> rxDiscMessageOkList;
    auto itTbDisc = m_expectedDiscTbs.begin();
    HarqProcessInfoList_t harqInfoList;

    for (auto it = sortedDiscMessages.begin(); it != sortedDiscMessages.end(); it++)
    {
        int pktIndex = (*it).index;
        NS_LOG_DEBUG("Decoding.."
                     << " starting from index = " << pktIndex);
        Ptr<LteSpectrumSignalParametersSlDiscFrame> params =
            DynamicCast<LteSpectrumSignalParametersSlDiscFrame>(m_rxPacketInfo.at(pktIndex).params);
        Ptr<Packet> rxCtrlMsg = params->packetBurst->GetPackets().front();
        SlDiscTbId_t tbId;
        LteRadioBearerTag tag;
        rxCtrlMsg->PeekPacketTag(tag);
        tbId.m_rnti = tag.GetRnti();
        tbId.m_resPsdch = params->resNo;
        itTbDisc = m_expectedDiscTbs.find(tbId);

        NS_ABORT_MSG_IF((itTbDisc == m_expectedDiscTbs.end()),
                        " Unable to retrieve SINR of the expected TB");
        NS_LOG_DEBUG("SINR value index of this TB in m_slSinrPerceived vector is "
                     << (*itTbDisc).second.index);
        // avoid to check for errors when error model is not enabled
        if (m_slDiscoveryErrorModelEnabled)
        {
            // retrieve HARQ info
            if ((*itTbDisc).second.ndi == 0)
            {
                harqInfoList =
                    m_slHarqPhyModule->GetHarqProcessInfoDisc((*itTbDisc).first.m_rnti,
                                                              (*itTbDisc).first.m_resPsdch);
                NS_LOG_DEBUG(this << " Number of Retx =" << harqInfoList.size());
            }

            // Check if any of the RBs in this TB have been collided
            for (auto rbIt = (*itTbDisc).second.rbBitmap.begin();
                 rbIt != (*itTbDisc).second.rbBitmap.end();
                 rbIt++)
            {
                // if m_dropRbOnCollisionEnabled == false, collidedRbBitmap will remain empty
                // and we move to the second "if" to check if the TB with similar RBs has already
                // been decoded. If m_dropRbOnCollisionEnabled == true, all the collided TBs
                // are marked corrupt and this for loop will break in the first "if" condition
                if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                {
                    NS_LOG_DEBUG(*rbIt << " TB collided, labeled as corrupted!");
                    (*itTbDisc).second.corrupt = true;
                    break;
                }
                if (rbDecodedBitmap.find(*rbIt) != rbDecodedBitmap.end())
                {
                    NS_LOG_DEBUG(*rbIt << " TB with the similar RB has already been decoded. Avoid "
                                          "to decode it again!");
                    (*itTbDisc).second.corrupt = true;
                    break;
                }
            }

            TbErrorStats_t tbStats = LteNistErrorModel::GetPsdchBler(
                m_fadingModel,
                LteNistErrorModel::SISO,
                GetMeanSinr(m_slSinrPerceived[(*itTbDisc).second.index] * m_slRxGain,
                            (*itTbDisc).second.rbBitmap),
                harqInfoList);
            (*itTbDisc).second.sinr = tbStats.sinr;

            if (!((*itTbDisc).second.corrupt))
            {
                NS_LOG_DEBUG("RB not collided");
                if (m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                           (*itTbDisc).first.m_resPsdch))
                {
                    NS_LOG_DEBUG("TB previously decoded. Consider it not corrupted");
                    (*itTbDisc).second.corrupt = false;
                }
                else
                {
                    double rndVal = m_random->GetValue();
                    NS_LOG_DEBUG("TBLER is " << tbStats.tbler << " random number drawn is "
                                             << rndVal);
                    (*itTbDisc).second.corrupt = !(rndVal > tbStats.tbler);
                    NS_LOG_DEBUG("Is TB marked as corrupted after tossing the coin? "
                                 << (*itTbDisc).second.corrupt);
                }
            }

            NS_LOG_DEBUG(this << " from RNTI " << (*itTbDisc).first.m_rnti << " TBLER "
                              << tbStats.tbler << " corrupted " << (*itTbDisc).second.corrupt
                              << " Sinr " << (*itTbDisc).second.sinr);

            // If the TB is not corrupt and has already been decoded means that it is a
            // retransmission. We logged it to discard overlapping retransmissions.
            if (!(*itTbDisc).second.corrupt &&
                m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                       (*itTbDisc).first.m_resPsdch))
            {
                rbDecodedBitmap.insert((*itTbDisc).second.rbBitmap.begin(),
                                       (*itTbDisc).second.rbBitmap.end());
            }

            // If the TB is not corrupt and has not been decoded before, we indicate it decoded and
            // consider its reception successful.
            //**NOTE** If the TB is not corrupt and was previously decoded; it means that we have
            // already reported the TB reception to the PHY, therefore, we do not report it again.

            if (!(*itTbDisc).second.corrupt &&
                !m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                        (*itTbDisc).first.m_resPsdch))
            {
                NS_LOG_DEBUG(
                    this << " from RNTI " << (*itTbDisc).first.m_rnti << " corrupted "
                         << (*itTbDisc).second.corrupt << " Previously decoded "
                         << m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                                   (*itTbDisc).first.m_resPsdch));
                m_slHarqPhyModule->IndicateDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                             (*itTbDisc).first.m_resPsdch);
                rxDiscMessageOkList.push_back(rxCtrlMsg);
                if (!m_ltePhyRxPsdchSdRsrpCallback.IsNull())
                {
                    m_ltePhyRxPsdchSdRsrpCallback(rxCtrlMsg,
                                                  params->psd,
                                                  (*itTbDisc).second.rbBitmap);
                }
                // Store the indices of the decoded RBs
                rbDecodedBitmap.insert((*itTbDisc).second.rbBitmap.begin(),
                                       (*itTbDisc).second.rbBitmap.end());
            }
            // Store the HARQ information
            m_slHarqPhyModule->UpdateDiscHarqProcessStatus((*itTbDisc).first.m_rnti,
                                                           (*itTbDisc).first.m_resPsdch,
                                                           (*itTbDisc).second.sinr);
        }
        else
        {
            // No error model enabled.
            // If m_dropRbOnCollisionEnabled == true, collided TBs will be marked as corrupted.
            // Else, TB will be received irrespective of collision and BLER, if it has not been
            // decoded before.
            if (m_dropRbOnCollisionEnabled)
            {
                NS_LOG_DEBUG(this << " PSDCH DropOnCollisionEnabled: Labeling Corrupted TB");
                // Check if any of the RBs in this TB have been collided
                for (auto rbIt = (*itTbDisc).second.rbBitmap.begin();
                     rbIt != (*itTbDisc).second.rbBitmap.end();
                     rbIt++)
                {
                    if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                    {
                        NS_LOG_DEBUG(*rbIt << " TB collided, labeled as corrupted!");
                        (*itTbDisc).second.corrupt = true;
                        break;
                    }
                    else
                    {
                        // We will check all the RBs and label the TB as not corrupted for every
                        // non-collided RBs. if any of the RB would collide, the above "if" will
                        // label the TB as corrupt and break.
                        NS_LOG_DEBUG(*rbIt << " RB not collided");
                        (*itTbDisc).second.corrupt = false;
                    }
                }
            }
            else
            {
                // m_dropRbOnCollisionEnabled == false. TB labeled as not corrupted. It will be
                // received by the UE if it has not been decoded before.
                (*itTbDisc).second.corrupt = false;
            }
            if (!(*itTbDisc).second.corrupt &&
                !m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                        (*itTbDisc).first.m_resPsdch))
            {
                NS_LOG_DEBUG(
                    this << " from RNTI " << (*itTbDisc).first.m_rnti << " corrupted "
                         << (*itTbDisc).second.corrupt << " Previously decoded "
                         << m_slHarqPhyModule->IsDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                                   (*itTbDisc).first.m_resPsdch));
                m_slHarqPhyModule->IndicateDiscTbPrevDecoded((*itTbDisc).first.m_rnti,
                                                             (*itTbDisc).first.m_resPsdch);
                rxDiscMessageOkList.push_back(rxCtrlMsg);
                if (!m_ltePhyRxPsdchSdRsrpCallback.IsNull())
                {
                    m_ltePhyRxPsdchSdRsrpCallback(rxCtrlMsg,
                                                  params->psd,
                                                  (*itTbDisc).second.rbBitmap);
                }
            }
        }

        // traces for discovery rx
        PhyReceptionStatParameters prsparams;
        prsparams.m_timestamp = Simulator::Now().GetMilliSeconds();
        prsparams.m_cellId = m_cellId;
        prsparams.m_imsi = 0; // it will be set by DlPhyTransmissionCallback in LteHelper
        prsparams.m_rnti = (*itTbDisc).first.m_rnti;
        prsparams.m_txMode = m_transmissionMode;
        prsparams.m_layer = 0;
        prsparams.m_mcs = 8; // for discovery, we use a fixed QPSK modulation
        prsparams.m_size =
            m_rxPacketInfo.at(pktIndex)
                .params->packetBurst->GetSize(); // discovery message has a static size
        prsparams.m_ndi = (*itTbDisc).second.ndi;
        prsparams.m_ccId = m_componentCarrierId;
        prsparams.m_correctness = (uint8_t) !(*itTbDisc).second.corrupt;
        prsparams.m_sinrPerRb =
            GetMeanSinr(m_slSinrPerceived[(*itTbDisc).second.index] * m_slRxGain,
                        (*itTbDisc).second.rbBitmap);
        prsparams.m_rv = (*itTbDisc).second.rv;
        m_slPhyReception(prsparams);
    }

    if (!rxDiscMessageOkList.empty())
    {
        if (!m_ltePhyRxDataEndOkCallback.IsNull())
        {
            NS_LOG_DEBUG(this << " Discovery OK");
            std::list<Ptr<Packet>>::iterator it;
            for (it = rxDiscMessageOkList.begin(); it != rxDiscMessageOkList.end(); it++)
            {
                m_ltePhyRxPsdchEndOkCallback(*it);
            }
        }
        else
        {
            NS_ABORT_MSG(
                "There are correctly received Disc messages but LtePhyRxDataEndOkCallback is NULL");
        }
    }
    else
    {
        if (!m_ltePhyRxCtrlEndErrorCallback.IsNull())
        {
            NS_LOG_DEBUG(this << " Discovery Error");
            m_ltePhyRxCtrlEndErrorCallback();
        }
    }
}

void
LteSpectrumPhy::RxSlPsbch(std::vector<uint32_t> pktIndexes)
{
    NS_LOG_FUNCTION(this << "Nb PSBCH messages:" << pktIndexes.size());

    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        uint32_t pktIndex = pktIndexes[i];
        Ptr<LteSpectrumSignalParametersSlMibFrame> params =
            DynamicCast<LteSpectrumSignalParametersSlMibFrame>(m_rxPacketInfo.at(pktIndex).params);
        NS_ASSERT(params);

        NS_LOG_LOGIC("Receiving a SLSS");
        // Measure S-RSRP
        if (!m_ltePhyRxSlssCallback.IsNull())
        {
            m_ltePhyRxSlssCallback(params->slssId, params->psd);
        }
    }

    // When control messages collide in the PSBCH, the receiver cannot know how many transmissions
    // occurred we sort the messages by SINR and try to decode the ones with highest average SINR
    // per RB first only one message per RB can be decoded

    std::list<uint32_t> rxControlMessageOkList;
    bool error = true;
    std::multiset<SlCtrlPacketInfo_t> sortedControlMessages;
    // container to store the RB indices of the collided TBs
    std::set<int> collidedRbBitmap;
    // container to store the RB indices of the decoded TBs
    std::set<int> rbDecodedBitmap;

    for (uint32_t i = 0; i < pktIndexes.size(); i++)
    {
        uint32_t pktIndex = pktIndexes[i];
        Ptr<LteSpectrumSignalParametersSlMibFrame> params =
            DynamicCast<LteSpectrumSignalParametersSlMibFrame>(m_rxPacketInfo.at(pktIndex).params);
        NS_ASSERT(params);
        Ptr<PacketBurst> pb = params->packetBurst;
        NS_ASSERT_MSG(pb->GetNPackets() == 1, "Received PSCCH burst with more than one packet");

        double meanSinr =
            GetMeanSinr(m_slSinrPerceived[pktIndexes[i]], m_rxPacketInfo.at(pktIndex).rbBitmap);
        SlCtrlPacketInfo_t pInfo;
        pInfo.sinr = meanSinr;
        pInfo.index = pktIndex;
        sortedControlMessages.insert(pInfo);
    }

    if (m_dropRbOnCollisionEnabled)
    {
        NS_LOG_DEBUG(this << "PSBCH DropOnCollisionEnabled");
        // Add new loop to make one pass and identify which RB have collisions
        std::set<int> collidedRbBitmapTemp;

        for (auto it = sortedControlMessages.begin(); it != sortedControlMessages.end(); it++)
        {
            // std::cout<<"Hi";
            uint32_t pktIndex = (*it).index;
            for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                 rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                 rbIt++)
            {
                if (collidedRbBitmapTemp.find(*rbIt) != collidedRbBitmapTemp.end())
                {
                    // collision, update the bitmap
                    collisioncount++;
                    // std::cout<<collisioncount<<std::endl;
                    collidedRbBitmap.insert(*rbIt);
                    break;
                }
                else
                {
                    // store resources used by the packet to detect collision
                    collidedRbBitmapTemp.insert((*rbIt));
                    // std::cout<<"Hi";
                }
            }
        }
    }

    for (auto it = sortedControlMessages.begin(); it != sortedControlMessages.end(); it++)
    {
        uint32_t pktIndex = (*it).index;

        bool corrupt = false;

        if (m_slCtrlErrorModelEnabled)
        {
            for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                 rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                 rbIt++)
            {
                // if m_dropRbOnCollisionEnabled == false, collidedRbBitmap will remain empty
                // and we move to the second "if" to check if the TB with similar RBs has already
                // been decoded. If m_dropRbOnCollisionEnabled == true, all the collided TBs
                // are marked corrupt and this for loop will break in the first "if" condition
                if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                {
                    corrupt = true;
                    NS_LOG_DEBUG(this << " RB " << *rbIt << " has collided");
                    std::cout<<this << " RB " << *rbIt << " has collided"<<std::endl;
                    collisioncount++;
                    // std::cout<<collisioncount<<std::endl;
                    break;
                }
                if (rbDecodedBitmap.find(*rbIt) != rbDecodedBitmap.end())
                {
                    NS_LOG_DEBUG(*rbIt << " TB with the similar RB has already been decoded. Avoid "
                                          "to decode it again!");
                    corrupt = true;
                    break;
                }
            }

            if (!corrupt)
            {
                double errorRate;
                // Average gain for SIMO based on [CatreuxMIMO] --> m_slSinrPerceived[i] * 2.51189
                errorRate = LteNistErrorModel::GetPsbchBler(
                                m_fadingModel,
                                LteNistErrorModel::SISO,
                                GetMeanSinr(m_slSinrPerceived[pktIndex] * m_slRxGain,
                                            m_rxPacketInfo[pktIndex].rbBitmap))
                                .tbler;
                corrupt = !(m_random->GetValue() > errorRate);
                NS_LOG_DEBUG(this << " PSBCH Decoding, errorRate " << errorRate << " error "
                                  << corrupt);
            }
        }
        else
        {
            // No error model enabled. If m_dropRbOnCollisionEnabled == true, it will just label the
            // TB as corrupted if the two TBs received at the same time use same RB. Note: PSCCH
            // occupies one RB. On the other hand, if m_dropRbOnCollisionEnabled == false, all the
            // TBs are considered as not corrupted.
            if (m_dropRbOnCollisionEnabled)
            {
                for (auto rbIt = m_rxPacketInfo.at(pktIndex).rbBitmap.begin();
                     rbIt != m_rxPacketInfo.at(pktIndex).rbBitmap.end();
                     rbIt++)
                {
                    if (collidedRbBitmap.find(*rbIt) != collidedRbBitmap.end())
                    {
                        corrupt = true;
                        NS_LOG_DEBUG(this << " RB " << *rbIt << " has collided");
                        std::cout<<this << " RB " << *rbIt << " has collided"<<std::endl;
                        collisioncount++;
                        // std::cout<<collisioncount<<std::endl;
                        break;
                    }
                }
            }
        }

        if (!corrupt)
        {
            error = false; // at least one control packet is OK
            rxControlMessageOkList.push_back(pktIndex);
            // Store the indices of the decoded RBs
            rbDecodedBitmap.insert(m_rxPacketInfo.at(pktIndex).rbBitmap.begin(),
                                   m_rxPacketInfo.at(pktIndex).rbBitmap.end());
        }

        // Add PSBCH trace
        PhyReceptionStatParameters prsparams;
        prsparams.m_timestamp = Simulator::Now().GetMilliSeconds();
        prsparams.m_cellId = m_cellId;
        prsparams.m_imsi = 0; // it will be set by DlPhyTransmissionCallback in LteHelper
        prsparams.m_rnti = 0;
        prsparams.m_txMode = m_transmissionMode;
        prsparams.m_layer = 0;
        prsparams.m_mcs =
            0; // for PSBCH, we use a fixed modulation (no mcs defined), use 0 to identify discovery
        prsparams.m_size = m_rxPacketInfo.at(pktIndex).params->packetBurst->GetSize();
        prsparams.m_rv = 0;
        prsparams.m_ndi = 1;
        prsparams.m_ccId = m_componentCarrierId;
        prsparams.m_correctness = !corrupt;
        prsparams.m_sinrPerRb = GetMeanSinr(m_slSinrPerceived[pktIndex] * m_slRxGain,
                                            m_rxPacketInfo.at(pktIndex).rbBitmap);
        m_slPhyReception(prsparams);
    }

    if (!pktIndexes.empty())
    {
        if (!error)
        {
            if (!m_ltePhyRxCtrlEndOkCallback.IsNull())
            {
                NS_LOG_DEBUG(this << " PSBCH OK");
                std::list<uint32_t>::iterator it;
                for (it = rxControlMessageOkList.begin(); it != rxControlMessageOkList.end(); it++)
                {
                    m_ltePhyRxPsbchEndOkCallback(
                        m_rxPacketInfo.at(*it).params->packetBurst->GetPackets().front(),
                        m_rxPacketInfo.at(*it).params->slssId);
                }
            }
        }
        else
        {
            if (!m_ltePhyRxCtrlEndErrorCallback.IsNull())
            {
                NS_LOG_DEBUG(this << " PSBCH Error");
                m_ltePhyRxDataEndErrorCallback();
            }
        }
    }
}

void
LteSpectrumPhy::EndRxDlCtrl()
{
    NS_LOG_FUNCTION(this);
    NS_LOG_LOGIC(this << " state: " << m_state);

    NS_ASSERT(m_state == RX_DL_CTRL);

    // this will trigger CQI calculation and Error Model evaluation
    // as a side effect, the error model should update the error status of all TBs
    m_interferenceCtrl->EndRx();
    // apply transmission mode gain
    NS_LOG_DEBUG(this << " txMode " << (uint16_t)m_transmissionMode << " gain "
                      << m_txModeGain.at(m_transmissionMode));
    NS_ASSERT(m_transmissionMode < m_txModeGain.size());
    if (m_transmissionMode > 0)
    {
        // in case of MIMO, ctrl is always txed as TX diversity
        m_sinrPerceived *= m_txModeGain.at(1);
    }
    //   m_sinrPerceived *= m_txModeGain.at (m_transmissionMode);
    bool error = false;
    if (m_ctrlErrorModelEnabled)
    {
        double errorRate = LteMiErrorModel::GetPcfichPdcchError(m_sinrPerceived);
        error = !(m_random->GetValue() > errorRate);
        NS_LOG_DEBUG(this << " PCFICH-PDCCH Decodification, errorRate " << errorRate << " error "
                          << error);
    }

    if (!error)
    {
        if (!m_ltePhyRxCtrlEndOkCallback.IsNull())
        {
            NS_LOG_DEBUG(this << " PCFICH-PDCCH Rxed OK");
            m_ltePhyRxCtrlEndOkCallback(m_rxControlMessageList);
        }
    }
    else
    {
        if (!m_ltePhyRxCtrlEndErrorCallback.IsNull())
        {
            NS_LOG_DEBUG(this << " PCFICH-PDCCH Error");
            m_ltePhyRxCtrlEndErrorCallback();
        }
    }
    ChangeState(IDLE);
    m_rxControlMessageList.clear();
}

void
LteSpectrumPhy::EndRxUlSrs()
{
    NS_ASSERT(m_state == RX_UL_SRS);
    ChangeState(IDLE);
    m_interferenceCtrl->EndRx();
    // nothing to do (used only for SRS at this stage)
}

void
LteSpectrumPhy::SetCellId(uint16_t cellId)
{
    m_cellId = cellId;
}

void
LteSpectrumPhy::AddL1GroupId(uint8_t groupId)
{
    NS_LOG_FUNCTION(this << (uint16_t)groupId);
    m_l1GroupIds.insert(groupId);
}

void
LteSpectrumPhy::RemoveL1GroupId(uint8_t groupId)
{
    m_l1GroupIds.erase(groupId);
}

void
LteSpectrumPhy::SetComponentCarrierId(uint8_t componentCarrierId)
{
    m_componentCarrierId = componentCarrierId;
}

void
LteSpectrumPhy::AddRsPowerChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceCtrl->AddRsPowerChunkProcessor(p);
}

void
LteSpectrumPhy::AddDataPowerChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceData->AddRsPowerChunkProcessor(p);
}

void
LteSpectrumPhy::AddDataSinrChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceData->AddSinrChunkProcessor(p);
}

void
LteSpectrumPhy::AddInterferenceCtrlChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceCtrl->AddInterferenceChunkProcessor(p);
}

void
LteSpectrumPhy::AddInterferenceDataChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceData->AddInterferenceChunkProcessor(p);
}

void
LteSpectrumPhy::AddCtrlSinrChunkProcessor(Ptr<LteChunkProcessor> p)
{
    m_interferenceCtrl->AddSinrChunkProcessor(p);
}

void
LteSpectrumPhy::AddSlSinrChunkProcessor(Ptr<LteSlChunkProcessor> p)
{
    m_interferenceSl->AddSinrChunkProcessor(p);
}

void
LteSpectrumPhy::AddSlSignalChunkProcessor(Ptr<LteSlChunkProcessor> p)
{
    m_interferenceSl->AddRsPowerChunkProcessor(p);
}

void
LteSpectrumPhy::AddSlInterferenceChunkProcessor(Ptr<LteSlChunkProcessor> p)
{
    m_interferenceSl->AddInterferenceChunkProcessor(p);
}

void
LteSpectrumPhy::SetTransmissionMode(uint8_t txMode)
{
    NS_LOG_FUNCTION(this << (uint16_t)txMode);
    NS_ASSERT_MSG(txMode < m_txModeGain.size(),
                  "TransmissionMode not available: 1.." << m_txModeGain.size());
    m_transmissionMode = txMode;
    m_layersNum = TransmissionModesLayers::TxMode2LayerNum(txMode);
}

void
LteSpectrumPhy::SetTxModeGain(uint8_t txMode, double gain)
{
    NS_LOG_FUNCTION(this << " txmode " << (uint16_t)txMode << " gain " << gain);
    if (txMode > 0)
    {
        // convert to linear
        double gainLin = std::pow(10.0, (gain / 10.0));
        if (m_txModeGain.size() < txMode)
        {
            m_txModeGain.resize(txMode);
        }
        m_txModeGain.at(txMode - 1) = gainLin;
    }
}

double
LteSpectrumPhy::GetMeanSinr(const SpectrumValue& sinr, const std::vector<int>& map)
{
    NS_LOG_FUNCTION(this << sinr);
    SpectrumValue sinrCopy = sinr;
    double sinrLin = 0;
    for (uint32_t i = 0; i < map.size(); i++)
    {
        sinrLin += sinrCopy[map.at(i)];
    }
    return sinrLin / map.size();
}

LteSpectrumPhy::State
LteSpectrumPhy::GetState()
{
    NS_LOG_FUNCTION(this);
    return m_state;
}

void
LteSpectrumPhy::SetSlssid(uint64_t slssid)
{
    NS_LOG_FUNCTION(this);
    m_slssId = slssid;
}

void
LteSpectrumPhy::SetRxPool(Ptr<SidelinkDiscResourcePool> newpool)
{
    NS_LOG_FUNCTION(this);
    m_discRxPools.push_back(newpool);
}

void
LteSpectrumPhy::SetDiscNumRetx(uint8_t retx)
{
    NS_LOG_FUNCTION(this << retx);
    m_slHarqPhyModule->SetDiscNumRetx(retx);
}

void
LteSpectrumPhy::SetSlRxGain(double gain)
{
    NS_LOG_FUNCTION(this << gain);
    // convert to linear
    gain = std::pow(10.0, (gain / 10.0));
    NS_LOG_DEBUG("Linear gain = " << gain);
    m_slRxGain = gain;
}

int64_t
LteSpectrumPhy::AssignStreams(int64_t stream)
{
    NS_LOG_FUNCTION(this << stream);
    m_random->SetStream(stream);
    return 1;
}

} // namespace ns3
