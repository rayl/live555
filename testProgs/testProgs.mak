INCLUDES = -I../UsageEnvironment/include -I../groupsock/include -I../liveMedia/include -I../BasicUsageEnvironment/include
##### Change the following for your environment: 
# Comment out the following line to produce Makefiles that generate debuggable code:
NODEBUG=1

# The following definition ensures that we are properly matching
# the WinSock2 library file with the correct header files.
# (will link with "ws2_32.lib" and include "winsock2.h" & "Ws2tcpip.h")
TARGETOS = WINNT

# If for some reason you wish to use WinSock1 instead, uncomment the
# following two definitions.
# (will link with "wsock32.lib" and include "winsock.h")
#TARGETOS = WIN95
#APPVER = 4.0

!include    <ntwin32.mak>

UI_OPTS =		$(guilflags) $(guilibsdll)
# Use the following to get a console (e.g., for debugging):
CONSOLE_UI_OPTS =		$(conlflags) $(conlibsdll)
CPU=i386

TOOLS32	=		c:\Program Files\DevStudio\Vc
COMPILE_OPTS =		$(INCLUDES) $(cdebug) $(cflags) $(cvarsdll) -I. -I"$(TOOLS32)\include"
C =			c
C_COMPILER =		"$(TOOLS32)\bin\cl"
C_FLAGS =		$(COMPILE_OPTS)
CPP =			cpp
CPLUSPLUS_COMPILER =	$(C_COMPILER)
CPLUSPLUS_FLAGS =	$(COMPILE_OPTS)
OBJ =			obj
LINK =			$(link) -out:
LIBRARY_LINK =		lib -out:
LINK_OPTS_0 =		$(linkdebug) msvcirt.lib
LIBRARY_LINK_OPTS =	
LINK_OPTS =		$(LINK_OPTS_0) $(UI_OPTS)
CONSOLE_LINK_OPTS =	$(LINK_OPTS_0) $(CONSOLE_UI_OPTS)
SERVICE_LINK_OPTS =     kernel32.lib advapi32.lib shell32.lib -subsystem:console,$(APPVER)
LIB_SUFFIX =		lib
LIBS_FOR_CONSOLE_APPLICATION =
LIBS_FOR_GUI_APPLICATION =
MULTIMEDIA_LIBS =	winmm.lib
EXE =			.exe
PLATFORM = Windows

rc32 = "$(TOOLS32)\bin\rc"
.rc.res:
	$(rc32) $<
##### End of variables to change

MULTICAST_STREAMER_APPS = testMP3Streamer$(EXE) testMPEG1or2VideoStreamer$(EXE) testMPEG1or2AudioVideoStreamer$(EXE) testMPEG2TransportStreamer$(EXE) testMPEG4VideoStreamer$(EXE) testWAVAudioStreamer$(EXE) testAMRAudioStreamer$(EXE) vobStreamer$(EXE)
MULTICAST_RECEIVER_APPS = testMP3Receiver$(EXE) testMPEG1or2VideoReceiver$(EXE) sapWatch$(EXE)
MULTICAST_MISC_APPS = testRelay$(EXE)
MULTICAST_APPS = $(MULTICAST_STREAMER_APPS) $(MULTICAST_RECEIVER_APPS) $(MULTICAST_MISC_APPS)

UNICAST_STREAMER_APPS = testOnDemandRTSPServer$(EXE) testMPEG1or2AudioVideoToDarwin$(EXE) testMPEG4VideoToDarwin$(EXE)
UNICAST_RECEIVER_APPS = openRTSP$(EXE) playSIP$(EXE)
UNICAST_APPS = $(UNICAST_STREAMER_APPS) $(UNICAST_RECEIVER_APPS)

MISC_APPS = testMPEG1or2Splitter$(EXE) testMPEG1or2ProgramToTransportStream$(EXE)

ALL = $(MULTICAST_APPS) $(UNICAST_APPS) $(MISC_APPS)
all: $(ALL)

extra:	testGSMStreamer$(EXE)

.$(C).$(OBJ):
	$(C_COMPILER) -c $(C_FLAGS) $<       

.$(CPP).$(OBJ):
	$(CPLUSPLUS_COMPILER) -c $(CPLUSPLUS_FLAGS) $<

MP3_STREAMER_OBJS = testMP3Streamer.$(OBJ)
MP3_RECEIVER_OBJS = testMP3Receiver.$(OBJ)
RELAY_OBJS = testRelay.$(OBJ)
MPEG_1OR2_SPLITTER_OBJS = testMPEG1or2Splitter.$(OBJ)
MPEG_1OR2_VIDEO_STREAMER_OBJS = testMPEG1or2VideoStreamer.$(OBJ)
MPEG_1OR2_VIDEO_RECEIVER_OBJS = testMPEG1or2VideoReceiver.$(OBJ)
MPEG_1OR2_AUDIO_VIDEO_STREAMER_OBJS = testMPEG1or2AudioVideoStreamer.$(OBJ)
MPEG2_TRANSPORT_STREAMER_OBJS = testMPEG2TransportStreamer.$(OBJ)
MPEG4_VIDEO_STREAMER_OBJS = testMPEG4VideoStreamer.$(OBJ)
WAV_AUDIO_STREAMER_OBJS = testWAVAudioStreamer.$(OBJ)
AMR_AUDIO_STREAMER_OBJS	= testAMRAudioStreamer.$(OBJ)
ON_DEMAND_RTSP_SERVER_OBJS	= testOnDemandRTSPServer.$(OBJ)
VOB_STREAMER_OBJS	= vobStreamer.$(OBJ)
OPEN_RTSP_OBJS    = openRTSP.$(OBJ) playCommon.$(OBJ)
PLAY_SIP_OBJS     = playSIP.$(OBJ) playCommon.$(OBJ)
SAP_WATCH_OBJS = sapWatch.$(OBJ)
MPEG_1OR2_AUDIO_VIDEO_TO_DARWIN_OBJS = testMPEG1or2AudioVideoToDarwin.$(OBJ)
MPEG_4_VIDEO_TO_DARWIN_OBJS = testMPEG4VideoToDarwin.$(OBJ)
MPEG_1OR2_PROGRAM_TO_TRANSPORT_STREAM_OBJS = testMPEG1or2ProgramToTransportStream.$(OBJ)

GSM_STREAMER_OBJS = testGSMStreamer.$(OBJ) testGSMEncoder.$(OBJ)

openRTSP.$(CPP):	playCommon.hh
playCommon.$(CPP):	playCommon.hh
playSIP.$(CPP):		playCommon.hh

USAGE_ENVIRONMENT_DIR = ../UsageEnvironment
USAGE_ENVIRONMENT_LIB = $(USAGE_ENVIRONMENT_DIR)/libUsageEnvironment.$(LIB_SUFFIX)
BASIC_USAGE_ENVIRONMENT_DIR = ../BasicUsageEnvironment
BASIC_USAGE_ENVIRONMENT_LIB = $(BASIC_USAGE_ENVIRONMENT_DIR)/libBasicUsageEnvironment.$(LIB_SUFFIX)
LIVEMEDIA_DIR = ../liveMedia
LIVEMEDIA_LIB = $(LIVEMEDIA_DIR)/libliveMedia.$(LIB_SUFFIX)
GROUPSOCK_DIR = ../groupsock
GROUPSOCK_LIB = $(GROUPSOCK_DIR)/libgroupsock.$(LIB_SUFFIX)
LOCAL_LIBS =	$(LIVEMEDIA_LIB) $(GROUPSOCK_LIB) \
		$(USAGE_ENVIRONMENT_LIB) $(BASIC_USAGE_ENVIRONMENT_LIB)
LIBS =			$(LOCAL_LIBS) $(LIBS_FOR_CONSOLE_APPLICATION)

testMP3Streamer$(EXE):	$(MP3_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MP3_STREAMER_OBJS) $(LIBS)
testMP3Receiver$(EXE):	$(MP3_RECEIVER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MP3_RECEIVER_OBJS) $(LIBS)
testRelay$(EXE):	$(RELAY_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(RELAY_OBJS) $(LIBS)
testMPEG1or2Splitter$(EXE):	$(MPEG_1OR2_SPLITTER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_SPLITTER_OBJS) $(LIBS)
testMPEG1or2VideoStreamer$(EXE):	$(MPEG_1OR2_VIDEO_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_VIDEO_STREAMER_OBJS) $(LIBS)
testMPEG1or2VideoReceiver$(EXE):	$(MPEG_1OR2_VIDEO_RECEIVER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_VIDEO_RECEIVER_OBJS) $(LIBS)
testMPEG1or2AudioVideoStreamer$(EXE):	$(MPEG_1OR2_AUDIO_VIDEO_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_AUDIO_VIDEO_STREAMER_OBJS) $(LIBS)
testMPEG2TransportStreamer$(EXE):	$(MPEG2_TRANSPORT_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG2_TRANSPORT_STREAMER_OBJS) $(LIBS)
testMPEG4VideoStreamer$(EXE):	$(MPEG4_VIDEO_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG4_VIDEO_STREAMER_OBJS) $(LIBS)
testWAVAudioStreamer$(EXE):	$(WAV_AUDIO_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(WAV_AUDIO_STREAMER_OBJS) $(LIBS)
testAMRAudioStreamer$(EXE):	$(AMR_AUDIO_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(AMR_AUDIO_STREAMER_OBJS) $(LIBS)
testOnDemandRTSPServer$(EXE):	$(ON_DEMAND_RTSP_SERVER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(ON_DEMAND_RTSP_SERVER_OBJS) $(LIBS)
vobStreamer$(EXE):	$(VOB_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(VOB_STREAMER_OBJS) $(LIBS)
openRTSP$(EXE):	$(OPEN_RTSP_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(OPEN_RTSP_OBJS) $(LIBS)
playSIP$(EXE):	$(PLAY_SIP_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(PLAY_SIP_OBJS) $(LIBS)
sapWatch$(EXE):	$(SAP_WATCH_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(SAP_WATCH_OBJS) $(LIBS)
testMPEG1or2AudioVideoToDarwin$(EXE):	$(MPEG_1OR2_AUDIO_VIDEO_TO_DARWIN_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_AUDIO_VIDEO_TO_DARWIN_OBJS) $(LIBS)
testMPEG4VideoToDarwin$(EXE):	$(MPEG_4_VIDEO_TO_DARWIN_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_4_VIDEO_TO_DARWIN_OBJS) $(LIBS)
testMPEG1or2ProgramToTransportStream$(EXE):	$(MPEG_1OR2_PROGRAM_TO_TRANSPORT_STREAM_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(MPEG_1OR2_PROGRAM_TO_TRANSPORT_STREAM_OBJS) $(LIBS)

testGSMStreamer$(EXE):	$(GSM_STREAMER_OBJS) $(LOCAL_LIBS)
	$(LINK)$@ $(CONSOLE_LINK_OPTS) $(GSM_STREAMER_OBJS) $(LIBS)

clean:
	-rm -rf *.$(OBJ) $(ALL) core *.core *~ include/*~

##### Any additional, platform-specific rules come here:
