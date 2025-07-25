/*
 * Copyright (c) 2011 Centre Tecnologic de Telecomunicacions de Catalunya (CTTC)
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
 * Author: Marco Miozzo <marco.miozzo@cttc.es>
 * Modification: Dizhi Zhou <dizhi.zhou@gmail.com>    // modify codes related to downlink scheduler
 */

#include "fdbet-ff-mac-scheduler.h"

#include "lte-amc.h"
#include "lte-vendor-specific-parameters.h"

#include <ns3/boolean.h>
#include <ns3/log.h>
#include <ns3/math.h>
#include <ns3/pointer.h>
#include <ns3/simulator.h>

#include <cfloat>
#include <set>

namespace ns3
{

NS_LOG_COMPONENT_DEFINE("FdBetFfMacScheduler");

/// FdBetType0AllocationRbg array
static const int FdBetType0AllocationRbg[4] = {
    10,  // RGB size 1
    26,  // RGB size 2
    63,  // RGB size 3
    110, // RGB size 4
};       // see table 7.1.6.1-1 of 36.213

NS_OBJECT_ENSURE_REGISTERED(FdBetFfMacScheduler);

FdBetFfMacScheduler::FdBetFfMacScheduler()
    : m_cschedSapUser(nullptr),
      m_schedSapUser(nullptr),
      m_timeWindow(99.0),
      m_nextRntiUl(0)
{
    m_amc = CreateObject<LteAmc>();
    m_cschedSapProvider = new MemberCschedSapProvider<FdBetFfMacScheduler>(this);
    m_schedSapProvider = new MemberSchedSapProvider<FdBetFfMacScheduler>(this);
}

FdBetFfMacScheduler::~FdBetFfMacScheduler()
{
    NS_LOG_FUNCTION(this);
}

void
FdBetFfMacScheduler::DoDispose()
{
    NS_LOG_FUNCTION(this);
    m_dlHarqProcessesDciBuffer.clear();
    m_dlHarqProcessesTimer.clear();
    m_dlHarqProcessesRlcPduListBuffer.clear();
    m_dlInfoListBuffered.clear();
    m_ulHarqCurrentProcessId.clear();
    m_ulHarqProcessesStatus.clear();
    m_ulHarqProcessesDciBuffer.clear();
    delete m_cschedSapProvider;
    delete m_schedSapProvider;
}

TypeId
FdBetFfMacScheduler::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::FdBetFfMacScheduler")
            .SetParent<FfMacScheduler>()
            .SetGroupName("Lte")
            .AddConstructor<FdBetFfMacScheduler>()
            .AddAttribute("CqiTimerThreshold",
                          "The number of TTIs a CQI is valid (default 1000 - 1 sec.)",
                          UintegerValue(1000),
                          MakeUintegerAccessor(&FdBetFfMacScheduler::m_cqiTimersThreshold),
                          MakeUintegerChecker<uint32_t>())
            .AddAttribute("HarqEnabled",
                          "Activate/Deactivate the HARQ [by default is active].",
                          BooleanValue(true),
                          MakeBooleanAccessor(&FdBetFfMacScheduler::m_harqOn),
                          MakeBooleanChecker())
            .AddAttribute("UlGrantMcs",
                          "The MCS of the UL grant, must be [0..15] (default 0)",
                          UintegerValue(0),
                          MakeUintegerAccessor(&FdBetFfMacScheduler::m_ulGrantMcs),
                          MakeUintegerChecker<uint8_t>());
    return tid;
}

void
FdBetFfMacScheduler::SetFfMacCschedSapUser(FfMacCschedSapUser* s)
{
    m_cschedSapUser = s;
}

void
FdBetFfMacScheduler::SetFfMacSchedSapUser(FfMacSchedSapUser* s)
{
    m_schedSapUser = s;
}

FfMacCschedSapProvider*
FdBetFfMacScheduler::GetFfMacCschedSapProvider()
{
    return m_cschedSapProvider;
}

FfMacSchedSapProvider*
FdBetFfMacScheduler::GetFfMacSchedSapProvider()
{
    return m_schedSapProvider;
}

void
FdBetFfMacScheduler::SetLteFfrSapProvider(LteFfrSapProvider* s)
{
    m_ffrSapProvider = s;
}

LteFfrSapUser*
FdBetFfMacScheduler::GetLteFfrSapUser()
{
    return m_ffrSapUser;
}

void
FdBetFfMacScheduler::DoCschedCellConfigReq(
    const FfMacCschedSapProvider::CschedCellConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    // Read the subset of parameters used
    m_cschedCellConfig = params;
    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);
    FfMacCschedSapUser::CschedUeConfigCnfParameters cnf;
    cnf.m_result = SUCCESS;
    m_cschedSapUser->CschedUeConfigCnf(cnf);
}

void
FdBetFfMacScheduler::DoCschedUeConfigReq(
    const FfMacCschedSapProvider::CschedUeConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this << " RNTI " << params.m_rnti << " txMode "
                         << (uint16_t)params.m_transmissionMode);
    auto it = m_uesTxMode.find(params.m_rnti);
    if (it == m_uesTxMode.end())
    {
        m_uesTxMode.insert(std::pair<uint16_t, double>(params.m_rnti, params.m_transmissionMode));
        // generate HARQ buffers
        m_dlHarqCurrentProcessId.insert(std::pair<uint16_t, uint8_t>(params.m_rnti, 0));
        DlHarqProcessesStatus_t dlHarqPrcStatus;
        dlHarqPrcStatus.resize(8, 0);
        m_dlHarqProcessesStatus.insert(
            std::pair<uint16_t, DlHarqProcessesStatus_t>(params.m_rnti, dlHarqPrcStatus));
        DlHarqProcessesTimer_t dlHarqProcessesTimer;
        dlHarqProcessesTimer.resize(8, 0);
        m_dlHarqProcessesTimer.insert(
            std::pair<uint16_t, DlHarqProcessesTimer_t>(params.m_rnti, dlHarqProcessesTimer));
        DlHarqProcessesDciBuffer_t dlHarqdci;
        dlHarqdci.resize(8);
        m_dlHarqProcessesDciBuffer.insert(
            std::pair<uint16_t, DlHarqProcessesDciBuffer_t>(params.m_rnti, dlHarqdci));
        DlHarqRlcPduListBuffer_t dlHarqRlcPdu;
        dlHarqRlcPdu.resize(2);
        dlHarqRlcPdu.at(0).resize(8);
        dlHarqRlcPdu.at(1).resize(8);
        m_dlHarqProcessesRlcPduListBuffer.insert(
            std::pair<uint16_t, DlHarqRlcPduListBuffer_t>(params.m_rnti, dlHarqRlcPdu));
        m_ulHarqCurrentProcessId.insert(std::pair<uint16_t, uint8_t>(params.m_rnti, 0));
        UlHarqProcessesStatus_t ulHarqPrcStatus;
        ulHarqPrcStatus.resize(8, 0);
        m_ulHarqProcessesStatus.insert(
            std::pair<uint16_t, UlHarqProcessesStatus_t>(params.m_rnti, ulHarqPrcStatus));
        UlHarqProcessesDciBuffer_t ulHarqdci;
        ulHarqdci.resize(8);
        m_ulHarqProcessesDciBuffer.insert(
            std::pair<uint16_t, UlHarqProcessesDciBuffer_t>(params.m_rnti, ulHarqdci));
    }
    else
    {
        (*it).second = params.m_transmissionMode;
    }
}

void
FdBetFfMacScheduler::DoCschedLcConfigReq(
    const FfMacCschedSapProvider::CschedLcConfigReqParameters& params)
{
    NS_LOG_FUNCTION(this << " New LC, rnti: " << params.m_rnti);

    for (std::size_t i = 0; i < params.m_logicalChannelConfigList.size(); i++)
    {
        auto it = m_flowStatsDl.find(params.m_rnti);

        if (it == m_flowStatsDl.end())
        {
            fdbetsFlowPerf_t flowStatsDl;
            flowStatsDl.flowStart = Simulator::Now();
            flowStatsDl.totalBytesTransmitted = 0;
            flowStatsDl.lastTtiBytesTrasmitted = 0;
            flowStatsDl.lastAveragedThroughput = 1;
            m_flowStatsDl.insert(std::pair<uint16_t, fdbetsFlowPerf_t>(params.m_rnti, flowStatsDl));
            fdbetsFlowPerf_t flowStatsUl;
            flowStatsUl.flowStart = Simulator::Now();
            flowStatsUl.totalBytesTransmitted = 0;
            flowStatsUl.lastTtiBytesTrasmitted = 0;
            flowStatsUl.lastAveragedThroughput = 1;
            m_flowStatsUl.insert(std::pair<uint16_t, fdbetsFlowPerf_t>(params.m_rnti, flowStatsUl));
        }
    }
}

void
FdBetFfMacScheduler::DoCschedLcReleaseReq(
    const FfMacCschedSapProvider::CschedLcReleaseReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    for (std::size_t i = 0; i < params.m_logicalChannelIdentity.size(); i++)
    {
        auto it = m_rlcBufferReq.begin();
        while (it != m_rlcBufferReq.end())
        {
            if (((*it).first.m_rnti == params.m_rnti) &&
                ((*it).first.m_lcId == params.m_logicalChannelIdentity.at(i)))
            {
                auto temp = it;
                it++;
                m_rlcBufferReq.erase(temp);
            }
            else
            {
                it++;
            }
        }
    }
}

void
FdBetFfMacScheduler::DoCschedUeReleaseReq(
    const FfMacCschedSapProvider::CschedUeReleaseReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    m_uesTxMode.erase(params.m_rnti);
    m_dlHarqCurrentProcessId.erase(params.m_rnti);
    m_dlHarqProcessesStatus.erase(params.m_rnti);
    m_dlHarqProcessesTimer.erase(params.m_rnti);
    m_dlHarqProcessesDciBuffer.erase(params.m_rnti);
    m_dlHarqProcessesRlcPduListBuffer.erase(params.m_rnti);
    m_ulHarqCurrentProcessId.erase(params.m_rnti);
    m_ulHarqProcessesStatus.erase(params.m_rnti);
    m_ulHarqProcessesDciBuffer.erase(params.m_rnti);
    m_flowStatsDl.erase(params.m_rnti);
    m_flowStatsUl.erase(params.m_rnti);
    m_ceBsrRxed.erase(params.m_rnti);
    auto it = m_rlcBufferReq.begin();
    while (it != m_rlcBufferReq.end())
    {
        if ((*it).first.m_rnti == params.m_rnti)
        {
            auto temp = it;
            it++;
            m_rlcBufferReq.erase(temp);
        }
        else
        {
            it++;
        }
    }
    if (m_nextRntiUl == params.m_rnti)
    {
        m_nextRntiUl = 0;
    }
}

void
FdBetFfMacScheduler::DoSchedDlRlcBufferReq(
    const FfMacSchedSapProvider::SchedDlRlcBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this << params.m_rnti << (uint32_t)params.m_logicalChannelIdentity);
    // API generated by RLC for updating RLC parameters on a LC (tx and retx queues)

    LteFlowId_t flow(params.m_rnti, params.m_logicalChannelIdentity);

    auto it = m_rlcBufferReq.find(flow);

    if (it == m_rlcBufferReq.end())
    {
        m_rlcBufferReq.insert(
            std::pair<LteFlowId_t, FfMacSchedSapProvider::SchedDlRlcBufferReqParameters>(flow,
                                                                                         params));
    }
    else
    {
        (*it).second = params;
    }
}

void
FdBetFfMacScheduler::DoSchedDlPagingBufferReq(
    const FfMacSchedSapProvider::SchedDlPagingBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("method not implemented");
}

void
FdBetFfMacScheduler::DoSchedDlMacBufferReq(
    const FfMacSchedSapProvider::SchedDlMacBufferReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    NS_FATAL_ERROR("method not implemented");
}

int
FdBetFfMacScheduler::GetRbgSize(int dlbandwidth)
{
    for (int i = 0; i < 4; i++)
    {
        if (dlbandwidth < FdBetType0AllocationRbg[i])
        {
            return (i + 1);
        }
    }

    return (-1);
}

unsigned int
FdBetFfMacScheduler::LcActivePerFlow(uint16_t rnti)
{
    unsigned int lcActive = 0;
    for (auto it = m_rlcBufferReq.begin(); it != m_rlcBufferReq.end(); it++)
    {
        if (((*it).first.m_rnti == rnti) && (((*it).second.m_rlcTransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcRetransmissionQueueSize > 0) ||
                                             ((*it).second.m_rlcStatusPduSize > 0)))
        {
            lcActive++;
        }
        if ((*it).first.m_rnti > rnti)
        {
            break;
        }
    }
    return (lcActive);
}

bool
FdBetFfMacScheduler::HarqProcessAvailability(uint16_t rnti)
{
    NS_LOG_FUNCTION(this << rnti);

    auto it = m_dlHarqCurrentProcessId.find(rnti);
    if (it == m_dlHarqCurrentProcessId.end())
    {
        NS_FATAL_ERROR("No Process Id found for this RNTI " << rnti);
    }
    auto itStat = m_dlHarqProcessesStatus.find(rnti);
    if (itStat == m_dlHarqProcessesStatus.end())
    {
        NS_FATAL_ERROR("No Process Id Statusfound for this RNTI " << rnti);
    }
    uint8_t i = (*it).second;
    do
    {
        i = (i + 1) % HARQ_PROC_NUM;
    } while (((*itStat).second.at(i) != 0) && (i != (*it).second));

    return (*itStat).second.at(i) == 0;
}

uint8_t
FdBetFfMacScheduler::UpdateHarqProcessId(uint16_t rnti)
{
    NS_LOG_FUNCTION(this << rnti);

    if (!m_harqOn)
    {
        return (0);
    }

    auto it = m_dlHarqCurrentProcessId.find(rnti);
    if (it == m_dlHarqCurrentProcessId.end())
    {
        NS_FATAL_ERROR("No Process Id found for this RNTI " << rnti);
    }
    auto itStat = m_dlHarqProcessesStatus.find(rnti);
    if (itStat == m_dlHarqProcessesStatus.end())
    {
        NS_FATAL_ERROR("No Process Id Statusfound for this RNTI " << rnti);
    }
    uint8_t i = (*it).second;
    do
    {
        i = (i + 1) % HARQ_PROC_NUM;
    } while (((*itStat).second.at(i) != 0) && (i != (*it).second));
    if ((*itStat).second.at(i) == 0)
    {
        (*it).second = i;
        (*itStat).second.at(i) = 1;
    }
    else
    {
        NS_FATAL_ERROR("No HARQ process available for RNTI "
                       << rnti << " check before update with HarqProcessAvailability");
    }

    return ((*it).second);
}

void
FdBetFfMacScheduler::RefreshHarqProcesses()
{
    NS_LOG_FUNCTION(this);

    for (auto itTimers = m_dlHarqProcessesTimer.begin(); itTimers != m_dlHarqProcessesTimer.end();
         itTimers++)
    {
        for (uint16_t i = 0; i < HARQ_PROC_NUM; i++)
        {
            if ((*itTimers).second.at(i) == HARQ_DL_TIMEOUT)
            {
                // reset HARQ process

                NS_LOG_DEBUG(this << " Reset HARQ proc " << i << " for RNTI " << (*itTimers).first);
                auto itStat = m_dlHarqProcessesStatus.find((*itTimers).first);
                if (itStat == m_dlHarqProcessesStatus.end())
                {
                    NS_FATAL_ERROR("No Process Id Status found for this RNTI "
                                   << (*itTimers).first);
                }
                (*itStat).second.at(i) = 0;
                (*itTimers).second.at(i) = 0;
            }
            else
            {
                (*itTimers).second.at(i)++;
            }
        }
    }
}

void
FdBetFfMacScheduler::DoSchedDlTriggerReq(
    const FfMacSchedSapProvider::SchedDlTriggerReqParameters& params)
{
    NS_LOG_FUNCTION(this << " Frame no. " << (params.m_sfnSf >> 4) << " subframe no. "
                         << (0xF & params.m_sfnSf));
    // API generated by RLC for triggering the scheduling of a DL subframe

    // evaluate the relative channel quality indicator for each UE per each RBG
    // (since we are using allocation type 0 the small unit of allocation is RBG)
    // Resource allocation type 0 (see sec 7.1.6.1 of 36.213)

    RefreshDlCqiMaps();

    int rbgSize = GetRbgSize(m_cschedCellConfig.m_dlBandwidth);
    int rbgNum = m_cschedCellConfig.m_dlBandwidth / rbgSize;
    std::map<uint16_t, std::vector<uint16_t>> allocationMap; // RBs map per RNTI
    std::vector<bool> rbgMap;                                // global RBGs map
    uint16_t rbgAllocatedNum = 0;
    std::set<uint16_t> rntiAllocated;
    rbgMap.resize(m_cschedCellConfig.m_dlBandwidth / rbgSize, false);
    FfMacSchedSapUser::SchedDlConfigIndParameters ret;

    // update UL HARQ proc id
    for (auto itProcId = m_ulHarqCurrentProcessId.begin();
         itProcId != m_ulHarqCurrentProcessId.end();
         itProcId++)
    {
        (*itProcId).second = ((*itProcId).second + 1) % HARQ_PROC_NUM;
    }

    // RACH Allocation
    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);
    uint16_t rbStart = 0;
    for (auto itRach = m_rachList.begin(); itRach != m_rachList.end(); itRach++)
    {
        NS_ASSERT_MSG(m_amc->GetUlTbSizeFromMcs(m_ulGrantMcs, m_cschedCellConfig.m_ulBandwidth) >
                          (*itRach).m_estimatedSize,
                      " Default UL Grant MCS does not allow to send RACH messages");
        BuildRarListElement_s newRar;
        newRar.m_rnti = (*itRach).m_rnti;
        // DL-RACH Allocation
        // Ideal: no needs of configuring m_dci
        // UL-RACH Allocation
        newRar.m_grant.m_rnti = newRar.m_rnti;
        newRar.m_grant.m_mcs = m_ulGrantMcs;
        uint16_t rbLen = 1;
        uint16_t tbSizeBits = 0;
        // find lowest TB size that fits UL grant estimated size
        while ((tbSizeBits < (*itRach).m_estimatedSize) &&
               (rbStart + rbLen < m_cschedCellConfig.m_ulBandwidth))
        {
            rbLen++;
            tbSizeBits = m_amc->GetUlTbSizeFromMcs(m_ulGrantMcs, rbLen);
        }
        if (tbSizeBits < (*itRach).m_estimatedSize)
        {
            // no more allocation space: finish allocation
            break;
        }
        newRar.m_grant.m_rbStart = rbStart;
        newRar.m_grant.m_rbLen = rbLen;
        newRar.m_grant.m_tbSize = tbSizeBits / 8;
        newRar.m_grant.m_hopping = false;
        newRar.m_grant.m_tpc = 0;
        newRar.m_grant.m_cqiRequest = false;
        newRar.m_grant.m_ulDelay = false;
        NS_LOG_INFO(this << " UL grant allocated to RNTI " << (*itRach).m_rnti << " rbStart "
                         << rbStart << " rbLen " << rbLen << " MCS " << m_ulGrantMcs << " tbSize "
                         << newRar.m_grant.m_tbSize);
        for (uint16_t i = rbStart; i < rbStart + rbLen; i++)
        {
            m_rachAllocationMap.at(i) = (*itRach).m_rnti;
        }

        if (m_harqOn)
        {
            // generate UL-DCI for HARQ retransmissions
            UlDciListElement_s uldci;
            uldci.m_rnti = newRar.m_rnti;
            uldci.m_rbLen = rbLen;
            uldci.m_rbStart = rbStart;
            uldci.m_mcs = m_ulGrantMcs;
            uldci.m_tbSize = tbSizeBits / 8;
            uldci.m_ndi = 1;
            uldci.m_cceIndex = 0;
            uldci.m_aggrLevel = 1;
            uldci.m_ueTxAntennaSelection = 3; // antenna selection OFF
            uldci.m_hopping = false;
            uldci.m_n2Dmrs = 0;
            uldci.m_tpc = 0;            // no power control
            uldci.m_cqiRequest = false; // only period CQI at this stage
            uldci.m_ulIndex = 0;        // TDD parameter
            uldci.m_dai = 1;            // TDD parameter
            uldci.m_freqHopping = 0;
            uldci.m_pdcchPowerOffset = 0; // not used

            uint8_t harqId = 0;
            auto itProcId = m_ulHarqCurrentProcessId.find(uldci.m_rnti);
            if (itProcId == m_ulHarqCurrentProcessId.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << uldci.m_rnti);
            }
            harqId = (*itProcId).second;
            auto itDci = m_ulHarqProcessesDciBuffer.find(uldci.m_rnti);
            if (itDci == m_ulHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in UL DCI HARQ buffer for RNTI "
                               << uldci.m_rnti);
            }
            (*itDci).second.at(harqId) = uldci;
        }

        rbStart = rbStart + rbLen;
        ret.m_buildRarList.push_back(newRar);
    }
    m_rachList.clear();

    // Process DL HARQ feedback
    RefreshHarqProcesses();
    // retrieve past HARQ retx buffered
    if (!m_dlInfoListBuffered.empty())
    {
        if (!params.m_dlInfoList.empty())
        {
            NS_LOG_INFO(this << " Received DL-HARQ feedback");
            m_dlInfoListBuffered.insert(m_dlInfoListBuffered.end(),
                                        params.m_dlInfoList.begin(),
                                        params.m_dlInfoList.end());
        }
    }
    else
    {
        if (!params.m_dlInfoList.empty())
        {
            m_dlInfoListBuffered = params.m_dlInfoList;
        }
    }
    if (!m_harqOn)
    {
        // Ignore HARQ feedback
        m_dlInfoListBuffered.clear();
    }
    std::vector<DlInfoListElement_s> dlInfoListUntxed;
    for (std::size_t i = 0; i < m_dlInfoListBuffered.size(); i++)
    {
        auto itRnti = rntiAllocated.find(m_dlInfoListBuffered.at(i).m_rnti);
        if (itRnti != rntiAllocated.end())
        {
            // RNTI already allocated for retx
            continue;
        }
        auto nLayers = m_dlInfoListBuffered.at(i).m_harqStatus.size();
        std::vector<bool> retx;
        NS_LOG_INFO(this << " Processing DLHARQ feedback");
        if (nLayers == 1)
        {
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(0) ==
                           DlInfoListElement_s::NACK);
            retx.push_back(false);
        }
        else
        {
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(0) ==
                           DlInfoListElement_s::NACK);
            retx.push_back(m_dlInfoListBuffered.at(i).m_harqStatus.at(1) ==
                           DlInfoListElement_s::NACK);
        }
        if (retx.at(0) || retx.at(1))
        {
            // retrieve HARQ process information
            uint16_t rnti = m_dlInfoListBuffered.at(i).m_rnti;
            uint8_t harqId = m_dlInfoListBuffered.at(i).m_harqProcessId;
            NS_LOG_INFO(this << " HARQ retx RNTI " << rnti << " harqId " << (uint16_t)harqId);
            auto itHarq = m_dlHarqProcessesDciBuffer.find(rnti);
            if (itHarq == m_dlHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << rnti);
            }

            DlDciListElement_s dci = (*itHarq).second.at(harqId);
            int rv = 0;
            if (dci.m_rv.size() == 1)
            {
                rv = dci.m_rv.at(0);
            }
            else
            {
                rv = (dci.m_rv.at(0) > dci.m_rv.at(1) ? dci.m_rv.at(0) : dci.m_rv.at(1));
            }

            if (rv == 3)
            {
                // maximum number of retx reached -> drop process
                NS_LOG_INFO("Maximum number of retransmissions reached -> drop process");
                auto it = m_dlHarqProcessesStatus.find(rnti);
                if (it == m_dlHarqProcessesStatus.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) "
                                 << m_dlInfoListBuffered.at(i).m_rnti);
                }
                (*it).second.at(harqId) = 0;
                auto itRlcPdu = m_dlHarqProcessesRlcPduListBuffer.find(rnti);
                if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
                {
                    NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                                   << m_dlInfoListBuffered.at(i).m_rnti);
                }
                for (std::size_t k = 0; k < (*itRlcPdu).second.size(); k++)
                {
                    (*itRlcPdu).second.at(k).at(harqId).clear();
                }
                continue;
            }
            // check the feasibility of retransmitting on the same RBGs
            // translate the DCI to Spectrum framework
            std::vector<int> dciRbg;
            uint32_t mask = 0x1;
            NS_LOG_INFO("Original RBGs " << dci.m_rbBitmap << " rnti " << dci.m_rnti);
            for (int j = 0; j < 32; j++)
            {
                if (((dci.m_rbBitmap & mask) >> j) == 1)
                {
                    dciRbg.push_back(j);
                    NS_LOG_INFO("\t" << j);
                }
                mask = (mask << 1);
            }
            bool free = true;
            for (std::size_t j = 0; j < dciRbg.size(); j++)
            {
                if (rbgMap.at(dciRbg.at(j)))
                {
                    free = false;
                    break;
                }
            }
            if (free)
            {
                // use the same RBGs for the retx
                // reserve RBGs
                for (std::size_t j = 0; j < dciRbg.size(); j++)
                {
                    rbgMap.at(dciRbg.at(j)) = true;
                    NS_LOG_INFO("RBG " << dciRbg.at(j) << " assigned");
                    rbgAllocatedNum++;
                }

                NS_LOG_INFO(this << " Send retx in the same RBGs");
            }
            else
            {
                // find RBGs for sending HARQ retx
                uint8_t j = 0;
                uint8_t rbgId = (dciRbg.at(dciRbg.size() - 1) + 1) % rbgNum;
                uint8_t startRbg = dciRbg.at(dciRbg.size() - 1);
                std::vector<bool> rbgMapCopy = rbgMap;
                while ((j < dciRbg.size()) && (startRbg != rbgId))
                {
                    if (!rbgMapCopy.at(rbgId))
                    {
                        rbgMapCopy.at(rbgId) = true;
                        dciRbg.at(j) = rbgId;
                        j++;
                    }
                    rbgId = (rbgId + 1) % rbgNum;
                }
                if (j == dciRbg.size())
                {
                    // find new RBGs -> update DCI map
                    uint32_t rbgMask = 0;
                    for (std::size_t k = 0; k < dciRbg.size(); k++)
                    {
                        rbgMask = rbgMask + (0x1 << dciRbg.at(k));
                        rbgAllocatedNum++;
                    }
                    dci.m_rbBitmap = rbgMask;
                    rbgMap = rbgMapCopy;
                    NS_LOG_INFO(this << " Move retx in RBGs " << dciRbg.size());
                }
                else
                {
                    // HARQ retx cannot be performed on this TTI -> store it
                    dlInfoListUntxed.push_back(m_dlInfoListBuffered.at(i));
                    NS_LOG_INFO(this << " No resource for this retx -> buffer it");
                }
            }
            // retrieve RLC PDU list for retx TBsize and update DCI
            BuildDataListElement_s newEl;
            auto itRlcPdu = m_dlHarqProcessesRlcPduListBuffer.find(rnti);
            if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI " << rnti);
            }
            for (std::size_t j = 0; j < nLayers; j++)
            {
                if (retx.at(j))
                {
                    if (j >= dci.m_ndi.size())
                    {
                        // for avoiding errors in MIMO transient phases
                        dci.m_ndi.push_back(0);
                        dci.m_rv.push_back(0);
                        dci.m_mcs.push_back(0);
                        dci.m_tbsSize.push_back(0);
                        NS_LOG_INFO(this << " layer " << (uint16_t)j
                                         << " no txed (MIMO transition)");
                    }
                    else
                    {
                        dci.m_ndi.at(j) = 0;
                        dci.m_rv.at(j)++;
                        (*itHarq).second.at(harqId).m_rv.at(j)++;
                        NS_LOG_INFO(this << " layer " << (uint16_t)j << " RV "
                                         << (uint16_t)dci.m_rv.at(j));
                    }
                }
                else
                {
                    // empty TB of layer j
                    dci.m_ndi.at(j) = 0;
                    dci.m_rv.at(j) = 0;
                    dci.m_mcs.at(j) = 0;
                    dci.m_tbsSize.at(j) = 0;
                    NS_LOG_INFO(this << " layer " << (uint16_t)j << " no retx");
                }
            }
            for (std::size_t k = 0; k < (*itRlcPdu).second.at(0).at(dci.m_harqProcess).size(); k++)
            {
                std::vector<RlcPduListElement_s> rlcPduListPerLc;
                for (std::size_t j = 0; j < nLayers; j++)
                {
                    if (retx.at(j))
                    {
                        if (j < dci.m_ndi.size())
                        {
                            NS_LOG_INFO(" layer " << (uint16_t)j << " tb size "
                                                  << dci.m_tbsSize.at(j));
                            rlcPduListPerLc.push_back(
                                (*itRlcPdu).second.at(j).at(dci.m_harqProcess).at(k));
                        }
                    }
                    else
                    { // if no retx needed on layer j, push an RlcPduListElement_s object with
                      // m_size=0 to keep the size of rlcPduListPerLc vector = 2 in case of MIMO
                        NS_LOG_INFO(" layer " << (uint16_t)j << " tb size " << dci.m_tbsSize.at(j));
                        RlcPduListElement_s emptyElement;
                        emptyElement.m_logicalChannelIdentity = (*itRlcPdu)
                                                                    .second.at(j)
                                                                    .at(dci.m_harqProcess)
                                                                    .at(k)
                                                                    .m_logicalChannelIdentity;
                        emptyElement.m_size = 0;
                        rlcPduListPerLc.push_back(emptyElement);
                    }
                }

                if (!rlcPduListPerLc.empty())
                {
                    newEl.m_rlcPduList.push_back(rlcPduListPerLc);
                }
            }
            newEl.m_rnti = rnti;
            newEl.m_dci = dci;
            (*itHarq).second.at(harqId).m_rv = dci.m_rv;
            // refresh timer
            auto itHarqTimer = m_dlHarqProcessesTimer.find(rnti);
            if (itHarqTimer == m_dlHarqProcessesTimer.end())
            {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)rnti);
            }
            (*itHarqTimer).second.at(harqId) = 0;
            ret.m_buildDataList.push_back(newEl);
            rntiAllocated.insert(rnti);
        }
        else
        {
            // update HARQ process status
            NS_LOG_INFO(this << " HARQ received ACK for UE " << m_dlInfoListBuffered.at(i).m_rnti);
            auto it = m_dlHarqProcessesStatus.find(m_dlInfoListBuffered.at(i).m_rnti);
            if (it == m_dlHarqProcessesStatus.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE "
                               << m_dlInfoListBuffered.at(i).m_rnti);
            }
            (*it).second.at(m_dlInfoListBuffered.at(i).m_harqProcessId) = 0;
            auto itRlcPdu =
                m_dlHarqProcessesRlcPduListBuffer.find(m_dlInfoListBuffered.at(i).m_rnti);
            if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                               << m_dlInfoListBuffered.at(i).m_rnti);
            }
            for (std::size_t k = 0; k < (*itRlcPdu).second.size(); k++)
            {
                (*itRlcPdu).second.at(k).at(m_dlInfoListBuffered.at(i).m_harqProcessId).clear();
            }
        }
    }
    m_dlInfoListBuffered.clear();
    m_dlInfoListBuffered = dlInfoListUntxed;

    if (rbgAllocatedNum == rbgNum)
    {
        // all the RBGs are already allocated -> exit
        if (!ret.m_buildDataList.empty() || !ret.m_buildRarList.empty())
        {
            m_schedSapUser->SchedDlConfigInd(ret);
        }
        return;
    }

    std::map<uint16_t, double> estAveThr; // store expected average throughput for UE
    auto itMax = estAveThr.end();
    std::map<uint16_t, int> rbgPerRntiLog; // record the number of RBG assigned to UE
    double metricMax = 0.0;
    for (auto itFlow = m_flowStatsDl.begin(); itFlow != m_flowStatsDl.end(); itFlow++)
    {
        auto itRnti = rntiAllocated.find((*itFlow).first);
        if ((itRnti != rntiAllocated.end()) || (!HarqProcessAvailability((*itFlow).first)))
        {
            // UE already allocated for HARQ or without HARQ process available -> drop it
            if (itRnti != rntiAllocated.end())
            {
                NS_LOG_DEBUG(this << " RNTI discarded for HARQ tx" << (uint16_t)(*itFlow).first);
            }
            if (!HarqProcessAvailability((*itFlow).first))
            {
                NS_LOG_DEBUG(this << " RNTI discarded for HARQ id" << (uint16_t)(*itFlow).first);
            }
            continue;
        }

        // check first what are channel conditions for this UE, if CQI!=0
        auto itCqi = m_p10CqiRxed.find((*itFlow).first);
        auto itTxMode = m_uesTxMode.find((*itFlow).first);
        if (itTxMode == m_uesTxMode.end())
        {
            NS_FATAL_ERROR("No Transmission Mode info on user " << (*itFlow).first);
        }
        auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);

        uint8_t cqiSum = 0;
        for (uint8_t j = 0; j < nLayer; j++)
        {
            if (itCqi == m_p10CqiRxed.end())
            {
                cqiSum += 1; // no info on this user -> lowest MCS
            }
            else
            {
                cqiSum = (*itCqi).second;
            }
        }
        if (cqiSum != 0)
        {
            estAveThr.insert(std::pair<uint16_t, double>((*itFlow).first,
                                                         (*itFlow).second.lastAveragedThroughput));
        }
        else
        {
            NS_LOG_INFO("Skip this flow, CQI==0, rnti:" << (*itFlow).first);
        }
    }

    if (!estAveThr.empty())
    {
        // Find UE with largest priority metric
        for (auto it = estAveThr.begin(); it != estAveThr.end(); it++)
        {
            double metric = 1 / (*it).second;
            if (metric > metricMax)
            {
                metricMax = metric;
                itMax = it;
            }
            rbgPerRntiLog.insert(std::pair<uint16_t, int>((*it).first, 1));
        }

        // The scheduler tries the best to achieve the equal throughput among all UEs
        int i = 0;
        do
        {
            NS_LOG_INFO(this << " ALLOCATION for RBG " << i << " of " << rbgNum);
            if (!rbgMap.at(i))
            {
                // allocate one RBG to current UE
                std::vector<uint16_t> tempMap;
                auto itMap = allocationMap.find((*itMax).first);
                if (itMap == allocationMap.end())
                {
                    tempMap.push_back(i);
                    allocationMap.insert(
                        std::pair<uint16_t, std::vector<uint16_t>>((*itMax).first, tempMap));
                }
                else
                {
                    (*itMap).second.push_back(i);
                }

                // calculate expected throughput for current UE
                auto itCqi = m_p10CqiRxed.find((*itMax).first);
                auto itTxMode = m_uesTxMode.find((*itMax).first);
                if (itTxMode == m_uesTxMode.end())
                {
                    NS_FATAL_ERROR("No Transmission Mode info on user " << (*itMax).first);
                }
                auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);
                std::vector<uint8_t> mcs;
                for (uint8_t j = 0; j < nLayer; j++)
                {
                    if (itCqi == m_p10CqiRxed.end())
                    {
                        mcs.push_back(0); // no info on this user -> lowest MCS
                    }
                    else
                    {
                        mcs.push_back(m_amc->GetMcsFromCqi((*itCqi).second));
                    }
                }

                auto itRbgPerRntiLog = rbgPerRntiLog.find((*itMax).first);
                auto itPastAveThr = m_flowStatsDl.find((*itMax).first);
                uint32_t bytesTxed = 0;
                for (uint8_t j = 0; j < nLayer; j++)
                {
                    int tbSize =
                        (m_amc->GetDlTbSizeFromMcs(mcs.at(0), (*itRbgPerRntiLog).second * rbgSize) /
                         8); // (size of TB in bytes according to table 7.1.7.2.1-1 of 36.213)
                    bytesTxed += tbSize;
                }
                double expectedAveThr =
                    ((1.0 - (1.0 / m_timeWindow)) * (*itPastAveThr).second.lastAveragedThroughput) +
                    ((1.0 / m_timeWindow) * (double)(bytesTxed / 0.001));

                int rbgPerRnti = (*itRbgPerRntiLog).second;
                rbgPerRnti++;
                rbgPerRntiLog[(*itMax).first] = rbgPerRnti;
                estAveThr[(*itMax).first] = expectedAveThr;

                // find new UE with largest priority metric
                metricMax = 0.0;
                for (auto it = estAveThr.begin(); it != estAveThr.end(); it++)
                {
                    double metric = 1 / (*it).second;
                    if (metric > metricMax)
                    {
                        itMax = it;
                        metricMax = metric;
                    }
                } // end for estAveThr

                rbgMap.at(i) = true;

            } // end for free RBGs

            i++;

        } while (i < rbgNum); // end for RBGs

    } // end if estAveThr

    // reset TTI stats of users
    for (auto itStats = m_flowStatsDl.begin(); itStats != m_flowStatsDl.end(); itStats++)
    {
        (*itStats).second.lastTtiBytesTrasmitted = 0;
    }

    // generate the transmission opportunities by grouping the RBGs of the same RNTI and
    // creating the correspondent DCIs
    auto itMap = allocationMap.begin();
    while (itMap != allocationMap.end())
    {
        // create new BuildDataListElement_s for this LC
        BuildDataListElement_s newEl;
        newEl.m_rnti = (*itMap).first;
        // create the DlDciListElement_s
        DlDciListElement_s newDci;
        newDci.m_rnti = (*itMap).first;
        newDci.m_harqProcess = UpdateHarqProcessId((*itMap).first);

        uint16_t lcActives = LcActivePerFlow((*itMap).first);
        NS_LOG_INFO(this << "Allocate user " << newEl.m_rnti << " rbg " << lcActives);
        if (lcActives == 0)
        {
            // Set to max value, to avoid divide by 0 below
            lcActives = (uint16_t)65535; // UINT16_MAX;
        }
        uint16_t RgbPerRnti = (*itMap).second.size();
        auto itCqi = m_p10CqiRxed.find((*itMap).first);
        auto itTxMode = m_uesTxMode.find((*itMap).first);
        if (itTxMode == m_uesTxMode.end())
        {
            NS_FATAL_ERROR("No Transmission Mode info on user " << (*itMap).first);
        }
        auto nLayer = TransmissionModesLayers::TxMode2LayerNum((*itTxMode).second);

        uint32_t bytesTxed = 0;
        for (uint8_t j = 0; j < nLayer; j++)
        {
            if (itCqi == m_p10CqiRxed.end())
            {
                newDci.m_mcs.push_back(0); // no info on this user -> lowest MCS
            }
            else
            {
                newDci.m_mcs.push_back(m_amc->GetMcsFromCqi((*itCqi).second));
            }

            int tbSize = (m_amc->GetDlTbSizeFromMcs(newDci.m_mcs.at(j), RgbPerRnti * rbgSize) /
                          8); // (size of TB in bytes according to table 7.1.7.2.1-1 of 36.213)
            newDci.m_tbsSize.push_back(tbSize);
            bytesTxed += tbSize;
        }

        newDci.m_resAlloc = 0; // only allocation type 0 at this stage
        newDci.m_rbBitmap = 0; // TBD (32 bit bitmap see 7.1.6 of 36.213)
        uint32_t rbgMask = 0;
        for (std::size_t k = 0; k < (*itMap).second.size(); k++)
        {
            rbgMask = rbgMask + (0x1 << (*itMap).second.at(k));
            NS_LOG_INFO(this << " Allocated RBG " << (*itMap).second.at(k));
        }
        newDci.m_rbBitmap = rbgMask; // (32 bit bitmap see 7.1.6 of 36.213)

        // create the rlc PDUs -> equally divide resources among actives LCs
        for (auto itBufReq = m_rlcBufferReq.begin(); itBufReq != m_rlcBufferReq.end(); itBufReq++)
        {
            if (((*itBufReq).first.m_rnti == (*itMap).first) &&
                (((*itBufReq).second.m_rlcTransmissionQueueSize > 0) ||
                 ((*itBufReq).second.m_rlcRetransmissionQueueSize > 0) ||
                 ((*itBufReq).second.m_rlcStatusPduSize > 0)))
            {
                std::vector<RlcPduListElement_s> newRlcPduLe;
                for (uint8_t j = 0; j < nLayer; j++)
                {
                    RlcPduListElement_s newRlcEl;
                    newRlcEl.m_logicalChannelIdentity = (*itBufReq).first.m_lcId;
                    newRlcEl.m_size = newDci.m_tbsSize.at(j) / lcActives;
                    NS_LOG_INFO(this << " LCID " << (uint32_t)newRlcEl.m_logicalChannelIdentity
                                     << " size " << newRlcEl.m_size << " layer " << (uint16_t)j);
                    newRlcPduLe.push_back(newRlcEl);
                    UpdateDlRlcBufferInfo(newDci.m_rnti,
                                          newRlcEl.m_logicalChannelIdentity,
                                          newRlcEl.m_size);
                    if (m_harqOn)
                    {
                        // store RLC PDU list for HARQ
                        auto itRlcPdu = m_dlHarqProcessesRlcPduListBuffer.find((*itMap).first);
                        if (itRlcPdu == m_dlHarqProcessesRlcPduListBuffer.end())
                        {
                            NS_FATAL_ERROR("Unable to find RlcPdcList in HARQ buffer for RNTI "
                                           << (*itMap).first);
                        }
                        (*itRlcPdu).second.at(j).at(newDci.m_harqProcess).push_back(newRlcEl);
                    }
                }
                newEl.m_rlcPduList.push_back(newRlcPduLe);
            }
            if ((*itBufReq).first.m_rnti > (*itMap).first)
            {
                break;
            }
        }
        for (uint8_t j = 0; j < nLayer; j++)
        {
            newDci.m_ndi.push_back(1);
            newDci.m_rv.push_back(0);
        }

        newDci.m_tpc = 1; // 1 is mapped to 0 in Accumulated Mode and to -1 in Absolute Mode

        newEl.m_dci = newDci;

        if (m_harqOn)
        {
            // store DCI for HARQ
            auto itDci = m_dlHarqProcessesDciBuffer.find(newEl.m_rnti);
            if (itDci == m_dlHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in DCI HARQ buffer for RNTI "
                               << newEl.m_rnti);
            }
            (*itDci).second.at(newDci.m_harqProcess) = newDci;
            // refresh timer
            auto itHarqTimer = m_dlHarqProcessesTimer.find(newEl.m_rnti);
            if (itHarqTimer == m_dlHarqProcessesTimer.end())
            {
                NS_FATAL_ERROR("Unable to find HARQ timer for RNTI " << (uint16_t)newEl.m_rnti);
            }
            (*itHarqTimer).second.at(newDci.m_harqProcess) = 0;
        }

        // ...more parameters -> ignored in this version

        ret.m_buildDataList.push_back(newEl);
        // update UE stats
        auto it = m_flowStatsDl.find((*itMap).first);
        if (it != m_flowStatsDl.end())
        {
            (*it).second.lastTtiBytesTrasmitted = bytesTxed;
            NS_LOG_INFO(this << " UE total bytes txed " << (*it).second.lastTtiBytesTrasmitted);
        }
        else
        {
            NS_FATAL_ERROR(this << " No Stats for this allocated UE");
        }

        itMap++;
    }                               // end while allocation
    ret.m_nrOfPdcchOfdmSymbols = 1; /// \todo check correct value according the DCIs txed

    // update UEs stats
    NS_LOG_INFO(this << " Update UEs statistics");
    for (auto itStats = m_flowStatsDl.begin(); itStats != m_flowStatsDl.end(); itStats++)
    {
        (*itStats).second.totalBytesTransmitted += (*itStats).second.lastTtiBytesTrasmitted;
        // update average throughput (see eq. 12.3 of Sec 12.3.1.2 of LTE – The UMTS Long Term
        // Evolution, Ed Wiley)
        (*itStats).second.lastAveragedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiBytesTrasmitted / 0.001));
        NS_LOG_INFO(this << " UE total bytes " << (*itStats).second.totalBytesTransmitted);
        NS_LOG_INFO(this << " UE average throughput " << (*itStats).second.lastAveragedThroughput);
        (*itStats).second.lastTtiBytesTrasmitted = 0;
    }

    m_schedSapUser->SchedDlConfigInd(ret);
}

void
FdBetFfMacScheduler::DoSchedDlRachInfoReq(
    const FfMacSchedSapProvider::SchedDlRachInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    m_rachList = params.m_rachList;
}

void
FdBetFfMacScheduler::DoSchedDlCqiInfoReq(
    const FfMacSchedSapProvider::SchedDlCqiInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    for (unsigned int i = 0; i < params.m_cqiList.size(); i++)
    {
        if (params.m_cqiList.at(i).m_cqiType == CqiListElement_s::P10)
        {
            NS_LOG_LOGIC("wideband CQI " << (uint32_t)params.m_cqiList.at(i).m_wbCqi.at(0)
                                         << " reported");
            uint16_t rnti = params.m_cqiList.at(i).m_rnti;
            auto it = m_p10CqiRxed.find(rnti);
            if (it == m_p10CqiRxed.end())
            {
                // create the new entry
                m_p10CqiRxed.insert(std::pair<uint16_t, uint8_t>(
                    rnti,
                    params.m_cqiList.at(i).m_wbCqi.at(0))); // only codeword 0 at this stage (SISO)
                // generate correspondent timer
                m_p10CqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
            }
            else
            {
                // update the CQI value and refresh correspondent timer
                (*it).second = params.m_cqiList.at(i).m_wbCqi.at(0);
                // update correspondent timer
                auto itTimers = m_p10CqiTimers.find(rnti);
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        else if (params.m_cqiList.at(i).m_cqiType == CqiListElement_s::A30)
        {
            // subband CQI reporting high layer configured
            uint16_t rnti = params.m_cqiList.at(i).m_rnti;
            auto it = m_a30CqiRxed.find(rnti);
            if (it == m_a30CqiRxed.end())
            {
                // create the new entry
                m_a30CqiRxed.insert(
                    std::pair<uint16_t, SbMeasResult_s>(rnti,
                                                        params.m_cqiList.at(i).m_sbMeasResult));
                m_a30CqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
            }
            else
            {
                // update the CQI value and refresh correspondent timer
                (*it).second = params.m_cqiList.at(i).m_sbMeasResult;
                auto itTimers = m_a30CqiTimers.find(rnti);
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        else
        {
            NS_LOG_ERROR(this << " CQI type unknown");
        }
    }
}

double
FdBetFfMacScheduler::EstimateUlSinr(uint16_t rnti, uint16_t rb)
{
    auto itCqi = m_ueCqi.find(rnti);
    if (itCqi == m_ueCqi.end())
    {
        // no cqi info about this UE
        return (NO_SINR);
    }
    else
    {
        // take the average SINR value among the available
        double sinrSum = 0;
        unsigned int sinrNum = 0;
        for (uint32_t i = 0; i < m_cschedCellConfig.m_ulBandwidth; i++)
        {
            double sinr = (*itCqi).second.at(i);
            if (sinr != NO_SINR)
            {
                sinrSum += sinr;
                sinrNum++;
            }
        }
        double estimatedSinr = (sinrNum > 0) ? (sinrSum / sinrNum) : DBL_MAX;
        // store the value
        (*itCqi).second.at(rb) = estimatedSinr;
        return (estimatedSinr);
    }
}

void
FdBetFfMacScheduler::DoSchedUlTriggerReq(
    const FfMacSchedSapProvider::SchedUlTriggerReqParameters& params)
{
    NS_LOG_FUNCTION(this << " UL - Frame no. " << (params.m_sfnSf >> 4) << " subframe no. "
                         << (0xF & params.m_sfnSf) << " size " << params.m_ulInfoList.size());

    RefreshUlCqiMaps();

    // Generate RBs map
    FfMacSchedSapUser::SchedUlConfigIndParameters ret;
    std::vector<bool> rbMap;
    std::set<uint16_t> rntiAllocated;
    std::vector<uint16_t> rbgAllocationMap;
    // update with RACH allocation map
    rbgAllocationMap = m_rachAllocationMap;
    // rbgAllocationMap.resize (m_cschedCellConfig.m_ulBandwidth, 0);
    m_rachAllocationMap.clear();
    m_rachAllocationMap.resize(m_cschedCellConfig.m_ulBandwidth, 0);

    rbMap.resize(m_cschedCellConfig.m_ulBandwidth, false);
    // remove RACH allocation
    for (uint16_t i = 0; i < m_cschedCellConfig.m_ulBandwidth; i++)
    {
        if (rbgAllocationMap.at(i) != 0)
        {
            rbMap.at(i) = true;
            NS_LOG_DEBUG(this << " Allocated for RACH " << i);
        }
    }

    if (m_harqOn)
    {
        //   Process UL HARQ feedback
        for (std::size_t i = 0; i < params.m_ulInfoList.size(); i++)
        {
            if (params.m_ulInfoList.at(i).m_receptionStatus == UlInfoListElement_s::NotOk)
            {
                // retx correspondent block: retrieve the UL-DCI
                uint16_t rnti = params.m_ulInfoList.at(i).m_rnti;
                auto itProcId = m_ulHarqCurrentProcessId.find(rnti);
                if (itProcId == m_ulHarqCurrentProcessId.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                }
                uint8_t harqId = (uint8_t)((*itProcId).second - HARQ_PERIOD) % HARQ_PROC_NUM;
                NS_LOG_INFO(this << " UL-HARQ retx RNTI " << rnti << " harqId " << (uint16_t)harqId
                                 << " i " << i << " size " << params.m_ulInfoList.size());
                auto itHarq = m_ulHarqProcessesDciBuffer.find(rnti);
                if (itHarq == m_ulHarqProcessesDciBuffer.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                    continue;
                }
                UlDciListElement_s dci = (*itHarq).second.at(harqId);
                auto itStat = m_ulHarqProcessesStatus.find(rnti);
                if (itStat == m_ulHarqProcessesStatus.end())
                {
                    NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) " << rnti);
                }
                if ((*itStat).second.at(harqId) >= 3)
                {
                    NS_LOG_INFO("Max number of retransmissions reached (UL)-> drop process");
                    continue;
                }
                bool free = true;
                for (int j = dci.m_rbStart; j < dci.m_rbStart + dci.m_rbLen; j++)
                {
                    if (rbMap.at(j))
                    {
                        free = false;
                        NS_LOG_INFO(this << " BUSY " << j);
                    }
                }
                if (free)
                {
                    // retx on the same RBs
                    for (int j = dci.m_rbStart; j < dci.m_rbStart + dci.m_rbLen; j++)
                    {
                        rbMap.at(j) = true;
                        rbgAllocationMap.at(j) = dci.m_rnti;
                        NS_LOG_INFO("\tRB " << j);
                    }
                    NS_LOG_INFO(this << " Send retx in the same RBs " << (uint16_t)dci.m_rbStart
                                     << " to " << dci.m_rbStart + dci.m_rbLen << " RV "
                                     << (*itStat).second.at(harqId) + 1);
                }
                else
                {
                    NS_LOG_INFO("Cannot allocate retx due to RACH allocations for UE " << rnti);
                    continue;
                }
                dci.m_ndi = 0;
                // Update HARQ buffers with new HarqId
                (*itStat).second.at((*itProcId).second) = (*itStat).second.at(harqId) + 1;
                (*itStat).second.at(harqId) = 0;
                (*itHarq).second.at((*itProcId).second) = dci;
                ret.m_dciList.push_back(dci);
                rntiAllocated.insert(dci.m_rnti);
            }
            else
            {
                NS_LOG_INFO(this << " HARQ-ACK feedback from RNTI "
                                 << params.m_ulInfoList.at(i).m_rnti);
            }
        }
    }

    std::map<uint16_t, uint32_t>::iterator it;
    int nflows = 0;

    for (it = m_ceBsrRxed.begin(); it != m_ceBsrRxed.end(); it++)
    {
        auto itRnti = rntiAllocated.find((*it).first);
        // select UEs with queues not empty and not yet allocated for HARQ
        if (((*it).second > 0) && (itRnti == rntiAllocated.end()))
        {
            nflows++;
        }
    }

    if (nflows == 0)
    {
        if (!ret.m_dciList.empty())
        {
            std::map<uint16_t, std::vector<uint16_t>>::iterator itMap;
            itMap = m_allocationMaps.find(params.m_sfnSf);
            if (itMap != m_allocationMaps.end())
            {
                // remove obsolete info on allocation first
                NS_LOG_DEBUG("Found SFnSF = " << params.m_sfnSf << " UL - Frame no. "
                                              << (params.m_sfnSf >> 4) << " subframe no. "
                                              << (0xF & params.m_sfnSf));
                m_allocationMaps.erase(itMap);
            }
            m_allocationMaps.insert(
                std::pair<uint16_t, std::vector<uint16_t>>(params.m_sfnSf, rbgAllocationMap));
            m_schedSapUser->SchedUlConfigInd(ret);
        }

        return; // no flows to be scheduled
    }

    // Divide the remaining resources equally among the active users starting from the subsequent
    // one served last scheduling trigger
    uint16_t rbPerFlow = (m_cschedCellConfig.m_ulBandwidth) / (nflows + rntiAllocated.size());
    if (rbPerFlow < 3)
    {
        rbPerFlow = 3; // at least 3 rbg per flow (till available resource) to ensure TxOpportunity
                       // >= 7 bytes
    }
    int rbAllocated = 0;

    if (m_nextRntiUl != 0)
    {
        for (it = m_ceBsrRxed.begin(); it != m_ceBsrRxed.end(); it++)
        {
            if ((*it).first == m_nextRntiUl)
            {
                break;
            }
        }
        if (it == m_ceBsrRxed.end())
        {
            NS_LOG_ERROR(this << " no user found");
        }
    }
    else
    {
        it = m_ceBsrRxed.begin();
        m_nextRntiUl = (*it).first;
    }
    do
    {
        auto itRnti = rntiAllocated.find((*it).first);
        if ((itRnti != rntiAllocated.end()) || ((*it).second == 0))
        {
            // UE already allocated for UL-HARQ -> skip it
            NS_LOG_DEBUG(this << " UE already allocated in HARQ -> discarded, RNTI "
                              << (*it).first);
            it++;
            if (it == m_ceBsrRxed.end())
            {
                // restart from the first
                it = m_ceBsrRxed.begin();
            }
            continue;
        }
        if (rbAllocated + rbPerFlow - 1 > m_cschedCellConfig.m_ulBandwidth)
        {
            // limit to physical resources last resource assignment
            rbPerFlow = m_cschedCellConfig.m_ulBandwidth - rbAllocated;
            // at least 3 rbg per flow to ensure TxOpportunity >= 7 bytes
            if (rbPerFlow < 3)
            {
                // terminate allocation
                rbPerFlow = 0;
            }
        }

        UlDciListElement_s uldci;
        uldci.m_rnti = (*it).first;
        uldci.m_rbLen = rbPerFlow;
        bool allocated = false;
        NS_LOG_INFO(this << " RB Allocated " << rbAllocated << " rbPerFlow " << rbPerFlow
                         << " flows " << nflows);
        while ((!allocated) && ((rbAllocated + rbPerFlow - m_cschedCellConfig.m_ulBandwidth) < 1) &&
               (rbPerFlow != 0))
        {
            // check availability
            bool free = true;
            for (int j = rbAllocated; j < rbAllocated + rbPerFlow; j++)
            {
                if (rbMap.at(j))
                {
                    free = false;
                    break;
                }
            }
            if (free)
            {
                uldci.m_rbStart = rbAllocated;

                for (int j = rbAllocated; j < rbAllocated + rbPerFlow; j++)
                {
                    rbMap.at(j) = true;
                    // store info on allocation for managing ul-cqi interpretation
                    rbgAllocationMap.at(j) = (*it).first;
                }
                rbAllocated += rbPerFlow;
                allocated = true;
                break;
            }
            rbAllocated++;
            if (rbAllocated + rbPerFlow - 1 > m_cschedCellConfig.m_ulBandwidth)
            {
                // limit to physical resources last resource assignment
                rbPerFlow = m_cschedCellConfig.m_ulBandwidth - rbAllocated;
                // at least 3 rbg per flow to ensure TxOpportunity >= 7 bytes
                if (rbPerFlow < 3)
                {
                    // terminate allocation
                    rbPerFlow = 0;
                }
            }
        }
        if (!allocated)
        {
            // unable to allocate new resource: finish scheduling
            m_nextRntiUl = (*it).first;
            if (!ret.m_dciList.empty())
            {
                m_schedSapUser->SchedUlConfigInd(ret);
            }
            std::map<uint16_t, std::vector<uint16_t>>::iterator itMap;
            itMap = m_allocationMaps.find(params.m_sfnSf);
            if (itMap != m_allocationMaps.end())
            {
                // remove obsolete info on allocation first
                NS_LOG_DEBUG("Found SFnSF = " << params.m_sfnSf << " UL - Frame no. "
                                              << (params.m_sfnSf >> 4) << " subframe no. "
                                              << (0xF & params.m_sfnSf));
                m_allocationMaps.erase(itMap);
            }
            m_allocationMaps.insert(
                std::pair<uint16_t, std::vector<uint16_t>>(params.m_sfnSf, rbgAllocationMap));
            return;
        }

        auto itCqi = m_ueCqi.find((*it).first);
        int cqi = 0;
        if (itCqi == m_ueCqi.end())
        {
            // no cqi info about this UE
            uldci.m_mcs = 0; // MCS 0 -> UL-AMC TBD
        }
        else
        {
            // take the lowest CQI value (worst RB)
            NS_ABORT_MSG_IF((*itCqi).second.empty(),
                            "CQI of RNTI = " << (*it).first << " has expired");
            double minSinr = (*itCqi).second.at(uldci.m_rbStart);
            if (minSinr == NO_SINR)
            {
                minSinr = EstimateUlSinr((*it).first, uldci.m_rbStart);
            }
            for (uint16_t i = uldci.m_rbStart; i < uldci.m_rbStart + uldci.m_rbLen; i++)
            {
                double sinr = (*itCqi).second.at(i);
                if (sinr == NO_SINR)
                {
                    sinr = EstimateUlSinr((*it).first, i);
                }
                if (sinr < minSinr)
                {
                    minSinr = sinr;
                }
            }

            // translate SINR -> cqi: WILD ACK: same as DL
            double s = log2(1 + (std::pow(10, minSinr / 10) / ((-std::log(5.0 * 0.00005)) / 1.5)));
            cqi = m_amc->GetCqiFromSpectralEfficiency(s);
            if (cqi == 0)
            {
                it++;
                if (it == m_ceBsrRxed.end())
                {
                    // restart from the first
                    it = m_ceBsrRxed.begin();
                }
                NS_LOG_DEBUG(this << " UE discarded for CQI = 0, RNTI " << uldci.m_rnti);
                // remove UE from allocation map
                for (uint16_t i = uldci.m_rbStart; i < uldci.m_rbStart + uldci.m_rbLen; i++)
                {
                    rbgAllocationMap.at(i) = 0;
                }
                continue; // CQI == 0 means "out of range" (see table 7.2.3-1 of 36.213)
            }
            uldci.m_mcs = m_amc->GetMcsFromCqi(cqi);
        }

        uldci.m_tbSize = (m_amc->GetUlTbSizeFromMcs(uldci.m_mcs, rbPerFlow) / 8);
        UpdateUlRlcBufferInfo(uldci.m_rnti, uldci.m_tbSize);
        uldci.m_ndi = 1;
        uldci.m_cceIndex = 0;
        uldci.m_aggrLevel = 1;
        uldci.m_ueTxAntennaSelection = 3; // antenna selection OFF
        uldci.m_hopping = false;
        uldci.m_n2Dmrs = 0;
        uldci.m_tpc = 0;            // no power control
        uldci.m_cqiRequest = false; // only period CQI at this stage
        uldci.m_ulIndex = 0;        // TDD parameter
        uldci.m_dai = 1;            // TDD parameter
        uldci.m_freqHopping = 0;
        uldci.m_pdcchPowerOffset = 0; // not used
        ret.m_dciList.push_back(uldci);
        // store DCI for HARQ_PERIOD
        uint8_t harqId = 0;
        if (m_harqOn)
        {
            auto itProcId = m_ulHarqCurrentProcessId.find(uldci.m_rnti);
            if (itProcId == m_ulHarqCurrentProcessId.end())
            {
                NS_FATAL_ERROR("No info find in HARQ buffer for UE " << uldci.m_rnti);
            }
            harqId = (*itProcId).second;
            auto itDci = m_ulHarqProcessesDciBuffer.find(uldci.m_rnti);
            if (itDci == m_ulHarqProcessesDciBuffer.end())
            {
                NS_FATAL_ERROR("Unable to find RNTI entry in UL DCI HARQ buffer for RNTI "
                               << uldci.m_rnti);
            }
            (*itDci).second.at(harqId) = uldci;
            // Update HARQ process status (RV 0)
            auto itStat = m_ulHarqProcessesStatus.find(uldci.m_rnti);
            if (itStat == m_ulHarqProcessesStatus.end())
            {
                NS_LOG_ERROR("No info find in HARQ buffer for UE (might change eNB) "
                             << uldci.m_rnti);
            }
            (*itStat).second.at(harqId) = 0;
        }

        NS_LOG_INFO(this << " UE Allocation RNTI " << (*it).first << " startPRB "
                         << (uint32_t)uldci.m_rbStart << " nPRB " << (uint32_t)uldci.m_rbLen
                         << " CQI " << cqi << " MCS " << (uint32_t)uldci.m_mcs << " TBsize "
                         << uldci.m_tbSize << " RbAlloc " << rbAllocated << " harqId "
                         << (uint16_t)harqId);

        // update TTI  UE stats
        auto itStats = m_flowStatsUl.find((*it).first);
        if (itStats != m_flowStatsUl.end())
        {
            (*itStats).second.lastTtiBytesTrasmitted = uldci.m_tbSize;
        }
        else
        {
            NS_LOG_DEBUG(this << " No Stats for this allocated UE");
        }

        it++;
        if (it == m_ceBsrRxed.end())
        {
            // restart from the first
            it = m_ceBsrRxed.begin();
        }
        if ((rbAllocated == m_cschedCellConfig.m_ulBandwidth) || (rbPerFlow == 0))
        {
            // Stop allocation: no more PRBs
            m_nextRntiUl = (*it).first;
            break;
        }
    } while (((*it).first != m_nextRntiUl) && (rbPerFlow != 0));

    // Update global UE stats
    // update UEs stats
    for (auto itStats = m_flowStatsUl.begin(); itStats != m_flowStatsUl.end(); itStats++)
    {
        (*itStats).second.totalBytesTransmitted += (*itStats).second.lastTtiBytesTrasmitted;
        // update average throughput (see eq. 12.3 of Sec 12.3.1.2 of LTE – The UMTS Long Term
        // Evolution, Ed Wiley)
        (*itStats).second.lastAveragedThroughput =
            ((1.0 - (1.0 / m_timeWindow)) * (*itStats).second.lastAveragedThroughput) +
            ((1.0 / m_timeWindow) * (double)((*itStats).second.lastTtiBytesTrasmitted / 0.001));
        NS_LOG_INFO(this << " UE total bytes " << (*itStats).second.totalBytesTransmitted);
        NS_LOG_INFO(this << " UE average throughput " << (*itStats).second.lastAveragedThroughput);
        (*itStats).second.lastTtiBytesTrasmitted = 0;
    }
    std::map<uint16_t, std::vector<uint16_t>>::iterator itMap;
    itMap = m_allocationMaps.find(params.m_sfnSf);
    if (itMap != m_allocationMaps.end())
    {
        // remove obsolete info on allocation first
        NS_LOG_DEBUG("Found SFnSF = " << params.m_sfnSf << " UL - Frame no. "
                                      << (params.m_sfnSf >> 4) << " subframe no. "
                                      << (0xF & params.m_sfnSf));
        m_allocationMaps.erase(itMap);
    }
    m_allocationMaps.insert(
        std::pair<uint16_t, std::vector<uint16_t>>(params.m_sfnSf, rbgAllocationMap));
    m_schedSapUser->SchedUlConfigInd(ret);
}

void
FdBetFfMacScheduler::DoSchedUlNoiseInterferenceReq(
    const FfMacSchedSapProvider::SchedUlNoiseInterferenceReqParameters& params)
{
    NS_LOG_FUNCTION(this);
}

void
FdBetFfMacScheduler::DoSchedUlSrInfoReq(
    const FfMacSchedSapProvider::SchedUlSrInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);
}

void
FdBetFfMacScheduler::DoSchedUlMacCtrlInfoReq(
    const FfMacSchedSapProvider::SchedUlMacCtrlInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);

    for (unsigned int i = 0; i < params.m_macCeList.size(); i++)
    {
        if (params.m_macCeList.at(i).m_macCeType == MacCeListElement_s::BSR)
        {
            // buffer status report
            // note that this scheduler does not differentiate the
            // allocation according to which LCGs have more/less bytes
            // to send.
            // Hence the BSR of different LCGs are just summed up to get
            // a total queue size that is used for allocation purposes.

            uint32_t buffer = 0;
            for (uint8_t lcg = 0; lcg < 4; ++lcg)
            {
                uint8_t bsrId = params.m_macCeList.at(i).m_macCeValue.m_bufferStatus.at(lcg);
                buffer += BufferSizeLevelBsr::BsrId2BufferSize(bsrId);
            }

            uint16_t rnti = params.m_macCeList.at(i).m_rnti;
            NS_LOG_LOGIC(this << "RNTI=" << rnti << " buffer=" << buffer);
            auto it = m_ceBsrRxed.find(rnti);
            if (it == m_ceBsrRxed.end())
            {
                // create the new entry
                m_ceBsrRxed.insert(std::pair<uint16_t, uint32_t>(rnti, buffer));
            }
            else
            {
                // update the buffer size value
                (*it).second = buffer;
            }
        }
    }
}

void
FdBetFfMacScheduler::DoSchedUlCqiInfoReq(
    const FfMacSchedSapProvider::SchedUlCqiInfoReqParameters& params)
{
    NS_LOG_FUNCTION(this);
    // retrieve the allocation for this subframe
    switch (m_ulCqiFilter)
    {
    case FfMacScheduler::SRS_UL_CQI: {
        // filter all the CQIs that are not SRS based
        if (params.m_ulCqi.m_type != UlCqi_s::SRS)
        {
            return;
        }
    }
    break;
    case FfMacScheduler::PUSCH_UL_CQI: {
        // filter all the CQIs that are not SRS based
        if (params.m_ulCqi.m_type != UlCqi_s::PUSCH)
        {
            return;
        }
    }
    break;
    default:
        NS_FATAL_ERROR("Unknown UL CQI type");
    }

    switch (params.m_ulCqi.m_type)
    {
    case UlCqi_s::PUSCH: {
        NS_LOG_DEBUG(this << " Collect PUSCH CQIs of Frame no. " << (params.m_sfnSf >> 4)
                          << " subframe no. " << (0xF & params.m_sfnSf));
        auto itMap = m_allocationMaps.find(params.m_sfnSf);
        if (itMap == m_allocationMaps.end())
        {
            return;
        }
        for (uint32_t i = 0; i < (*itMap).second.size(); i++)
        {
            // convert from fixed point notation Sxxxxxxxxxxx.xxx to double
            double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(i));
            auto itCqi = m_ueCqi.find((*itMap).second.at(i));
            if (itCqi == m_ueCqi.end())
            {
                // create a new entry
                std::vector<double> newCqi;
                for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
                {
                    if (i == j)
                    {
                        newCqi.push_back(sinr);
                    }
                    else
                    {
                        // initialize with NO_SINR value.
                        newCqi.push_back(NO_SINR);
                    }
                }
                m_ueCqi.insert(
                    std::pair<uint16_t, std::vector<double>>((*itMap).second.at(i), newCqi));
                // generate correspondent timer
                m_ueCqiTimers.insert(
                    std::pair<uint16_t, uint32_t>((*itMap).second.at(i), m_cqiTimersThreshold));
            }
            else
            {
                // update the value
                (*itCqi).second.at(i) = sinr;
                NS_LOG_DEBUG(this << " RNTI " << (*itMap).second.at(i) << " RB " << i << " SINR "
                                  << sinr);
                // update correspondent timer
                auto itTimers = m_ueCqiTimers.find((*itMap).second.at(i));
                (*itTimers).second = m_cqiTimersThreshold;
            }
        }
        // remove obsolete info on allocation
        m_allocationMaps.erase(itMap);
    }
    break;
    case UlCqi_s::SRS: {
        // get the RNTI from vendor specific parameters
        uint16_t rnti = 0;
        NS_ASSERT(!params.m_vendorSpecificList.empty());
        for (std::size_t i = 0; i < params.m_vendorSpecificList.size(); i++)
        {
            if (params.m_vendorSpecificList.at(i).m_type == SRS_CQI_RNTI_VSP)
            {
                Ptr<SrsCqiRntiVsp> vsp =
                    DynamicCast<SrsCqiRntiVsp>(params.m_vendorSpecificList.at(i).m_value);
                rnti = vsp->GetRnti();
            }
        }
        auto itCqi = m_ueCqi.find(rnti);
        if (itCqi == m_ueCqi.end())
        {
            // create a new entry
            std::vector<double> newCqi;
            for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
            {
                double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(j));
                newCqi.push_back(sinr);
                NS_LOG_INFO(this << " RNTI " << rnti << " new SRS-CQI for RB  " << j << " value "
                                 << sinr);
            }
            m_ueCqi.insert(std::pair<uint16_t, std::vector<double>>(rnti, newCqi));
            // generate correspondent timer
            m_ueCqiTimers.insert(std::pair<uint16_t, uint32_t>(rnti, m_cqiTimersThreshold));
        }
        else
        {
            // update the values
            for (uint32_t j = 0; j < m_cschedCellConfig.m_ulBandwidth; j++)
            {
                double sinr = LteFfConverter::fpS11dot3toDouble(params.m_ulCqi.m_sinr.at(j));
                (*itCqi).second.at(j) = sinr;
                NS_LOG_INFO(this << " RNTI " << rnti << " update SRS-CQI for RB  " << j << " value "
                                 << sinr);
            }
            // update correspondent timer
            auto itTimers = m_ueCqiTimers.find(rnti);
            (*itTimers).second = m_cqiTimersThreshold;
        }
    }
    break;
    case UlCqi_s::PUCCH_1:
    case UlCqi_s::PUCCH_2:
    case UlCqi_s::PRACH: {
        NS_FATAL_ERROR("FdBetFfMacScheduler supports only PUSCH and SRS UL-CQIs");
    }
    break;
    default:
        NS_FATAL_ERROR("Unknown type of UL-CQI");
    }
}

void
FdBetFfMacScheduler::RefreshDlCqiMaps()
{
    // refresh DL CQI P01 Map
    auto itP10 = m_p10CqiTimers.begin();
    while (itP10 != m_p10CqiTimers.end())
    {
        NS_LOG_INFO(this << " P10-CQI for user " << (*itP10).first << " is "
                         << (uint32_t)(*itP10).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itP10).second == 0)
        {
            // delete correspondent entries
            auto itMap = m_p10CqiRxed.find((*itP10).first);
            NS_ASSERT_MSG(itMap != m_p10CqiRxed.end(),
                          " Does not find CQI report for user " << (*itP10).first);
            NS_LOG_INFO(this << " P10-CQI expired for user " << (*itP10).first);
            m_p10CqiRxed.erase(itMap);
            auto temp = itP10;
            itP10++;
            m_p10CqiTimers.erase(temp);
        }
        else
        {
            (*itP10).second--;
            itP10++;
        }
    }

    // refresh DL CQI A30 Map
    auto itA30 = m_a30CqiTimers.begin();
    while (itA30 != m_a30CqiTimers.end())
    {
        NS_LOG_INFO(this << " A30-CQI for user " << (*itA30).first << " is "
                         << (uint32_t)(*itA30).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itA30).second == 0)
        {
            // delete correspondent entries
            auto itMap = m_a30CqiRxed.find((*itA30).first);
            NS_ASSERT_MSG(itMap != m_a30CqiRxed.end(),
                          " Does not find CQI report for user " << (*itA30).first);
            NS_LOG_INFO(this << " A30-CQI expired for user " << (*itA30).first);
            m_a30CqiRxed.erase(itMap);
            auto temp = itA30;
            itA30++;
            m_a30CqiTimers.erase(temp);
        }
        else
        {
            (*itA30).second--;
            itA30++;
        }
    }
}

void
FdBetFfMacScheduler::RefreshUlCqiMaps()
{
    // refresh UL CQI  Map
    auto itUl = m_ueCqiTimers.begin();
    while (itUl != m_ueCqiTimers.end())
    {
        NS_LOG_INFO(this << " UL-CQI for user " << (*itUl).first << " is "
                         << (uint32_t)(*itUl).second << " thr " << (uint32_t)m_cqiTimersThreshold);
        if ((*itUl).second == 0)
        {
            // delete correspondent entries
            auto itMap = m_ueCqi.find((*itUl).first);
            NS_ASSERT_MSG(itMap != m_ueCqi.end(),
                          " Does not find CQI report for user " << (*itUl).first);
            NS_LOG_INFO(this << " UL-CQI exired for user " << (*itUl).first);
            (*itMap).second.clear();
            m_ueCqi.erase(itMap);
            auto temp = itUl;
            itUl++;
            m_ueCqiTimers.erase(temp);
        }
        else
        {
            (*itUl).second--;
            itUl++;
        }
    }
}

void
FdBetFfMacScheduler::UpdateDlRlcBufferInfo(uint16_t rnti, uint8_t lcid, uint16_t size)
{
    LteFlowId_t flow(rnti, lcid);
    auto it = m_rlcBufferReq.find(flow);
    if (it != m_rlcBufferReq.end())
    {
        NS_LOG_INFO(this << " UE " << rnti << " LC " << (uint16_t)lcid << " txqueue "
                         << (*it).second.m_rlcTransmissionQueueSize << " retxqueue "
                         << (*it).second.m_rlcRetransmissionQueueSize << " status "
                         << (*it).second.m_rlcStatusPduSize << " decrease " << size);
        // Update queues: RLC tx order Status, ReTx, Tx
        // Update status queue
        if (((*it).second.m_rlcStatusPduSize > 0) && (size >= (*it).second.m_rlcStatusPduSize))
        {
            (*it).second.m_rlcStatusPduSize = 0;
        }
        else if (((*it).second.m_rlcRetransmissionQueueSize > 0) &&
                 (size >= (*it).second.m_rlcRetransmissionQueueSize))
        {
            (*it).second.m_rlcRetransmissionQueueSize = 0;
        }
        else if ((*it).second.m_rlcTransmissionQueueSize > 0)
        {
            uint32_t rlcOverhead;
            if (lcid == 1)
            {
                // for SRB1 (using RLC AM) it's better to
                // overestimate RLC overhead rather than
                // underestimate it and risk unneeded
                // segmentation which increases delay
                rlcOverhead = 4;
            }
            else
            {
                // minimum RLC overhead due to header
                rlcOverhead = 2;
            }
            // update transmission queue
            if ((*it).second.m_rlcTransmissionQueueSize <= size - rlcOverhead)
            {
                (*it).second.m_rlcTransmissionQueueSize = 0;
            }
            else
            {
                (*it).second.m_rlcTransmissionQueueSize -= size - rlcOverhead;
            }
        }
    }
    else
    {
        NS_LOG_ERROR(this << " Does not find DL RLC Buffer Report of UE " << rnti);
    }
}

void
FdBetFfMacScheduler::UpdateUlRlcBufferInfo(uint16_t rnti, uint16_t size)
{
    size = size - 2; // remove the minimum RLC overhead
    auto it = m_ceBsrRxed.find(rnti);
    if (it != m_ceBsrRxed.end())
    {
        NS_LOG_INFO(this << " UE " << rnti << " size " << size << " BSR " << (*it).second);
        if ((*it).second >= size)
        {
            (*it).second -= size;
        }
        else
        {
            (*it).second = 0;
        }
    }
    else
    {
        NS_LOG_ERROR(this << " Does not find BSR report info of UE " << rnti);
    }
}

void
FdBetFfMacScheduler::TransmissionModeConfigurationUpdate(uint16_t rnti, uint8_t txMode)
{
    NS_LOG_FUNCTION(this << " RNTI " << rnti << " txMode " << (uint16_t)txMode);
    FfMacCschedSapUser::CschedUeConfigUpdateIndParameters params;
    params.m_rnti = rnti;
    params.m_transmissionMode = txMode;
    m_cschedSapUser->CschedUeConfigUpdateInd(params);
}

} // namespace ns3
