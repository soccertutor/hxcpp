#pragma once

#include <hxcpp.h>

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <SubAuth.h>

#define SCHANNEL_USE_BLACKLISTS
#define SECURITY_WIN32

#include <schannel.h>
#include <security.h>
#include <shlwapi.h>

namespace hx::schannel
{
	class SChannelContext final
	{
		const int TLS_MAX_PACKET_SIZE = (16384 + 512);

		SChannelContext(const char* inHost);
	public:
		~SChannelContext();

		const char* host;

		CredHandle credHandle;
		TimeStamp credTimestamp;
		CtxtHandle ctxtHandle;
		TimeStamp ctxtTimestamp;

		DWORD requestFlags;
		DWORD contextFlags;

		SecPkgContext_StreamSizes sizes;

		void startHandshake(Dynamic cbSuccess, Dynamic cbFailure);
		void handshake(Array<uint8_t> input, Dynamic cbSuccess, Dynamic cbFailure);

		void encode(Array<uint8_t> input, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure);
		void decode(Array<uint8_t> input, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure);
		void close(Dynamic cbSuccess, Dynamic cbFailure);

		static cpp::Pointer<SChannelContext> create(::String host);
	};
}