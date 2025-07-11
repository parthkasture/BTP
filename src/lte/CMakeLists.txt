set(emu_sources)
set(emu_headers)
set(emu_features)
set(emu_libraries)
if(${ENABLE_EMU})
  set(emu_sources
      helper/emu-epc-helper.cc
  )
  set(emu_headers
      helper/emu-epc-helper.h
  )
  set(emu_features
      EmuFdNetDevice
  )
  set(emu_libraries
      ${libfd-net-device}
  )
endif()

set(source_files
    ${emu_sources}
    helper/cc-helper.cc
    helper/epc-helper.cc
    helper/lte-3gpp-hex-grid-enb-topology-helper.cc
    helper/lte-global-pathloss-database.cc
    helper/lte-helper.cc
    helper/lte-hex-grid-enb-topology-helper.cc
    helper/lte-sidelink-helper.cc
    helper/lte-stats-calculator.cc
    helper/mac-stats-calculator.cc
    helper/no-backhaul-epc-helper.cc
    helper/phy-rx-stats-calculator.cc
    helper/phy-stats-calculator.cc
    helper/phy-tx-stats-calculator.cc
    helper/point-to-point-epc-helper.cc
    helper/radio-bearer-stats-calculator.cc
    helper/radio-bearer-stats-connector.cc
    helper/radio-environment-map-helper.cc
    helper/rrc-stats-calculator.cc
    helper/sidelink-rsrp-calculator.cc
    model/a2-a4-rsrq-handover-algorithm.cc
    model/a3-rsrp-handover-algorithm.cc
    model/component-carrier-enb.cc
    model/component-carrier-ue.cc
    model/component-carrier.cc
    model/cqa-ff-mac-scheduler.cc
    model/epc-enb-application.cc
    model/epc-enb-s1-sap.cc
    model/epc-gtpc-header.cc
    model/epc-gtpu-header.cc
    model/epc-mme-application.cc
    model/epc-pgw-application.cc
    model/epc-s11-sap.cc
    model/epc-s1ap-sap.cc
    model/epc-sgw-application.cc
    model/epc-tft-classifier.cc
    model/epc-tft.cc
    model/epc-ue-nas.cc
    model/epc-x2-header.cc
    model/epc-x2-sap.cc
    model/epc-x2.cc
    model/eps-bearer-tag.cc
    model/eps-bearer.cc
    model/fdbet-ff-mac-scheduler.cc
    model/fdmt-ff-mac-scheduler.cc
    model/fdtbfq-ff-mac-scheduler.cc
    model/ff-mac-common.cc
    model/ff-mac-csched-sap.cc
    model/ff-mac-sched-sap.cc
    model/ff-mac-scheduler.cc
    model/lte-amc.cc
    model/lte-anr-sap.cc
    model/lte-anr.cc
    model/lte-as-sap.cc
    model/lte-asn1-header.cc
    model/lte-ccm-mac-sap.cc
    model/lte-ccm-rrc-sap.cc
    model/lte-chunk-processor.cc
    model/lte-common.cc
    model/lte-control-messages.cc
    model/lte-enb-cmac-sap.cc
    model/lte-enb-component-carrier-manager.cc
    model/lte-enb-cphy-sap.cc
    model/lte-enb-mac.cc
    model/lte-enb-net-device.cc
    model/lte-enb-phy-sap.cc
    model/lte-enb-phy.cc
    model/lte-enb-rrc.cc
    model/lte-ffr-algorithm.cc
    model/lte-ffr-distributed-algorithm.cc
    model/lte-ffr-enhanced-algorithm.cc
    model/lte-ffr-rrc-sap.cc
    model/lte-ffr-sap.cc
    model/lte-ffr-soft-algorithm.cc
    model/lte-fr-hard-algorithm.cc
    model/lte-fr-no-op-algorithm.cc
    model/lte-fr-soft-algorithm.cc
    model/lte-fr-strict-algorithm.cc
    model/lte-handover-algorithm.cc
    model/lte-handover-management-sap.cc
    model/lte-harq-phy.cc
    model/lte-interference.cc
    model/lte-mac-sap.cc
    model/lte-mi-error-model.cc
    model/lte-net-device.cc
    model/lte-nist-error-model.cc
    model/lte-pdcp-header.cc
    model/lte-pdcp-sap.cc
    model/lte-pdcp-tag.cc
    model/lte-pdcp.cc
    model/lte-phy-tag.cc
    model/lte-phy.cc
    model/lte-radio-bearer-info.cc
    model/lte-radio-bearer-tag.cc
    model/lte-rlc-am-header.cc
    model/lte-rlc-am.cc
    model/lte-rlc-header.cc
    model/lte-rlc-sap.cc
    model/lte-rlc-sdu-status-tag.cc
    model/lte-rlc-sequence-number.cc
    model/lte-rlc-tag.cc
    model/lte-rlc-tm.cc
    model/lte-rlc-um.cc
    model/lte-rlc.cc
    model/lte-rrc-header.cc
    model/lte-rrc-protocol-ideal.cc
    model/lte-rrc-protocol-real.cc
    model/lte-rrc-sap.cc
    model/lte-sl-basic-ue-controller.cc
    model/lte-sl-chunk-processor.cc
    model/lte-sl-disc-preconfig-pool-factory.cc
    model/lte-sl-disc-resource-pool-factory.cc
    model/lte-sl-enb-rrc.cc
    model/lte-sl-harq-phy.cc
    model/lte-sl-header.cc
    model/lte-sl-interference.cc
    model/lte-sl-o2o-comm-params.cc
    model/lte-sl-pc5-signalling-header.cc
    model/lte-sl-pdcp-header.cc
    model/lte-sl-pool.cc
    model/lte-sl-pool-factory.cc
    model/lte-sl-preconfig-pool-factory.cc
    model/lte-sl-resource-pool-factory.cc
    model/lte-sl-tag.cc
    model/lte-sl-tft.cc
    model/lte-sl-ue-controller.cc
    model/lte-sl-ue-ctrl-sap.cc
    model/lte-sl-ue-net-device.cc
    model/lte-sl-ue-rrc.cc
    model/lte-spectrum-phy.cc
    model/lte-spectrum-signal-parameters.cc
    model/lte-spectrum-value-helper.cc
    model/lte-ue-ccm-rrc-sap.cc
    model/lte-ue-cmac-sap.cc
    model/lte-ue-component-carrier-manager.cc
    model/lte-ue-cphy-sap.cc
    model/lte-ue-mac.cc
    model/lte-ue-net-device.cc
    model/lte-ue-phy-sap.cc
    model/lte-ue-phy.cc
    model/lte-ue-power-control.cc
    model/lte-ue-rrc.cc
    model/lte-vendor-specific-parameters.cc
    model/no-op-component-carrier-manager.cc
    model/no-op-handover-algorithm.cc
    model/pf-ff-mac-scheduler.cc
    model/pss-ff-mac-scheduler.cc
    model/rem-spectrum-phy.cc
    model/rr-ff-mac-scheduler.cc
    model/rr-sl-ff-mac-scheduler.cc
    model/simple-ue-component-carrier-manager.cc
    model/tdbet-ff-mac-scheduler.cc
    model/tdmt-ff-mac-scheduler.cc
    model/tdtbfq-ff-mac-scheduler.cc
    model/tta-ff-mac-scheduler.cc
)

set(header_files
    ${emu_headers}
    helper/cc-helper.h
    helper/epc-helper.h
    helper/lte-3gpp-hex-grid-enb-topology-helper.h
    helper/lte-global-pathloss-database.h
    helper/lte-helper.h
    helper/lte-hex-grid-enb-topology-helper.h
    helper/lte-sidelink-helper.h
    helper/lte-stats-calculator.h
    helper/mac-stats-calculator.h
    helper/no-backhaul-epc-helper.h
    helper/phy-rx-stats-calculator.h
    helper/phy-stats-calculator.h
    helper/phy-tx-stats-calculator.h
    helper/point-to-point-epc-helper.h
    helper/radio-bearer-stats-calculator.h
    helper/radio-bearer-stats-connector.h
    helper/radio-environment-map-helper.h
    helper/rrc-stats-calculator.h
    helper/sidelink-rsrp-calculator.h
    model/a2-a4-rsrq-handover-algorithm.h
    model/a3-rsrp-handover-algorithm.h
    model/component-carrier-enb.h
    model/component-carrier-ue.h
    model/component-carrier.h
    model/cqa-ff-mac-scheduler.h
    model/epc-enb-application.h
    model/epc-enb-s1-sap.h
    model/epc-gtpc-header.h
    model/epc-gtpu-header.h
    model/epc-mme-application.h
    model/epc-pgw-application.h
    model/epc-s11-sap.h
    model/epc-s1ap-sap.h
    model/epc-sgw-application.h
    model/epc-tft-classifier.h
    model/epc-tft.h
    model/epc-ue-nas.h
    model/epc-x2-header.h
    model/epc-x2-sap.h
    model/epc-x2.h
    model/eps-bearer-tag.h
    model/eps-bearer.h
    model/fdbet-ff-mac-scheduler.h
    model/fdmt-ff-mac-scheduler.h
    model/fdtbfq-ff-mac-scheduler.h
    model/ff-mac-common.h
    model/ff-mac-csched-sap.h
    model/ff-mac-sched-sap.h
    model/ff-mac-scheduler.h
    model/lte-amc.h
    model/lte-anr-sap.h
    model/lte-anr.h
    model/lte-as-sap.h
    model/lte-asn1-header.h
    model/lte-ccm-mac-sap.h
    model/lte-ccm-rrc-sap.h
    model/lte-chunk-processor.h
    model/lte-common.h
    model/lte-control-messages.h
    model/lte-enb-cmac-sap.h
    model/lte-enb-component-carrier-manager.h
    model/lte-enb-cphy-sap.h
    model/lte-enb-mac.h
    model/lte-enb-net-device.h
    model/lte-enb-phy-sap.h
    model/lte-enb-phy.h
    model/lte-enb-rrc.h
    model/lte-ffr-algorithm.h
    model/lte-ffr-distributed-algorithm.h
    model/lte-ffr-enhanced-algorithm.h
    model/lte-ffr-rrc-sap.h
    model/lte-ffr-sap.h
    model/lte-ffr-soft-algorithm.h
    model/lte-fr-hard-algorithm.h
    model/lte-fr-no-op-algorithm.h
    model/lte-fr-soft-algorithm.h
    model/lte-fr-strict-algorithm.h
    model/lte-handover-algorithm.h
    model/lte-handover-management-sap.h
    model/lte-harq-phy.h
    model/lte-interference.h
    model/lte-mac-sap.h
    model/lte-mi-error-model.h
    model/lte-net-device.h
    model/lte-nist-error-model.h
    model/lte-pdcp-header.h
    model/lte-pdcp-sap.h
    model/lte-pdcp-tag.h
    model/lte-pdcp.h
    model/lte-phy-tag.h
    model/lte-phy.h
    model/lte-radio-bearer-info.h
    model/lte-radio-bearer-tag.h
    model/lte-rlc-am-header.h
    model/lte-rlc-am.h
    model/lte-rlc-header.h
    model/lte-rlc-sap.h
    model/lte-rlc-sdu-status-tag.h
    model/lte-rlc-sequence-number.h
    model/lte-rlc-tag.h
    model/lte-rlc-tm.h
    model/lte-rlc-um.h
    model/lte-rlc.h
    model/lte-rrc-header.h
    model/lte-rrc-protocol-ideal.h
    model/lte-rrc-protocol-real.h
    model/lte-rrc-sap.h
    model/lte-sl-basic-ue-controller.h
    model/lte-sl-chunk-processor.h
    model/lte-sl-disc-preconfig-pool-factory.h
    model/lte-sl-disc-resource-pool-factory.h
    model/lte-sl-enb-rrc.h
    model/lte-sl-harq-phy.h
    model/lte-sl-header.h
    model/lte-sl-interference.h
    model/lte-sl-o2o-comm-params.h
    model/lte-sl-pc5-signalling-header.h
    model/lte-sl-pdcp-header.h
    model/lte-sl-pool-factory.h
    model/lte-sl-pool.h
    model/lte-sl-preconfig-pool-factory.h
    model/lte-sl-resource-pool-factory.h
    model/lte-sl-tag.h
    model/lte-sl-tft.h
    model/lte-sl-ue-controller.h
    model/lte-sl-ue-ctrl-sap.h
    model/lte-sl-ue-net-device.h
    model/lte-sl-ue-rrc.h
    model/lte-spectrum-phy.h
    model/lte-spectrum-signal-parameters.h
    model/lte-spectrum-value-helper.h
    model/lte-ue-ccm-rrc-sap.h
    model/lte-ue-cmac-sap.h
    model/lte-ue-component-carrier-manager.h
    model/lte-ue-cphy-sap.h
    model/lte-ue-mac.h
    model/lte-ue-net-device.h
    model/lte-ue-phy-sap.h
    model/lte-ue-phy.h
    model/lte-ue-power-control.h
    model/lte-ue-rrc.h
    model/lte-vendor-specific-parameters.h
    model/no-op-component-carrier-manager.h
    model/no-op-handover-algorithm.h
    model/pf-ff-mac-scheduler.h
    model/pss-ff-mac-scheduler.h
    model/rem-spectrum-phy.h
    model/rr-ff-mac-scheduler.h
    model/rr-sl-ff-mac-scheduler.h
    model/simple-ue-component-carrier-manager.h
    model/tdbet-ff-mac-scheduler.h
    model/tdmt-ff-mac-scheduler.h
    model/tdtbfq-ff-mac-scheduler.h
    model/tta-ff-mac-scheduler.h
)

set(test_sources
    test/epc-test-gtpu.cc
    test/epc-test-s1u-downlink.cc
    test/epc-test-s1u-uplink.cc
    test/lte-ffr-simple.cc
    test/lte-simple-helper.cc
    test/lte-simple-net-device.cc
    test/lte-simple-spectrum-phy.cc
    test/lte-test-aggregation-throughput-scale.cc
    test/lte-test-carrier-aggregation-configuration.cc
    test/lte-test-carrier-aggregation.cc
    test/lte-test-cell-selection.cc
    test/lte-test-cqa-ff-mac-scheduler.cc
    test/lte-test-cqi-generation.cc
    test/lte-test-deactivate-bearer.cc
    test/lte-test-downlink-power-control.cc
    test/lte-test-downlink-sinr.cc
    test/lte-test-earfcn.cc
    test/lte-test-entities.cc
    test/lte-test-fdbet-ff-mac-scheduler.cc
    test/lte-test-fdmt-ff-mac-scheduler.cc
    test/lte-test-fdtbfq-ff-mac-scheduler.cc
    test/lte-test-frequency-reuse.cc
    test/lte-test-harq.cc
    test/lte-test-interference-fr.cc
    test/lte-test-interference.cc
    test/lte-test-ipv6-routing.cc
    test/lte-test-link-adaptation.cc
    test/lte-test-mimo.cc
    test/lte-test-pathloss-model.cc
    test/lte-test-pf-ff-mac-scheduler.cc
    test/lte-test-phy-error-model.cc
    test/lte-test-primary-cell-change.cc
    test/lte-test-pss-ff-mac-scheduler.cc
    test/lte-test-radio-link-failure.cc
    test/lte-test-rlc-am-e2e.cc
    test/lte-test-rlc-am-transmitter.cc
    test/lte-test-rlc-um-e2e.cc
    test/lte-test-rlc-um-transmitter.cc
    test/lte-test-rr-ff-mac-scheduler.cc
    test/lte-test-secondary-cell-handover.cc
    test/lte-test-secondary-cell-selection.cc
    test/lte-test-spectrum-value-helper.cc
    test/lte-test-tdbet-ff-mac-scheduler.cc
    test/lte-test-tdmt-ff-mac-scheduler.cc
    test/lte-test-tdtbfq-ff-mac-scheduler.cc
    test/lte-test-tta-ff-mac-scheduler.cc
    test/lte-test-ue-measurements.cc
    test/lte-test-ue-phy.cc
    test/lte-test-uplink-power-control.cc
    test/lte-test-uplink-sinr.cc
    test/test-asn1-encoding.cc
    test/test-epc-tft-classifier.cc
    test/test-lte-antenna.cc
    test/test-lte-epc-e2e-data.cc
    test/test-lte-handover-delay.cc
    test/test-lte-handover-failure.cc
    test/test-lte-handover-target.cc
    test/test-lte-pc5-signalling-header.cc
    test/test-lte-rlc-header.cc
    test/test-lte-rrc.cc
    test/test-lte-x2-handover-measures.cc
    test/test-lte-x2-handover.cc
    test/test-nist-phy-error-model.cc
    test/test-sidelink-comm-pool.cc
    test/test-sidelink-disc-pool.cc
    test/test-sidelink-in-coverage-comm.cc
    test/test-sidelink-out-of-coverage-comm.cc
    test/test-sidelink-synch.cc
    test/test-sl-in-covrg-1relay-1remote-disconnect-relay.cc
    test/test-sl-in-covrg-1relay-1remote-disconnect-remote.cc
    test/test-sl-in-covrg-1relay-1remote-keepalive.cc
    test/test-sl-in-covrg-1relay-1remote-reconnect-relay.cc
    test/test-sl-in-covrg-1relay-1remote-reconnect-remote.cc
    test/test-sl-in-covrg-1relay-1remote-regular.cc
    test/test-sl-in-covrg-1relay-2remote-regular.cc
    test/test-sl-in-covrg-2relay-1remote-disconnect-relay.cc
    test/test-sl-in-covrg-2relay-1remote-regular.cc
    test/test-sl-in-covrg-discovery-1relay-1remote.cc
    test/test-sl-in-covrg-discovery-multi-app.cc
    test/test-sl-in-covrg-discovery-regular.cc
    test/test-sl-ooc-1relay-1remote-misaligned.cc
    test/test-sl-ooc-1relay-1remote-regular.cc
    test/test-sl-ooc-relay-generic-traffic-rm2rh.cc
    test/test-sl-ooc-relay-generic-traffic-rm2rm.cc
    test/test-wrap-around-hex-topology.cc
)

build_lib(
  LIBNAME lte
  SOURCE_FILES ${source_files}
  HEADER_FILES ${header_files}
  LIBRARIES_TO_LINK
    ${emu_libraries}
    ${libcore}
    ${libnetwork}
    ${libspectrum}
    ${libstats}
    ${libbuildings}
    ${libvirtual-net-device}
    ${libpoint-to-point}
    ${libapplications}
    ${libinternet}
    ${libcsma}
    ${libconfig-store}
  TEST_SOURCES ${test_sources}
  MODULE_ENABLED_FEATURES ${emu_features}
)
