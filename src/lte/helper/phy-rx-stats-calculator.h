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
 * Author: Jaume Nin <jnin@cttc.es>
 * Modified by: Marco Miozzo <mmiozzo@cttc.es>
 *        Convert MacStatsCalculator in PhyRxStatsCalculator
 * Modified by: NIST // Contributions may not be subject to US copyright.
 */

#ifndef PHY_RX_STATS_CALCULATOR_H_
#define PHY_RX_STATS_CALCULATOR_H_

#include "lte-stats-calculator.h"

#include "ns3/nstime.h"
#include "ns3/uinteger.h"
#include <ns3/lte-common.h>

#include <fstream>
#include <string>

namespace ns3
{

/**
 * \ingroup lte
 *
 * Takes care of storing the information generated at PHY layer regarding
 * reception. Metrics saved are:
 *
 *   - Timestamp (in seconds)
 *   - Frame index
 *   - Subframe index
 *   - C-RNTI
 *   - MCS for transport block 1
 *   - Size of transport block 1
 *   - MCS for transport block 2 (0 if not used)
 *   - Size of transport block 2 (0 if not used)
 */
class PhyRxStatsCalculator : public LteStatsCalculator
{
  public:
    /**
     * Constructor
     */
    PhyRxStatsCalculator();

    /**
     * Destructor
     */
    ~PhyRxStatsCalculator() override;

    // Inherited from ns3::Object
    /**
     * Register this type.
     * \return The object TypeId.
     */
    static TypeId GetTypeId();

    /**
     * Set the name of the file where the UL Rx PHY statistics will be stored.
     *
     * \param outputFilename The string with the name of the file
     */
    void SetUlRxOutputFilename(std::string outputFilename);

    /**
     * Get the name of the file where the UL RX PHY statistics will be stored.
     * \return The name of the file where the UL RX PHY statistics will be stored
     */
    std::string GetUlRxOutputFilename();

    /**
     * Set the name of the file where the DL RX PHY statistics will be stored.
     *
     * \param outputFilename The string with the name of the file
     */
    void SetDlRxOutputFilename(std::string outputFilename);

    /**
     * Get the name of the file where the DL RX PHY statistics will be stored.
     * \return The name of the file where the DL RX PHY statistics will be stored
     */
    std::string GetDlRxOutputFilename();

    /**
     * Notifies the stats calculator that a downlink reception has occurred.
     * \param params The trace information regarding PHY reception stats
     */
    void DlPhyReception(PhyReceptionStatParameters params);

    /**
     * Notifies the stats calculator that an uplink reception has occurred.
     * \param params The trace information regarding PHY reception stats
     */
    void UlPhyReception(PhyReceptionStatParameters params);

    // Sidelink
    /**
     * Set the name of the file where the SL RX PHY statistics will be stored.
     *
     * \param outputFilename The string with the name of the file
     */
    void SetSlRxOutputFilename(std::string outputFilename);

    /**
     * Get the name of the file where the SL RX PHY statistics will be stored.
     * \return The name of the file where the SL RX PHY statistics will be stored
     */
    std::string GetSlRxOutputFilename();

    /**
     * Set the name of the file where the SL RX PSCCH statistics will be stored.
     *
     * \param outputFilename The string with the name of the file
     */
    void SetSlPscchRxOutputFilename(std::string outputFilename);

    /**
     * Get the name of the file where the SL RX PSCCH statistics will be stored.
     * \return The name of the file where the SL RX PHY statistics will be stored
     */
    std::string GetSlPscchRxOutputFilename();

    /**
     * Notifies the stats calculator that a Sidelink reception has occurred.
     * \param params The trace information regarding PHY reception stats
     */
    void SlPhyReception(PhyReceptionStatParameters params);

    /**
     * Notifies the stats calculator that a Sidelink reception has occurred.
     * \param params The trace information regarding PHY reception stats
     */
    void SlPscchReception(SlPhyReceptionStatParameters params);

    /**
     * trace sink
     *
     * \param phyRxStats
     * \param path
     * \param params
     */
    static void DlPhyReceptionCallback(Ptr<PhyRxStatsCalculator> phyRxStats,
                                       std::string path,
                                       PhyReceptionStatParameters params);

    /**
     * trace sink
     *
     * \param phyRxStats
     * \param path
     * \param params
     */
    static void UlPhyReceptionCallback(Ptr<PhyRxStatsCalculator> phyRxStats,
                                       std::string path,
                                       PhyReceptionStatParameters params);

    /**
     * trace sink
     *
     * \param phyRxStats
     * \param path
     * \param params
     */
    static void SlPhyReceptionCallback(Ptr<PhyRxStatsCalculator> phyRxStats,
                                       std::string path,
                                       PhyReceptionStatParameters params);

    /**
     * trace sink
     *
     * \param phyRxStats
     * \param path
     * \param params
     */
    static void SlPscchReceptionCallback(Ptr<PhyRxStatsCalculator> phyRxStats,
                                         std::string path,
                                         SlPhyReceptionStatParameters params);

  private:
    /**
     * When writing DL RX PHY statistics first time to file,
     * columns description is added. Then next lines are
     * appended to file. This value is true if output
     * files have not been opened yet
     */
    bool m_dlRxFirstWrite;

    /**
     * When writing UL RX PHY statistics first time to file,
     * columns description is added. Then next lines are
     * appended to file. This value is true if output
     * files have not been opened yet
     */
    bool m_ulRxFirstWrite;

    /**
     * When writing Sidelink RX PHY statistics first time to file,
     * columns description is added. Then next lines are
     * appended to file. This value is true if output
     * files have not been opened yet
     */
    bool m_slRxFirstWrite;

    /**
     * When writing Sidelink PSCCH RX PHY statistics first time to file,
     * columns description is added. Then next lines are
     * appended to file. This value is true if output
     * files have not been opened yet
     */
    bool m_slPscchRxFirstWrite;

    /**
     * DL RX PHY output trace file
     */
    std::ofstream m_dlRxOutFile;

    /**
     * UL RX PHY output trace file
     */
    std::ofstream m_ulRxOutFile;
};

} // namespace ns3

#endif /* PHY_RX_STATS_CALCULATOR_H_ */
