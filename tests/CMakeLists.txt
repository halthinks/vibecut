# SPDX-License-Identifier: BSD-2-Clause
# SPDX-FileCopyrightText: Jean-Baptiste Mardelle <jb@kdenlive.org>, Julius Künzel <julius.kuenzel@kde.org>, Eric Jiang <erjiang@alumni.iu.edu>, Vincent Pinon <vincent.pinon@asygn.com>

include_directories(..)
set(TEST_SOURCES ${CMAKE_CURRENT_SOURCE_DIR})
configure_file(tests_definitions.h.in tests_definitions.h)
kde_enable_exceptions()

set(KdenliveTest_SOURCES
    avcurvetest.cpp
    audiolevelstasktest.cpp
    cachetest.cpp
    colorscopestest.cpp
    compositiontest.cpp
    documenttest.cpp
    effectstest.cpp
    effectsgrouptest.cpp
    filetest.cpp # codespell:ignore filetest
    gradienteditwidgettest.cpp
    groupstest.cpp
    hidetest.cpp
    keyframetest.cpp
    markertest.cpp
    mixtest.cpp
    modeltest.cpp
    movetest.cpp
    nestingtest.cpp
    otiotest.cpp
    regressions.cpp
    rendermodeltest.cpp
    replacetest.cpp
    sequencetest.cpp
    snaptest.cpp
    spacertest.cpp
    subtitlestest.cpp
    timelinepreviewtest.cpp
    timewarptest.cpp
    titlertest.cpp
    treetest.cpp
    trimmingtest.cpp
    utilstest.cpp
    vibecutbrolltest.cpp
    vibecutcapabilitysurfacetest.cpp
    vibecutcontractstest.cpp
    vibecutconversationcontexttest.cpp
    vibecutduplicatefusiontest.cpp
    vibecutedittoolstest.cpp
    vibecuteffecttoolstest.cpp
    vibecutembeddingrefreshtest.cpp
    vibecutembeddingstoretest.cpp
    vibecutevaltest.cpp
    vibecutmutationlivetest.cpp
    vibecutmutationrollbacktest.cpp
    vibecutmutationstalelivetest.cpp
    vibecutrepeatedtakemutationtest.cpp
    vibecutextractorevidencecontracttest.cpp
    vibecutextractorprovidertest.cpp
    vibecutlocalactionprovidertest.cpp
    vibecutlocalaudioeventprovidertest.cpp
    vibecutlocaldiarizationprovidertest.cpp
    vibecutlocalobjectprovidertest.cpp
    vibecutlocalocrprovidertest.cpp
    vibecutactionsummarytest.cpp
    vibecudaudioeventsummarytest.cpp
    vibecutobjecttracktest.cpp
    vibecutsubjectcandidatetest.cpp
    vibecutocrtemporaltest.cpp
    vibecutr128tooltest.cpp
    vibecutroomtonetest.cpp
    vibecuthighlightstest.cpp
    vibecuthookstest.cpp
    vibecutjobmanagertest.cpp
    vibecutmarkertoolstest.cpp
    vibecutmediaevidencetest.cpp
    vibecutmediaindextest.cpp
    vibecutmodelprovidertest.cpp
    vibecutpacingtest.cpp
    vibecutplangatetest.cpp
    vibecutplanruntimetest.cpp
    vibecutpolicyoverridestest.cpp
    vibecutprojectmemorytest.cpp
    vibecutprojectrevisiontest.cpp
    vibecutprojectrulestest.cpp
    vibecutprojectsnapshottest.cpp
    vibecutroughcutalternativestest.cpp
    vibecutroughcutrelevancetest.cpp
    vibecutroughcutsynthesistest.cpp
    vibecutscripttest.cpp
    vibecutsemanticintegrationtest.cpp
    vibecutsemantictooltest.cpp
    vibecutspeakeridentitytest.cpp
    vibecutsubtitletoolstest.cpp
    vibecuttest.cpp
    vibecuttoolpolicytest.cpp
    vibecuttoolsurfacetest.cpp
)

include(ECMAddTests)

foreach(_source ${KdenliveTest_SOURCES})
  get_filename_component(_targetname ${_source} NAME_WE)
  ecm_add_test(
      TestMain.cpp
      test_utils.cpp
      abortutil.cpp
      ${_source}
      TEST_NAME ${_targetname}
      LINK_LIBRARIES kdenliveLib
  )
  set_property(TARGET ${_targetname} PROPERTY CXX_STANDARD 14)
endforeach()
