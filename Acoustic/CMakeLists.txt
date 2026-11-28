cmake_minimum_required(VERSION 3.16)
set(amp_config "${CMAKE_CURRENT_SOURCE_DIR}/../NeuralAmpModeler/config.h")
file(STRINGS "${amp_config}" version_line REGEX "^#define PLUG_VERSION_STR ")
file(STRINGS "${amp_config}" version_hex_line REGEX "^#define PLUG_VERSION_HEX ")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${amp_config}")
string(REGEX REPLACE "^#define PLUG_VERSION_STR \"([0-9]+\\.[0-9]+\\.[0-9]+)\"" "\\1" acoustic_version "${version_line}")
string(REGEX REPLACE "^#define PLUG_VERSION_HEX (0x[0-9A-Fa-f]+)" "\\1" acoustic_version_hex "${version_hex_line}")
math(EXPR acoustic_version_integer "${acoustic_version_hex}")
project(PukeAcoustic VERSION "${acoustic_version}" LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(root "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(ip "${root}/iPlug2/IPlug")
set(g "${root}/iPlug2/IGraphics")
set(deps "${root}/iPlug2/Dependencies")
set(gdeps "${deps}/IGraphics")
set(sdk "${deps}/IPlug/VST3_SDK")
set(art "${root}/Models/GUI/instruments/peararchtop.png")
set(font "${root}/NeuralAmpModeler/resources/fonts/Roboto-Regular.ttf")

add_library(acoustic_ir STATIC "${root}/AudioDSPTools/dsp/dsp.cpp"
  "${root}/AudioDSPTools/dsp/ImpulseResponse.cpp" "${root}/AudioDSPTools/dsp/wav.cpp")
target_include_directories(acoustic_ir PUBLIC "${root}/eigen" "${root}/NeuralAmpModeler")
if(MSVC)
  target_compile_definitions(acoustic_ir PUBLIC NOMINMAX)
endif()

if(APPLE)
  enable_language(OBJC OBJCXX)
  set(CMAKE_OBJCXX_STANDARD 17)
  set(formats VST3 AU)
elseif(WIN32)
  enable_language(RC)
  set(formats VST3)
else()
  message(STATUS "Native plugin targets require macOS or Windows. Building the shared mixer checks.")
  enable_testing()
  add_executable(acoustic_check "${root}/NeuralAmpModeler/tests/cabinet_mixer_test.cpp")
  target_compile_options(acoustic_check PRIVATE -UNDEBUG)
  target_link_libraries(acoustic_check PRIVATE acoustic_ir pthread)
  add_test(NAME acoustic_pair COMMAND acoustic_check
    "${root}/Models/Instruments/AcousticGuitar/BR_M149_Fingerstyle_Center.wav" "${root}/Models/Instruments/AcousticGuitar/BR_M149_Strumming_Center.wav")
  return()
endif()

set(instruments "${root}/Models/Instruments")
file(GLOB_RECURSE presets CONFIGURE_DEPENDS RELATIVE "${instruments}/Presets"
  "${instruments}/Presets/*.[fF][xX][pP]")

foreach(format IN LISTS formats)
  set(target Acoustic${format})
  add_library(${target} MODULE PukeAcoustic.cpp
    ${ip}/IPlugAPIBase.cpp ${ip}/IPlugParameter.cpp ${ip}/IPlugProcessor.cpp
    ${ip}/IPlugPluginBase.cpp ${ip}/IPlugTimer.cpp
    ${g}/IGraphics.cpp ${g}/IControl.cpp ${g}/IGraphicsEditorDelegate.cpp
    ${g}/Controls/IControls.cpp ${g}/Controls/IPopupMenuControl.cpp ${g}/Controls/ITextEntryControl.cpp)
  target_link_libraries(${target} PRIVATE acoustic_ir)
  target_compile_definitions(${target} PRIVATE ${format}_API IPLUG_EDITOR=1 IPLUG_DSP=1
    PLUG_VERSION_STR="${PROJECT_VERSION}" PLUG_VERSION_HEX=${acoustic_version_hex}
    IGRAPHICS_NANOVG NOMINMAX $<$<CONFIG:Debug>:DEVELOPMENT=1> $<$<NOT:$<CONFIG:Debug>>:RELEASE=1>)
  target_include_directories(${target} PRIVATE . ${ip} ${ip}/Extras ${ip}/VST3 ${ip}/AUv2
    ${root}/iPlug2/WDL ${g} ${g}/Platforms ${g}/Drawing ${g}/Controls
    ${g}/Extras ${gdeps}/NanoSVG/src ${gdeps}/NanoVG/src ${gdeps}/STB)

  if(format STREQUAL "VST3")
    set(ext vst3)
    target_include_directories(${target} PRIVATE ${sdk})
    target_sources(${target} PRIVATE ${ip}/VST3/IPlugVST3.cpp ${ip}/VST3/IPlugVST3_ProcessorBase.cpp)
    set(sdk_sources
      base/source/baseiids base/source/fbuffer base/source/fdebug base/source/fdynlib
      base/source/fobject base/source/fstreamer base/source/fstring base/source/timer base/source/updatehandler
      base/thread/source/fcondition base/thread/source/flock pluginterfaces/base/conststringtable
      pluginterfaces/base/coreiids pluginterfaces/base/funknown pluginterfaces/base/ustring
      public.sdk/source/common/commoniids public.sdk/source/common/memorystream public.sdk/source/common/pluginview
      public.sdk/source/main/pluginfactory public.sdk/source/vst/vstaudioeffect public.sdk/source/vst/vstbus
      public.sdk/source/vst/vstcomponent public.sdk/source/vst/vstcomponentbase public.sdk/source/vst/vstinitiids
      public.sdk/source/vst/vstparameters public.sdk/source/vst/vstsinglecomponenteffect)
    if(APPLE)
      list(APPEND sdk_sources public.sdk/source/main/macmain)
    else()
      list(APPEND sdk_sources public.sdk/source/main/dllmain)
    endif()
    foreach(src IN LISTS sdk_sources)
      target_sources(${target} PRIVATE "${sdk}/${src}.cpp")
    endforeach()
  else()
    set(ext component)
    target_sources(${target} PRIVATE ${ip}/AUv2/IPlugAU.cpp ${ip}/AUv2/IPlugAU_view_factory.mm
      ${ip}/AUv2/dfx-au-utilities.c)
  endif()

  set(bundle "${CMAKE_CURRENT_BINARY_DIR}/products/PukeAcoustic.${ext}")
  set(resources "${bundle}/Contents/Resources")
  if(APPLE)
    set(binary_dir "${bundle}/Contents/MacOS")
    set(suffix "")
    configure_file("${format}-Info.plist" "${bundle}/Contents/Info.plist" @ONLY)
    foreach(logo puke rat)
      configure_file("${root}/NeuralAmpModeler/resources/img/${logo}.svg" "${resources}/${logo}.svg" COPYONLY)
    endforeach()
    configure_file("${art}" "${resources}/peararchtop.png" COPYONLY)
    configure_file("${root}/NeuralAmpModeler/resources/fonts/Roboto-Bold.ttf" "${resources}/Roboto-Bold.ttf" COPYONLY)
    configure_file("${font}" "${resources}/Roboto-Regular.ttf" COPYONLY)
    target_sources(${target} PRIVATE ${ip}/IPlugPaths.mm ${g}/Platforms/IGraphicsMac.mm
      ${g}/Platforms/IGraphicsMac_view.mm ${g}/Platforms/IGraphicsCoreText.mm ${g}/Drawing/IGraphicsNanoVG_src.m)
    set_source_files_properties(${g}/Drawing/IGraphicsNanoVG_src.m PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_compile_options(${target} PRIVATE -include "${ip}/IPlugOBJCPrefix.pch")
    target_compile_definitions(${target} PRIVATE IGRAPHICS_METAL OBJC_PREFIX=vPukeAcoustic SWELL_CLEANUP_ON_UNLOAD)
    target_include_directories(${target} PRIVATE ${root}/iPlug2/WDL/swell ${gdeps}/MetalNanoVG/src)
    foreach(framework Cocoa Carbon CoreFoundation CoreData Foundation CoreServices QuartzCore Metal MetalKit
        CoreGraphics CoreText CoreVideo AudioToolbox AudioUnit CoreAudio CoreMIDI Accelerate)
      target_link_libraries(${target} PRIVATE "-framework ${framework}")
    endforeach()
  else()
    set(binary_dir "${bundle}/Contents/x86_64-win")
    set(suffix ".vst3")
    configure_file(resources.rc.in resources.rc @ONLY)
    target_sources(${target} PRIVATE ${ip}/IPlugPaths.cpp ${g}/Platforms/IGraphicsWin.cpp
      "${CMAKE_CURRENT_BINARY_DIR}/resources.rc")
    # IGraphicsWin relies on shell, common-dialog, and NT declarations from windows.h.
    target_compile_definitions(${target} PRIVATE IGRAPHICS_GL2 _CRT_SECURE_NO_WARNINGS)
    target_compile_options(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
    target_include_directories(${target} PRIVATE ${gdeps}/glad_GL2/include ${gdeps}/glad_GL2/src
      ${deps}/Build/src/freetype/include)
    target_link_directories(${target} PRIVATE "${deps}/Build/win/x64/$<CONFIG>")
    target_link_libraries(${target} PRIVATE comctl32 comdlg32 shell32 wininet shlwapi winmm opengl32)
  endif()
  set_target_properties(${target} PROPERTIES OUTPUT_NAME PukeAcoustic PREFIX "" SUFFIX "${suffix}"
    LIBRARY_OUTPUT_DIRECTORY "${binary_dir}/$<0:>" RUNTIME_OUTPUT_DIRECTORY "${binary_dir}/$<0:>")
  # Remove deleted presets from incremental builds before copying the current set.
  file(REMOVE_RECURSE "${resources}/Models/Instruments/Presets")
  foreach(preset IN LISTS presets)
    configure_file("${instruments}/Presets/${preset}" "${resources}/Models/Instruments/Presets/${preset}" COPYONLY)
  endforeach()
  configure_file("${root}/NeuralAmpModeler/installer/ThirdPartyNotices.txt"
    "${resources}/ThirdPartyNotices.txt" COPYONLY)
  file(GLOB_RECURSE captures CONFIGURE_DEPENDS RELATIVE "${instruments}" "${instruments}/*.[wW][aA][vV]")
  file(GLOB mic_images CONFIGURE_DEPENDS "${root}/Models/GUI/mics/*.png")
  set(capture_mics)
  foreach(capture IN LISTS captures)
    get_filename_component(stem "${capture}" NAME_WE)
    if(stem MATCHES "^[^_]+_([^_]+)_[^_]+(_(Left|Right|Center))?$")
      string(TOLOWER "${CMAKE_MATCH_1}" mic)
      list(APPEND capture_mics "${mic}")
    endif()
    configure_file("${instruments}/${capture}" "${resources}/Models/Instruments/${capture}" COPYONLY)
  endforeach()
  foreach(image IN LISTS mic_images)
    get_filename_component(stem "${image}" NAME_WE)
    string(TOLOWER "${stem}" mic)
    if(mic IN_LIST capture_mics)
      configure_file("${image}" "${resources}/Models/GUI/mics/${stem}.png" COPYONLY)
    endif()
  endforeach()
endforeach()
