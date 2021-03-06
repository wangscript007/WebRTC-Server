/*
 * WebRTC.cpp
 *
 *  Created on: 2019/07/02
 *      Author: max
 *		Email: Kingsleyyau@gmail.com
 */

#include "WebRTC.h"

// System
#include <signal.h>

#include <include/CommonHeader.h>

// Common
#include <common/LogManager.h>
#include <common/StringHandle.h>
#include <common/CommonFunc.h>

// ThirdParty
#include <libsdp.h>

namespace mediaserver {

WebRTC::WebRTC()
:mClientMutex(KMutex::MutexType_Recursive),
 mRtpTransformPidMutex(KMutex::MutexType_Recursive) {
	// TODO Auto-generated constructor stub
	mIceClient.SetCallback(this);
	mDtlsSession.SetSocketSender(this);
	mRtpSession.SetRtpSender(this);
	mRtpSession.SetRtcpSender(this);

	mpWebRTCCallback = NULL;

	mVideoSSRC = 0;
	mAudioSSRC = 0;

	mRtpDstAudioPort = 0;
	mRtpDstVideoPort = 0;

	mRtp2RtmpShellFilePath = "";
	mRtmpUrl = "";

	mpSdpFile = NULL;
	mSdpFilePath = "";
	mRtpTransformPid = 0;

	mNeedTranscodeVideo = true;

	mRunning = false;
}

WebRTC::~WebRTC() {
	// TODO Auto-generated destructor stub
}

bool WebRTC::GobalInit(
		const string& certPath,
		const string& keyPath,
		const string& stunServerIp,
		const string& localIp,
		const string& turnUserName,
		const string& turnPassword
		) {
	bool bFlag = true;

	bFlag &= IceClient::GobalInit(stunServerIp, localIp, turnUserName, turnPassword);
	bFlag &= DtlsSession::GobalInit(certPath, keyPath);
	bFlag &= RtpSession::GobalInit();

	LogAync(
			LOG_ERR_USER,
			"WebRTC::GobalInit( "
			"[%s] "
			")",
			FLAG_2_STRING(bFlag)
			);

	return bFlag;
}

void WebRTC::SetCallback(WebRTCCallback *callback) {
	mpWebRTCCallback = callback;
}

bool WebRTC::Init(
		const string& rtp2RtmpShellFilePath,
		const string& rtpDstAudioIp,
		unsigned int rtpDstAudioPort,
		const string& rtpDstVideoIp,
		unsigned int rtpDstVideoPort
		) {
	bool bFlag = true;
	bFlag &= mRtpDstAudioClient.Init(rtpDstAudioIp, rtpDstAudioPort, rtpDstAudioPort);
	bFlag &= mRtpDstVideoClient.Init(rtpDstVideoIp, rtpDstVideoPort, rtpDstVideoPort);

	if ( bFlag ) {
		mRtpDstAudioIp = rtpDstAudioIp;
		mRtpDstAudioPort = rtpDstAudioPort;
		mRtpDstVideoIp = rtpDstVideoIp;
		mRtpDstVideoPort = rtpDstVideoPort;
		mRtp2RtmpShellFilePath = rtp2RtmpShellFilePath;
	}

	LogAync(
			LOG_STAT,
			"WebRTC::Init( "
			"this : %p, "
			"[%s], "
			"rtpDstAudioIp : %s, "
			"rtpDstAudioPort : %u, "
			"rtpDstVideoIp : %s, "
			"rtpDstVideoPort : %u "
			")",
			this,
			FLAG_2_STRING(bFlag),
			rtpDstAudioIp.c_str(),
			rtpDstAudioPort,
			rtpDstVideoIp.c_str(),
			rtpDstVideoPort
			);

	return bFlag;
}

bool WebRTC::Start(
		const string& sdp,
		const string& rtmpUrl
		) {
	bool bFlag = true;

	mClientMutex.lock();
	if( mRunning ) {
		Stop();
	}

	mRunning = true;

	// Reset Param
	mNeedTranscodeVideo = true;
	mWebRTCMediaType = WebRTCMediaType_None;

	// Start Modules
	bFlag &= ParseRemoteSdp(sdp);
	bFlag &= mIceClient.Start();
	bFlag &= mDtlsSession.Start();

	bFlag &= mRtpDstAudioClient.Start(NULL, 0, NULL, 0);
	bFlag &= mRtpDstVideoClient.Start(NULL, 0, NULL, 0);

	string tmpDir = "/tmp/webrtc";
	MakeDir(tmpDir);
	char sdpFilePathTmp[MAX_PATH] = {'0'};
	snprintf(sdpFilePathTmp, sizeof(sdpFilePathTmp) - 1, "%s/%d_%d.sdp", tmpDir.c_str(), mRtpDstAudioPort, mRtpDstVideoPort);
	mSdpFilePath = sdpFilePathTmp;

	mRtmpUrl = rtmpUrl;

	LogAync(
			LOG_WARNING,
			"WebRTC::Start( "
			"this : %p, "
			"[%s], "
			"rtmpUrl : %s "
			")",
			this,
			FLAG_2_STRING(bFlag),
			rtmpUrl.c_str()
			);

	if( !bFlag ) {
		Stop();
	}

	mClientMutex.unlock();

	return bFlag;
}

void WebRTC::Stop() {
	mClientMutex.lock();
	if( mRunning ) {
		LogAync(
				LOG_MSG,
				"WebRTC::Stop( "
				"this : %p "
				")",
				this
				);

		// 停止媒体流服务
		mIceClient.Stop();
		mDtlsSession.Stop();
		mRtpSession.Stop();
		mRtpDstAudioClient.Stop();
		mRtpDstVideoClient.Stop();

		// 停止转发RTMP
		StopRtpTransform();

		// 还原参数
		mAudioSSRC = 0;
		mVideoSSRC = 0;

		mRunning = false;

		LogAync(
				LOG_WARNING,
				"WebRTC::Stop( "
				"this : %p, "
				"[OK], "
				"rtmpUrl : %s "
				")",
				this,
				mRtmpUrl.c_str()
				);
	}
	mClientMutex.unlock();
}

void WebRTC::UpdateCandidate(const string& sdp) {
//	LogAync(
//			LOG_STAT,
//			"WebRTC::UpdateCandidate( "
//			"this : %p, "
//			"sdp : %s "
//			")",
//			this,
//			sdp.c_str()
//			);

	mIceClient.SetRemoteSdp(sdp);
}

string WebRTC::GetRtmpUrl() {
	return mRtmpUrl;
}

bool WebRTC::ParseRemoteSdp(const string& sdp) {
	LogAync(
			LOG_MSG,
			"WebRTC::ParseRemoteSdp( "
			"this : %p, "
			"sdp : \n%s"
			")",
			this,
			sdp.c_str()
			);

	bool bFlag = false;

	struct sdp_session *session = NULL;
	int err = -1;
	err = sdp_description_read(sdp.c_str(), &session);
	if( err == 0 ) {
		bFlag = true;
		list_node node;
		sdp_attr *attr = NULL;

		list_walk_entry_forward(&session->attrs, attr, node) {
			if( attr->key && attr->value ) {
				string key(attr->key);
				string value(attr->value);

				if( key == "group" ) {
					LogAync(
							LOG_MSG,
							"WebRTC::ParseRemoteSdp( "
							"this : %p, "
							"[Found Remote Group Bundle], "
							"%s:%s "
							")",
							this,
							key.c_str(),
							value.c_str()
							);

					vector<string> group = StringHandle::splitWithVector(value, " ");
					if( group.size() > 2 ) {
						mAudioMid = group[1];
						mVideoMid = group[2];
					} else if ( group.size() == 2 ) {
						mAudioMid = mVideoMid = group[1];
					}
				}

			}
		}

		LogAync(
				LOG_STAT,
				"WebRTC::ParseRemoteSdp( "
				"this : %p, "
				"media_count : %d "
				")",
				this,
				session->media_count
				);
		sdp_media *media = NULL;
		list_walk_entry_forward(&session->medias, media, node) {
			if ( media ) {
				LogAync(
						LOG_MSG,
						"WebRTC::ParseRemoteSdp( "
						"this : %p, "
						"[Found Remote Media], "
						"media_type : %s, "
						"media_attr_count : %d "
						")",
						this,
						sdp_media_type_str(media->type),
						media->attr_count
						);

				if ( session->media_count == 2 ) {
					mWebRTCMediaType = WebRTCMediaType_BothVideoAudio;
				} else {
					if ( media->type == SDP_MEDIA_TYPE_AUDIO ) {
						mWebRTCMediaType = WebRTCMediaType_OnlyAudio;
					} else {
						mWebRTCMediaType = WebRTCMediaType_OnlyVideo;
					}
				}

				for(int i = 0; i < (int)media->payload_type_array_count; i++) {
					sdp_payload payload = media->payload_type_array[i];
					LogAync(
							LOG_STAT,
							"WebRTC::ParseRemoteSdp( "
							"this : %p, "
							"media_type : %s, "
							"payload[%d] : [%d %s/%u/%s %s] "
							")",
							this,
							sdp_media_type_str(media->type),
							i,
							payload.payload_type,
							payload.encoding_name,
							payload.clock_rate,
							payload.encoding_params,
							payload.fmtp
							);

					if ( 0 == strcmp(payload.encoding_name, "H264") ) {
						LogAync(
								LOG_WARNING,
								"WebRTC::ParseRemoteSdp( "
								"this : %p, "
								"[Found Remote Media H264 Codec], "
								"media_type : %s, "
								"payload : %d %s/%u/%s, "
								"fmtp : %s "
								")",
								this,
								sdp_media_type_str(media->type),
								payload.payload_type,
								payload.encoding_name,
								payload.clock_rate,
								payload.encoding_params,
								payload.fmtp
								);

						if ( mNeedTranscodeVideo ) {
							mVideoSdpPayload.payload_type = payload.payload_type;
							mVideoSdpPayload.encoding_name = payload.encoding_name?payload.encoding_name:"";
							mVideoSdpPayload.clock_rate = payload.clock_rate;
							mVideoSdpPayload.encoding_params = payload.encoding_params?payload.encoding_params:"";
							mVideoSdpPayload.fmtp = payload.fmtp?payload.fmtp:"";

							string fmtp = payload.fmtp;
							size_t start = fmtp.find("profile-level-id=");
							if ( string::npos != start ) {
								size_t end = fmtp.find(";", start);
								end = (string::npos != end)?end:fmtp.length();

								int len = strlen("profile-level-id=");
								string profileLevelId = fmtp.substr(start + len, end - start - len);

								LogAync(
										LOG_STAT,
										"WebRTC::ParseRemoteSdp( "
										"this : %p, "
										"[Found Remote Media H264 Codec, Check Level Id], "
										"profileLevelId : %s "
										")",
										this,
										profileLevelId.c_str()
										);

								if ( profileLevelId.length() == 6 ) {
									string profileIdc = profileLevelId.substr(0, 2);
									LogAync(
											LOG_STAT,
											"WebRTC::ParseRemoteSdp( "
											"this : %p, "
											"[Found Remote Media H264 Codec, Check Idc], "
											"profileIdc : %s "
											")",
											this,
											profileIdc.c_str()
											);
									/**
									 * We choose Baseline for the first option
									 * Example:
									 * 		profile-level-id=42C01E : Baseline profile 3.0
									 *
									 * profile_idc(8 bits)
									 * 0x42(66) Baseline profile
									 * 0x4D(77) Main profile
									 * 0x58(88) Extended profile
									 *
									 * compatiable(8 bits)
									 *
									 * level_idc(8 bits)
									 * 等级	最大比特率(BP、MP、EP)kbit/s	高分辨率示例@最高帧率(最大存储帧)
									 * 3 	10000 	[352*480@61.4(12) | 352*576@51.1(10) | 720*480@30.0(6) | 720*576@25.0(5)]
									 * 3.1 	14000 	[720*480@80.0(13) | 720*576@66.7(11) | 1280*720@30.0(5)]
									 * 3.2	20000	[1280*720@60.0(5) | 1280*1024@42.2(4)]
									 */
									if ( profileIdc == "42" ) {
										mNeedTranscodeVideo = false;

										LogAync(
												LOG_WARNING,
												"WebRTC::ParseRemoteSdp( "
												"this : %p, "
												"[Found Remote Media H264 Codec, Relay Without Transcode], "
												"media_type : %s, "
												"payload : %d %s/%u/%s, "
												"profileLevelId : %s "
												")",
												this,
												sdp_media_type_str(media->type),
												payload.payload_type,
												payload.encoding_name,
												payload.clock_rate,
												payload.encoding_params,
												profileLevelId.c_str()
												);
										break;
									}
								}
							}
						}
					} else if ( 0 == strcmp(payload.encoding_name, "opus") ) {
						LogAync(
								LOG_WARNING,
								"WebRTC::ParseRemoteSdp( "
								"this : %p, "
								"[Found Remote Media OPUS Codec], "
								"media_type : %s, "
								"payload : %d %s/%u/%s, "
								"fmtp : %s "
								")",
								this,
								sdp_media_type_str(media->type),
								payload.payload_type,
								payload.encoding_name,
								payload.clock_rate,
								payload.encoding_params,
								payload.fmtp
								);

						mAudioSdpPayload.payload_type = payload.payload_type;
						mAudioSdpPayload.encoding_name = payload.encoding_name?payload.encoding_name:"";
						mAudioSdpPayload.clock_rate = payload.clock_rate;
						mAudioSdpPayload.encoding_params = payload.encoding_params?payload.encoding_params:"";
						mAudioSdpPayload.fmtp = payload.fmtp?payload.fmtp:"";
						break;
					}
				}

				list_walk_entry_forward(&media->attrs, attr, node) {
					if( attr->key && attr->value ) {
						string key(attr->key);
						string value(attr->value);

						LogAync(
								LOG_STAT,
								"WebRTC::ParseRemoteSdp( "
								"this : %p, "
								"media_type : %s, "
								"attr : [%s %s] "
								")",
								this,
								sdp_media_type_str(media->type),
								key.c_str(),
								value.c_str()
								);

						if ( key == "ssrc" ) {
							string::size_type pos = value.find(" ", 0);
							if( pos != string::npos ) {
								string ssrc = value.substr(0, pos);
								if ( media->type == SDP_MEDIA_TYPE_AUDIO ) {
									if ( mAudioSSRC == 0 ) {
										mAudioSSRC = atoll(ssrc.c_str());
										break;
									}
								} else if ( media->type == SDP_MEDIA_TYPE_VIDEO ) {
									if ( mVideoSSRC == 0 ) {
										mVideoSSRC = atoll(ssrc.c_str());
										break;
									}
								}
							}
						} else if( key == "rtcp-fb" ) {
							string::size_type pos = value.find(" ", 0);
							if( pos != string::npos ) {
								string payload = value.substr(0, pos);

								if ( media->type == SDP_MEDIA_TYPE_AUDIO ) {
//									if( mAudioSdpPayload.payload_type == atoi(payload.c_str()) ) {
//										string rtcpFb = key + ":" + value;
//
//										LogAync(
//												LOG_MSG,
//												"WebRTC::ParseRemoteSdp( "
//												"this : %p, "
//												"[Found Remote Audio RTCP Feedback], "
//												"media_type : %s, "
//												"%s "
//												")",
//												this,
//												sdp_media_type_str(media->type),
//												rtcpFb.c_str()
//												);
//
//										mAudioRtcpFbList.push_back(rtcpFb);
//									}
								} else if ( media->type == SDP_MEDIA_TYPE_VIDEO ) {
									if( (int)mVideoSdpPayload.payload_type == atoi(payload.c_str()) ) {
										string rtcpFb = key + ":" + value;

										LogAync(
												LOG_MSG,
												"WebRTC::ParseRemoteSdp( "
												"this : %p, "
												"[Found Remote Video RTCP Feedback], "
												"media_type : %s, "
												"%s "
												")",
												this,
												sdp_media_type_str(media->type),
												rtcpFb.c_str()
												);

										mVideoRtcpFbList.push_back(rtcpFb);
									}
								}
							}
						}
					}
				}
			}
		}

		LogAync(
				LOG_MSG,
				"WebRTC::ParseRemoteSdp( "
				"this : %p, "
				"[Parse Remote SDP OK], "
				"mVideoSdpPayload : %d %s/%u, "
				"fmtp : %s, "
				"mAudioSSRC : %u, "
				"mAudioMid : %s, "
				"mVideoSSRC : %u, "
				"mVideoMid : %s "
				")",
				this,
				mVideoSdpPayload.payload_type,
				mVideoSdpPayload.encoding_name.c_str(),
				mVideoSdpPayload.clock_rate,
				mVideoSdpPayload.fmtp.c_str(),
				mAudioSSRC,
				mAudioMid.c_str(),
				mVideoSSRC,
				mVideoMid.c_str()
				);
	}

	if( bFlag ) {
		mIceClient.SetRemoteSdp(sdp);
	}

	return bFlag;
}

string WebRTC::CreateLocalSdp() {
	/**
	 * 这里必须把视频放前面, 否则FFMPEG会解析不了视频
	 */
	char header[1024] = {'0'};
	snprintf(header, sizeof(header) - 1,
			"SDP:"
			"v=0\n"
			"o=- 0 0 IN IP4 127.0.0.1\n"
			"s=No Name\n"
			"t=0 0\n"
	);

	char video[2048] = {'0'};
	char audio[2048] = {'0'};

	char sdp[4096] = {'0'};
	switch (mWebRTCMediaType) {
		case WebRTCMediaType_BothVideoAudio:{
			snprintf(video, sizeof(video) - 1,
					"m=video %u RTP/AVP %u\n"
					"c=IN IP4 %s\n"
					"a=rtpmap:%u %s/%u\n"
					"a=fmtp:%u %s\n",
					mRtpDstVideoPort,
					mVideoSdpPayload.payload_type,
					mRtpDstVideoIp.c_str(),
					mVideoSdpPayload.payload_type,
					mVideoSdpPayload.encoding_name.c_str(),
					mVideoSdpPayload.clock_rate,
					mVideoSdpPayload.payload_type,
					mVideoSdpPayload.fmtp.c_str()
					);
			snprintf(audio, sizeof(audio) - 1,
					"m=audio %u RTP/AVP %u\n"
					"c=IN IP4 %s\n"
					"a=rtpmap:%u %s/%u%s\n"
					"a=fmtp:%u %s\n",
					mRtpDstAudioPort,
					mAudioSdpPayload.payload_type,
					mRtpDstAudioIp.c_str(),
					mAudioSdpPayload.payload_type,
					mAudioSdpPayload.encoding_name.c_str(),
					mAudioSdpPayload.clock_rate,
					(mAudioSdpPayload.encoding_params.length() > 0)?("/" + mAudioSdpPayload.encoding_params).c_str():"",
					mAudioSdpPayload.payload_type,
					mAudioSdpPayload.fmtp.c_str()
					);
			snprintf(sdp, sizeof(sdp) - 1,
					"%s%s%s",
					header,
					video,
					audio
					);
		}break;
		case WebRTCMediaType_OnlyVideo:{
			snprintf(video, sizeof(video) - 1,
					"m=video %u RTP/AVP %u\n"
					"c=IN IP4 %s\n"
					"a=rtpmap:%u %s/%u\n"
					"a=fmtp:%u %s\n",
					mRtpDstVideoPort,
					mVideoSdpPayload.payload_type,
					mRtpDstVideoIp.c_str(),
					mVideoSdpPayload.payload_type,
					mVideoSdpPayload.encoding_name.c_str(),
					mVideoSdpPayload.clock_rate,
					mVideoSdpPayload.payload_type,
					mVideoSdpPayload.fmtp.c_str()
					);
			snprintf(sdp, sizeof(sdp) - 1,
					"%s%s",
					header,
					video
					);
		}break;
		case WebRTCMediaType_OnlyAudio:{
			snprintf(audio, sizeof(audio) - 1,
					"m=audio %u RTP/AVP %u\n"
					"c=IN IP4 %s\n"
					"a=rtpmap:%u %s/%u%s\n"
					"a=fmtp:%u %s\n",
					mRtpDstAudioPort,
					mAudioSdpPayload.payload_type,
					mRtpDstAudioIp.c_str(),
					mAudioSdpPayload.payload_type,
					mAudioSdpPayload.encoding_name.c_str(),
					mAudioSdpPayload.clock_rate,
					(mAudioSdpPayload.encoding_params.length() > 0)?("/" + mAudioSdpPayload.encoding_params).c_str():"",
					mAudioSdpPayload.payload_type,
					mAudioSdpPayload.fmtp.c_str()
					);
			snprintf(sdp, sizeof(sdp) - 1,
					"%s%s",
					header,
					audio
					);
		}break;
		default:break;
	};

//	snprintf(sdp, sizeof(sdp) - 1,
//			"SDP:"
//			"v=0\n"
//			"o=- 0 0 IN IP4 127.0.0.1\n"
//			"s=No Name\n"
//			"t=0 0\n"
//			"m=video %u RTP/AVP %u\n"
//			"c=IN IP4 %s\n"
//			"a=rtpmap:%u %s/%u\n"
//			"a=fmtp:%u %s\n"
//			"m=audio %u RTP/AVP %u\n"
//			"c=IN IP4 %s\n"
//			"a=rtpmap:%u %s/%u%s\n"
//			"a=fmtp:%u %s\n",
//			mRtpDstVideoPort,
//			mVideoSdpPayload.payload_type,
//			mRtpDstVideoIp.c_str(),
//			mVideoSdpPayload.payload_type,
//			mVideoSdpPayload.encoding_name.c_str(),
//			mVideoSdpPayload.clock_rate,
//			mVideoSdpPayload.payload_type,
//			mVideoSdpPayload.fmtp.c_str(),
//			mRtpDstAudioPort,
//			mAudioSdpPayload.payload_type,
//			mRtpDstAudioIp.c_str(),
//			mAudioSdpPayload.payload_type,
//			mAudioSdpPayload.encoding_name.c_str(),
//			mAudioSdpPayload.clock_rate,
//			(mAudioSdpPayload.encoding_params.length() > 0)?("/" + mAudioSdpPayload.encoding_params).c_str():"",
//			mAudioSdpPayload.payload_type,
//			mAudioSdpPayload.fmtp.c_str()
//			);

	LogAync(
			LOG_MSG,
			"WebRTC::CreateLocalSdp( "
			"this : %p, "
			"sdp :\n%s"
			")",
			this,
			sdp
			);

	return string(sdp);
}

bool WebRTC::CreateLocalSdpFile() {
	bool bFlag = false;
	mpSdpFile = fopen(mSdpFilePath.c_str(), "w+b");
	if ( mpSdpFile ) {
		string sdp = CreateLocalSdp();
		size_t size = fwrite(sdp.c_str(), sizeof(char), sdp.length(), mpSdpFile);

		if ( size == sdp.length() ) {
			bFlag = true;
			fflush(mpSdpFile);
		}

        fclose(mpSdpFile);
        mpSdpFile = NULL;
	}

	if ( !bFlag ) {
		LogAync(
				LOG_WARNING,
				"WebRTC::CreateLocalSdp( "
				"this : %p, "
				"[Fail], "
				"mSdpFilePath : %s "
				")",
				this,
				mSdpFilePath.c_str()
				);
	}

	return bFlag;
}

void WebRTC::RemoveLocalSdpFile() {
    if (mpSdpFile) {
        fclose(mpSdpFile);
        mpSdpFile = NULL;
    }
    remove(mSdpFilePath.c_str());
}

bool WebRTC::StartRtpTransform() {
	bool bFlag = CreateLocalSdpFile();

	if ( bFlag ) {
		pid_t pid = fork();
		if ( pid < 0 ) {
			LogAync(
					LOG_ERR_SYS,
					"WebRTC::StartRtpTransform( "
					"this : %p, "
					"[Can't Fork New Process Error] "
					")",
					this
					);
			bFlag = false;
		} else if ( pid > 0 ) {
			LogAync(
					LOG_MSG,
					"WebRTC::StartRtpTransform( "
					"this : %p, "
					"[Fork New Process OK], "
					"pid : %u "
					")",
					this,
					pid
					);
			mRtpTransformPid = pid;
			MainLoop::GetMainLoop()->StartWatchChild(mRtpTransformPid, this);
		} else {
			char transcode[2] = {'\0'};
			sprintf(transcode, "%d", mNeedTranscodeVideo);
			int ret = execle("/bin/sh", "sh", mRtp2RtmpShellFilePath.c_str(), mSdpFilePath.c_str(), mRtmpUrl.c_str(), transcode, NULL, NULL);
			exit(EXIT_SUCCESS);
		}
	}

	return bFlag;
}

void WebRTC::StopRtpTransform() {
	LogAync(
			LOG_MSG,
			"WebRTC::StopRtpTransform( "
			"this : %p, "
			"pid : %d "
			")",
			this,
			mRtpTransformPid
			);

	// 不需要锁, 内部有锁, 找不到就放过
	MainLoop::GetMainLoop()->StopWatchChild(mRtpTransformPid);

	mRtpTransformPidMutex.lock();
	if ( mRtpTransformPid != 0 ) {
		LogAync(
				LOG_MSG,
				"WebRTC::StopRtpTransform( "
				"this : %p, "
				"pid : %d "
				")",
				this,
				mRtpTransformPid
				);
		kill(mRtpTransformPid, SIGTERM);
		mRtpTransformPid = 0;
	}
	mRtpTransformPidMutex.unlock();

	RemoveLocalSdpFile();

	LogAync(
			LOG_MSG,
			"WebRTC::StopRtpTransform( "
			"this : %p, "
			"[OK], "
			"pid : %d "
			")",
			this,
			mRtpTransformPid
			);
}

string WebRTC::CreateVideoAudioSdp(const string& candidate, const string& ip, unsigned int port, vector<string> candList, const string& ufrag, const string& pwd) {
	string result;

	string audioRtcpFb = "";
	while( !mAudioRtcpFbList.empty() ) {
		string rtcpFb = mAudioRtcpFbList.front();
		mAudioRtcpFbList.pop_front();

		if( rtcpFb.length() > 0 ) {
			audioRtcpFb += "a=";
			audioRtcpFb += rtcpFb;
			audioRtcpFb += "\n";
		}
	}

	string videoRtcpFb = "";
	while( !mVideoRtcpFbList.empty() ) {
		string rtcpFb = mVideoRtcpFbList.front();
		mVideoRtcpFbList.pop_front();

		if( rtcpFb.length() > 0 ) {
			videoRtcpFb += "a=";
			videoRtcpFb += rtcpFb;
			videoRtcpFb += "\n";
		}
	}

	char sdp[4096] = {'0'};
	snprintf(sdp, sizeof(sdp) - 1,
			"v=0\n"
			"o=MediaServer 8792925737725123967 2 IN IP4 127.0.0.1\n"
			"s=MediaServer\n"
			"t=0 0\n"
			"a=group:BUNDLE %s %s\n"
			"a=msid-semantic: WMS\n"
			"m=audio %u UDP/TLS/RTP/SAVPF %u\n"
			"c=IN IP4 %s\n"
			"a=rtcp:9 IN IP4 0.0.0.0\n"
			"%s"
			"a=ice-ufrag:%s\n"
			"a=ice-pwd:%s\n"
			"a=ice-options:trickle\n"
			"a=fingerprint:sha-256 %s\n"
			"a=setup:active\n"
			"a=mid:%s\n"
			"a=recvonly\n"
			"a=rtcp-mux\n"
			"a=rtpmap:%u %s/%u%s\n"
//			"a=rtcp-fb:%u transport-cc\n"
			"%s"
			"a=fmtp:%u minptime=10;useinbandfec=1\n"
			"m=video 9 UDP/TLS/RTP/SAVPF %u\n"
			"c=IN IP4 0.0.0.0\n"
			"a=rtcp:9 IN IP4 0.0.0.0\n"
			"a=ice-ufrag:%s\n"
			"a=ice-pwd:%s\n"
			"a=ice-options:trickle\n"
			"a=fingerprint:sha-256 %s\n"
			"a=setup:active\n"
			"a=mid:%s\n"
//			"b=AS:800\n"
			"a=recvonly\n"
			"a=rtcp-mux\n"
			"a=rtcp-rsize\n"
			"a=rtpmap:%u %s/%u\n"
			"%s",
			mAudioMid.c_str(),
			mVideoMid.c_str(),
			port,
			mAudioSdpPayload.payload_type,
			ip.c_str(),
			candidate.c_str(),
			ufrag.c_str(),
			pwd.c_str(),
			DtlsSession::GetFingerprint(),
			mAudioMid.c_str(),
			mAudioSdpPayload.payload_type,
			mAudioSdpPayload.encoding_name.c_str(),
			mAudioSdpPayload.clock_rate,
			(mAudioSdpPayload.encoding_params.length() > 0)?("/" + mAudioSdpPayload.encoding_params).c_str():"",
//			mAudioSdpPayload.payload_type,
			audioRtcpFb.c_str(),
			mAudioSdpPayload.payload_type,
			mVideoSdpPayload.payload_type,
			ufrag.c_str(),
			pwd.c_str(),
			DtlsSession::GetFingerprint(),
			mVideoMid.c_str(),
			mVideoSdpPayload.payload_type,
			mVideoSdpPayload.encoding_name.c_str(),
			mVideoSdpPayload.clock_rate,
			videoRtcpFb.c_str()
			);

	result = sdp;
	char fmtp[256] = {'0'};
	snprintf(fmtp, sizeof(fmtp) - 1,
			"a=fmtp:%u %s\n",
			mVideoSdpPayload.payload_type,
			mVideoSdpPayload.fmtp.c_str()
			);
	result += fmtp;

	return result;
}

string WebRTC::CreateVideoOnlySdp(const string& candidate, const string& ip, unsigned int port, vector<string> candList, const string& ufrag, const string& pwd) {
	string result;

	string videoRtcpFb = "";
	while( !mVideoRtcpFbList.empty() ) {
		string rtcpFb = mVideoRtcpFbList.front();
		mVideoRtcpFbList.pop_front();

		if( rtcpFb.length() > 0 ) {
			videoRtcpFb += "a=";
			videoRtcpFb += rtcpFb;
			videoRtcpFb += "\n";
		}
	}

	char sdp[4096] = {'0'};
	snprintf(sdp, sizeof(sdp) - 1,
			"v=0\n"
			"o=MediaServer 8792925737725123967 2 IN IP4 127.0.0.1\n"
			"s=MediaServer\n"
			"t=0 0\n"
			"a=group:BUNDLE %s\n"
			"a=msid-semantic: WMS\n"
			"m=video %u UDP/TLS/RTP/SAVPF %u\n"
			"c=IN IP4 %s\n"
			"a=rtcp:9 IN IP4 0.0.0.0\n"
			"%s"
			"a=ice-ufrag:%s\n"
			"a=ice-pwd:%s\n"
			"a=ice-options:trickle\n"
			"a=fingerprint:sha-256 %s\n"
			"a=setup:active\n"
			"a=mid:%s\n"
			"a=recvonly\n"
			"a=rtcp-mux\n"
			"a=rtcp-rsize\n"
			"a=rtpmap:%u %s/%u\n"
			"%s",
			mVideoMid.c_str(),
			port,
			mVideoSdpPayload.payload_type,
			ip.c_str(),
			candidate.c_str(),
			ufrag.c_str(),
			pwd.c_str(),
			DtlsSession::GetFingerprint(),
			mVideoMid.c_str(),
			mVideoSdpPayload.payload_type,
			mVideoSdpPayload.encoding_name.c_str(),
			mVideoSdpPayload.clock_rate,
			videoRtcpFb.c_str()
			);

	result = sdp;
	char fmtp[256] = {'0'};
	snprintf(fmtp, sizeof(fmtp) - 1,
			"a=fmtp:%u %s\n",
			mVideoSdpPayload.payload_type,
			mVideoSdpPayload.fmtp.c_str()
			);
	result += fmtp;

	return result;
}

string WebRTC::CreateAudioOnlySdp(const string& candidate, const string& ip, unsigned int port, vector<string> candList, const string& ufrag, const string& pwd) {
	string result;
	return result;
}

int WebRTC::SendData(const void *data, unsigned int len) {
	// Send RTP data through ICE data channel
	return mIceClient.SendData(data, len);
}

void WebRTC::OnIceCandidateGatheringDone(IceClient *ice, const string& ip, unsigned int port, vector<string> candList, const string& ufrag, const string& pwd) {
	string candidate;
	for(int i = 0; i < (int)candList.size(); i++) {
		candidate += candList[i];
	}

	LogAync(
			LOG_WARNING,
			"WebRTC::OnIceCandidateGatheringDone( "
			"this : %p, "
			"ice : %p, "
			"ip : %s, "
			"port : %u, "
			"rtmpUrl : %s, "
			"candidate :\n%s"
			")",
			this,
			ice,
			ip.c_str(),
			port,
			mRtmpUrl.c_str(),
			candidate.c_str()
			);

//	string audioRtcpFb = "";
//	while( !mAudioRtcpFbList.empty() ) {
//		string rtcpFb = mAudioRtcpFbList.front();
//		mAudioRtcpFbList.pop_front();
//
//		if( rtcpFb.length() > 0 ) {
//			audioRtcpFb += "a=";
//			audioRtcpFb += rtcpFb;
//			audioRtcpFb += "\n";
//		}
//	}
//
//	string videoRtcpFb = "";
//	while( !mVideoRtcpFbList.empty() ) {
//		string rtcpFb = mVideoRtcpFbList.front();
//		mVideoRtcpFbList.pop_front();
//
//		if( rtcpFb.length() > 0 ) {
//			videoRtcpFb += "a=";
//			videoRtcpFb += rtcpFb;
//			videoRtcpFb += "\n";
//		}
//	}
//
//	char sdp[4096] = {'0'};
//	snprintf(sdp, sizeof(sdp) - 1,
//			"v=0\n"
//			"o=MediaServer 8792925737725123967 2 IN IP4 127.0.0.1\n"
//			"s=MediaServer\n"
//			"t=0 0\n"
//			"a=group:BUNDLE %s %s\n"
//			"a=msid-semantic: WMS\n"
//			"m=audio %u UDP/TLS/RTP/SAVPF %u\n"
//			"c=IN IP4 %s\n"
//			"a=rtcp:9 IN IP4 0.0.0.0\n"
//			"%s"
//			"a=ice-ufrag:%s\n"
//			"a=ice-pwd:%s\n"
//			"a=ice-options:trickle\n"
//			"a=fingerprint:sha-256 %s\n"
//			"a=setup:active\n"
//			"a=mid:%s\n"
//			"a=recvonly\n"
//			"a=rtcp-mux\n"
//			"a=rtpmap:%u %s/%u%s\n"
////			"a=rtcp-fb:%u transport-cc\n"
//			"%s"
//			"a=fmtp:%u minptime=10;useinbandfec=1\n"
//			"m=video 9 UDP/TLS/RTP/SAVPF %u\n"
//			"c=IN IP4 0.0.0.0\n"
//			"a=rtcp:9 IN IP4 0.0.0.0\n"
//			"a=ice-ufrag:%s\n"
//			"a=ice-pwd:%s\n"
//			"a=ice-options:trickle\n"
//			"a=fingerprint:sha-256 %s\n"
//			"a=setup:active\n"
//			"a=mid:%s\n"
////			"b=AS:800\n"
//			"a=recvonly\n"
//			"a=rtcp-mux\n"
//			"a=rtcp-rsize\n"
//			"a=rtpmap:%u %s/%u\n"
//			"%s",
//			mAudioMid.c_str(),
//			mVideoMid.c_str(),
//			port,
//			mAudioSdpPayload.payload_type,
//			ip.c_str(),
//			candidate.c_str(),
//			ufrag.c_str(),
//			pwd.c_str(),
//			DtlsSession::GetFingerprint(),
//			mAudioMid.c_str(),
//			mAudioSdpPayload.payload_type,
//			mAudioSdpPayload.encoding_name.c_str(),
//			mAudioSdpPayload.clock_rate,
//			(mAudioSdpPayload.encoding_params.length() > 0)?("/" + mAudioSdpPayload.encoding_params).c_str():"",
////			mAudioSdpPayload.payload_type,
//			audioRtcpFb.c_str(),
//			mAudioSdpPayload.payload_type,
//			mVideoSdpPayload.payload_type,
//			ufrag.c_str(),
//			pwd.c_str(),
//			DtlsSession::GetFingerprint(),
//			mVideoMid.c_str(),
//			mVideoSdpPayload.payload_type,
//			mVideoSdpPayload.encoding_name.c_str(),
//			mVideoSdpPayload.clock_rate,
//			videoRtcpFb.c_str()
//			);
//
//	string sdpStr = sdp;
//	char fmtp[256] = {'0'};
//	snprintf(fmtp, sizeof(fmtp) - 1,
//			"a=fmtp:%u %s\n",
//			mVideoSdpPayload.payload_type,
//			mVideoSdpPayload.fmtp.c_str()
//			);
//	sdpStr += fmtp;

	string sdpStr =  "";
	switch (mWebRTCMediaType) {
		case WebRTCMediaType_BothVideoAudio:{
			sdpStr = CreateVideoAudioSdp(candidate, ip, port, candList, ufrag, pwd);
		}break;
		case WebRTCMediaType_OnlyVideo:{
			sdpStr = CreateVideoOnlySdp(candidate, ip, port, candList, ufrag, pwd);
		}break;
		case WebRTCMediaType_OnlyAudio:{
			sdpStr = CreateAudioOnlySdp(candidate, ip, port, candList, ufrag, pwd);
		}break;
		default:break;
	};

	if( mpWebRTCCallback ) {
		mpWebRTCCallback->OnWebRTCServerSdp(this, sdpStr, mWebRTCMediaType);
	}

}

void WebRTC::OnIceNewSelectedPairFull(IceClient *ice) {
	LogAync(
			LOG_WARNING,
			"WebRTC::OnIceNewSelectedPairFull( "
			"this : %p, "
			"ice : %p, "
			"local : %s, "
			"remote : %s, "
			"rtmpUrl : %s "
			")",
			this,
			ice,
			ice->GetLocalAddress().c_str(),
			ice->GetRemoteAddress().c_str(),
			mRtmpUrl.c_str()
			);
}

void WebRTC::OnIceConnected(IceClient *ice) {
	LogAync(
			LOG_WARNING,
			"WebRTC::OnIceConnected( "
			"this : %p, "
			"ice : %p, "
			"rtmpUrl : %s "
			")",
			this,
			ice,
			mRtmpUrl.c_str()
			);
	mDtlsSession.Handshake();
}

void WebRTC::OnIceRecvData(IceClient *ice, const char *data, unsigned int size, unsigned int streamId, unsigned int componentId) {
//	LogAync(
//			LOG_STAT,
//			"WebRTC::OnIceRecvData( "
//			"this : %p, "
//			"ice : %p, "
//			"streamId : %u, "
//			"componentId : %u, "
//			"size : %d, "
//			"data[0] : 0x%X "
//			")",
//			this,
//			ice,
//			streamId,
//			componentId,
//			size,
//			(unsigned char)data[0]
//			);

	bool bFlag = false;

	char pkt[RTP_MAX_LEN] = {0};
	unsigned int pktSize = size;

	if ( size > RTP_MAX_LEN ) {
		LogAync(
				LOG_WARNING,
				"WebRTC::OnIceRecvData( "
				"this : %p, "
				"[Unknow Data Format], "
				"ice : %p, "
				"streamId : %u, "
				"componentId : %u, "
				"size : %d, "
				"data[0] : 0x%X "
				")",
				this,
				ice,
				streamId,
				componentId,
				size,
				(unsigned char)data[0]
				);
		return;
	}

	if( DtlsSession::IsDTLS(data, size) ) {
		bFlag = mDtlsSession.RecvFrame(data, size);
		if( bFlag ) {
			// Check Handshake status
			DtlsSessionStatus status = mDtlsSession.GetDtlsSessionStatus();
			if( status == DtlsSessionStatus_HandshakeDone ) {
				LogAync(
						LOG_WARNING,
						"WebRTC::OnIceRecvData( "
						"this : %p, "
						"[DTLS Handshake OK], "
						"rtmpUrl : %s "
						")",
						this,
						mRtmpUrl.c_str()
						);

				bool bStart = false;
				if( StartRtpTransform() ) {
					char localKey[SRTP_MASTER_LENGTH];
					int localSize = 0;
					mDtlsSession.GetClientKey(localKey, localSize);
					char remoteKey[SRTP_MASTER_LENGTH];
					int remoteSize = 0;
					mDtlsSession.GetServerKey(remoteKey, remoteSize);

					bStart = mRtpSession.Start(localKey, localSize, remoteKey, remoteSize);
				}

				if ( bStart ) {
					if( mpWebRTCCallback ) {
						mpWebRTCCallback->OnWebRTCStartMedia(this);
					}
				} else {
					if( mpWebRTCCallback ) {
						mpWebRTCCallback->OnWebRTCError(this, WebRTCErrorType_Rtp2Rtmp_Start_Fail, WebRTCErrorMsg[WebRTCErrorType_Rtp2Rtmp_Start_Fail]);
					}
				}

			} else if ( status == DtlsSessionStatus_Alert ) {
				LogAync(
						LOG_WARNING,
						"WebRTC::OnIceRecvData( "
						"this : %p, "
						"[DTLS Alert], "
						"rtmpUrl : %s "
						")",
						this,
						mRtmpUrl.c_str()
						);
//				mRtpSession.Stop();
//				mRtpDstAudioClient.Stop();
//				mRtpDstVideoClient.Stop();

				if( mpWebRTCCallback ) {
					mpWebRTCCallback->OnWebRTCClose(this);
				}
			}
		}
	} else if( RtpSession::IsRtp(data, size) ) {
		bFlag = mRtpSession.RecvRtpPacket(data, size, pkt, pktSize);
		if( bFlag ) {
			unsigned int ssrc = RtpSession::GetRtpSSRC(pkt, pktSize);
//			LogAync(
//					LOG_STAT,
//					"WebRTC::OnIceRecvData( "
//					"this : %p, "
//					"[Relay RTP], "
//					"ssrc : %u, "
//					"mAudioSSRC : %u, "
//					"mVideoSSRC : %u "
//					")",
//					this,
//					ssrc,
//					mAudioSSRC,
//					mVideoSSRC
//					);

			if( ssrc == mAudioSSRC ) {
				mRtpDstAudioClient.SendRtpPacket(pkt, pktSize);
			} else if ( ssrc == mVideoSSRC ) {
				mRtpDstVideoClient.SendRtpPacket(pkt, pktSize);
			}
		}
	} else if( RtpSession::IsRtcp(data, size) ){
		bFlag = mRtpSession.RecvRtcpPacket(data, size, pkt, pktSize);
		if( bFlag ) {
			unsigned int ssrc = RtpSession::GetRtcpSSRC(pkt, pktSize);
//			LogAync(
//					LOG_STAT,
//					"WebRTC::OnIceRecvData( "
//					"this : %p, "
//					"[Relay RTCP], "
//					"ssrc : %u, "
//					"mAudioSSRC : %u, "
//					"mVideoSSRC : %u "
//					")",
//					this,
//					ssrc,
//					mAudioSSRC,
//					mVideoSSRC
//					);

			if( ssrc == mAudioSSRC ) {
				mRtpDstAudioClient.SendRtcpPacket(pkt, pktSize);
			} else if ( ssrc == mVideoSSRC ) {
				mRtpDstVideoClient.SendRtcpPacket(pkt, pktSize);
			}
		}
	} else {
		LogAync(
				LOG_WARNING,
				"WebRTC::OnIceRecvData( "
				"this : %p, "
				"[Unknow Data Format], "
				"ice : %p, "
				"streamId : %u, "
				"componentId : %u, "
				"size : %d, "
				"data[0] : 0x%X "
				")",
				this,
				ice,
				streamId,
				componentId,
				size,
				(unsigned char)data[0]
				);
	}
}

void WebRTC::OnIceClose(IceClient *ice) {
	LogAync(
			LOG_WARNING,
			"WebRTC::OnIceClose( "
			"this : %p, "
			"ice : %p, "
			"local : %s, "
			"remote : %s "
			")",
			this,
			ice,
			ice->GetLocalAddress().c_str(),
			ice->GetRemoteAddress().c_str()
			);

	if( mpWebRTCCallback ) {
		mpWebRTCCallback->OnWebRTCClose(this);
	}
}

void WebRTC::OnChildExit(int pid) {
	LogAync(
			LOG_MSG,
			"WebRTC::OnChildExit( "
			"this : %p, "
			"pid : %d "
			")",
			this,
			pid
			);
	mRtpTransformPidMutex.lock();
	mRtpTransformPid = 0;
	mRtpTransformPidMutex.unlock();

	if( mpWebRTCCallback ) {
		mpWebRTCCallback->OnWebRTCError(this, WebRTCErrorType_Rtp2Rtmp_Exit, WebRTCErrorMsg[WebRTCErrorType_Rtp2Rtmp_Exit]);
	}
}
} /* namespace mediaserver */
