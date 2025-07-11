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
 * Author: Manuel Requena <manuel.requena@cttc.es>
 * Modified by: NIST // Contributions may not be subject to US copyright.
 */

#ifndef LTE_PDCP_SAP_H
#define LTE_PDCP_SAP_H

#include "ns3/packet.h"

namespace ns3
{

/**
 * Service Access Point (SAP) offered by the PDCP entity to the RRC entity
 * See 3GPP 36.323 Packet Data Convergence Protocol (PDCP) specification
 *
 * This is the PDCP SAP Provider
 * (i.e. the part of the SAP that contains the PDCP methods called by the RRC)
 */
class LtePdcpSapProvider
{
  public:
    virtual ~LtePdcpSapProvider();

    /**
     * Parameters for LtePdcpSapProvider::TransmitPdcpSdu
     */
    struct TransmitPdcpSduParameters
    {
        Ptr<Packet> pdcpSdu; /**< the RRC PDU */
        uint16_t rnti;       /**< the C-RNTI identifying the UE */
        uint8_t lcid;     /**< the logical channel id corresponding to the sending RLC instance */
        uint32_t srcL2Id; /**< Source L2 ID (24 bits) */
        uint32_t dstL2Id; /**< Destination L2 ID (24 bits) */
        uint8_t sduType;  /**< SDU type for SLRB (3 bits) */
    };

    /**
     * Send RRC PDU parameters to the PDCP for transmission
     *
     * This method is to be called when upper RRC entity has a
     * RRC PDU ready to send
     *
     * \param params Parameters
     */
    virtual void TransmitPdcpSdu(TransmitPdcpSduParameters params) = 0;
};

/**
 * Service Access Point (SAP) offered by the PDCP entity to the RRC entity
 * See 3GPP 36.323 Packet Data Convergence Protocol (PDCP) specification
 *
 * This is the PDCP SAP User
 * (i.e. the part of the SAP that contains the RRC methods called by the PDCP)
 */
class LtePdcpSapUser
{
  public:
    virtual ~LtePdcpSapUser();

    /**
     * PDCP SDU types (section 6.3.14)
     */
    typedef enum
    {
        IP_SDU = 0,
        ARP_SDU = 1,
        PC5_SIGNALING_SDU = 2,
        NON_IP_SDU = 3
    } SduType_t;

    /**
     * Parameters for LtePdcpSapUser::ReceivePdcpSdu
     */
    struct ReceivePdcpSduParameters
    {
        Ptr<Packet> pdcpSdu; /**< the RRC PDU */
        uint16_t rnti;       /**< the C-RNTI identifying the UE */
        uint8_t lcid;     /**< the logical channel id corresponding to the sending RLC instance */
        uint32_t srcL2Id; /**< Source L2 ID (24 bits) */
        uint32_t dstL2Id; /**< Destination L2 ID (24 bits) */
        uint8_t sduType;  /**< SDU type for SLRB (3 bits) */
    };

    /**
     * Called by the PDCP entity to notify the RRC entity of the reception of a new RRC PDU
     *
     * \param params Parameters
     */
    virtual void ReceivePdcpSdu(ReceivePdcpSduParameters params) = 0;
};

/// LtePdcpSpecificLtePdcpSapProvider class
template <class C>
class LtePdcpSpecificLtePdcpSapProvider : public LtePdcpSapProvider
{
  public:
    /**
     * Constructor
     *
     * \param pdcp PDCP
     */
    LtePdcpSpecificLtePdcpSapProvider(C* pdcp);

    // Delete default constructor to avoid misuse
    LtePdcpSpecificLtePdcpSapProvider() = delete;

    // Interface implemented from LtePdcpSapProvider
    void TransmitPdcpSdu(TransmitPdcpSduParameters params) override;

  private:
    C* m_pdcp; ///< the PDCP
};

template <class C>
LtePdcpSpecificLtePdcpSapProvider<C>::LtePdcpSpecificLtePdcpSapProvider(C* pdcp)
    : m_pdcp(pdcp)
{
}

template <class C>
void
LtePdcpSpecificLtePdcpSapProvider<C>::TransmitPdcpSdu(TransmitPdcpSduParameters params)
{
    m_pdcp->DoTransmitPdcpSdu(params);
}

/// LtePdcpSpecificLtePdcpSapUser class
template <class C>
class LtePdcpSpecificLtePdcpSapUser : public LtePdcpSapUser
{
  public:
    /**
     * Constructor
     *
     * \param rrc RRC
     */
    LtePdcpSpecificLtePdcpSapUser(C* rrc);

    // Delete default constructor to avoid misuse
    LtePdcpSpecificLtePdcpSapUser() = delete;

    // Interface implemented from LtePdcpSapUser
    void ReceivePdcpSdu(ReceivePdcpSduParameters params) override;

  private:
    C* m_rrc; ///< RRC
};

template <class C>
LtePdcpSpecificLtePdcpSapUser<C>::LtePdcpSpecificLtePdcpSapUser(C* rrc)
    : m_rrc(rrc)
{
}

template <class C>
void
LtePdcpSpecificLtePdcpSapUser<C>::ReceivePdcpSdu(ReceivePdcpSduParameters params)
{
    m_rrc->DoReceivePdcpSdu(params);
}

} // namespace ns3

#endif // LTE_PDCP_SAP_H
