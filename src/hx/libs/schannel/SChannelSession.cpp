#include <hx/schannel/SChannelSession.h>
#include <array>
#include <algorithm>

namespace
{
	static void init_sec_buffer(SecBuffer* buffer, unsigned long type, void* data, unsigned long size)
	{
		buffer->cbBuffer = size;
		buffer->BufferType = type;
		buffer->pvBuffer = data;
	}

	static void init_sec_buffer_desc(SecBufferDesc* desc, SecBuffer* buffers, unsigned long buffer_count)
	{
		desc->ulVersion = SECBUFFER_VERSION;
		desc->pBuffers = buffers;
		desc->cBuffers = buffer_count;
	}
}

hx::schannel::SChannelContext::SChannelContext(const char* inHost)
	: host(inHost)
{
}

hx::schannel::SChannelContext::~SChannelContext()
{
	DeleteSecurityContext(&ctxtHandle);
	FreeCredentialHandle(&credHandle);
}

void hx::schannel::SChannelContext::startHandshake(Dynamic cbSuccess, Dynamic cbFailure)
{
	/*auto parameter = TLS_PARAMETERS{ 0 };
	parameter.grbitDisabledProtocols = SP_PROT_TLS1_3_CLIENT | SP_PROT_TLS1_3_SERVER;*/

	auto credential = SCH_CREDENTIALS{ 0 };
	credential.dwFlags   = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_USE_STRONG_CRYPTO | SCH_CRED_NO_DEFAULT_CREDS;
	credential.dwVersion = SCH_CREDENTIALS_VERSION;
	/*credential.cTlsParameters = 1;
	credential.pTlsParameters = &parameter;*/

	if (SEC_E_OK != AcquireCredentialsHandle(NULL, UNISP_NAME, SECPKG_CRED_OUTBOUND, NULL, &credential, NULL, NULL, &credHandle, &credTimestamp))
	{
		cbFailure(HX_CSTRING("Failed to aquire credentials"));

		return;
	}

	auto outputBuffer            = SecBuffer();
	auto outputBufferDescription = SecBufferDesc();

	init_sec_buffer(&outputBuffer, SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&outputBufferDescription, &outputBuffer, 1);

	requestFlags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM;

	if (SEC_I_CONTINUE_NEEDED != InitializeSecurityContext(&credHandle, nullptr, const_cast<char*>(host), requestFlags, 0, 0, nullptr, 0, &ctxtHandle, &outputBufferDescription, &contextFlags, &ctxtTimestamp))
	{
		cbFailure(HX_CSTRING("Failed to generate initial handshake"));

		return;
	}

	auto buffer = Array<uint8_t>(outputBuffer.cbBuffer, outputBuffer.cbBuffer);

	std::memcpy(buffer->getBase(), outputBuffer.pvBuffer, outputBuffer.cbBuffer);

	FreeContextBuffer(outputBuffer.pvBuffer);

	cbSuccess(buffer);
}

void hx::schannel::SChannelContext::handshake(Array<uint8_t> input, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto outputBuffers           = std::array<SecBuffer, 3>();
	auto outputBufferDescription = SecBufferDesc();
	auto inputBuffers            = std::array<SecBuffer, 2>();
	auto inputBufferDescription  = SecBufferDesc();

	// Input Buffers
	init_sec_buffer(&inputBuffers[0], SECBUFFER_TOKEN, input->getBase(), input->length);
	init_sec_buffer(&inputBuffers[1], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&inputBufferDescription, inputBuffers.data(), inputBuffers.size());

	// Output Buffers
	init_sec_buffer(&outputBuffers[0], SECBUFFER_TOKEN, nullptr, 0);
	init_sec_buffer(&outputBuffers[1], SECBUFFER_ALERT, nullptr, 0);
	init_sec_buffer(&outputBuffers[2], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&outputBufferDescription, outputBuffers.data(), outputBuffers.size());

	auto result = SEC_E_OK;

	switch ((result = InitializeSecurityContext(&credHandle, &ctxtHandle, const_cast<char*>(host), requestFlags, 0, 0, &inputBufferDescription, 0, nullptr, &outputBufferDescription, &contextFlags, &ctxtTimestamp)))
	{
	case SEC_E_OK:
	{
		QueryContextAttributes(&ctxtHandle, SECPKG_ATTR_STREAM_SIZES, &sizes);

		cbSuccess(0, null());

		break;
	}
	case SEC_E_INCOMPLETE_MESSAGE:
	{
		cbSuccess(1, null());

		break;
	}
	case SEC_I_INCOMPLETE_CREDENTIALS:
	{
		cbFailure(HX_CSTRING("Credentials requested"));

		break;
	}
	case SEC_I_CONTINUE_NEEDED:
	{
		if (outputBuffers[0].BufferType != SECBUFFER_TOKEN)
		{
			cbFailure(HX_CSTRING("Expected buffer to be a token type"));

			break;
		}
		if (outputBuffers[0].cbBuffer <= 0)
		{
			cbFailure(HX_CSTRING("Token buffer contains no data"));

			break;
		}

		auto output = Array<uint8_t>(outputBuffers[0].cbBuffer, outputBuffers[0].cbBuffer);

		std::memcpy(output->getBase(), outputBuffers[0].pvBuffer, outputBuffers[0].cbBuffer);

		FreeContextBuffer(outputBuffers[0].pvBuffer);

		cbSuccess(2, output);

		break;
	}
	case SEC_E_WRONG_PRINCIPAL:
	{
		cbFailure(HX_CSTRING("SNI or certificate check failed"));

		break;
	}
	default:
	{
		cbFailure(HX_CSTRING("Creating security context failed"));

		break;
	}
	}
}

void hx::schannel::SChannelContext::encode(Array<uint8_t> input, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto use                = std::min(static_cast<unsigned long>(length), sizes.cbMaximumMessage);
	auto buffer             = std::vector<uint8_t>(TLS_MAX_PACKET_SIZE);
	auto buffers            = std::array<SecBuffer, 4>();
	auto buffersDescription = SecBufferDesc();

	init_sec_buffer(&buffers[0], SECBUFFER_STREAM_HEADER, buffer.data(), sizes.cbHeader);
	init_sec_buffer(&buffers[1], SECBUFFER_DATA, buffer.data() + sizes.cbHeader, use);
	init_sec_buffer(&buffers[2], SECBUFFER_STREAM_TRAILER, buffer.data() + sizes.cbHeader + use, sizes.cbTrailer);
	init_sec_buffer(&buffers[3], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&buffersDescription, buffers.data(), buffers.size());

	std::memcpy(buffers[1].pvBuffer, input->getBase() + offset, use);

	auto result = SEC_E_OK;
	if (SEC_E_OK != (result = EncryptMessage(&ctxtHandle, 0, &buffersDescription, 0)))
	{
		cbFailure(HX_CSTRING("Failed to encrypt message"));

		return;
	}

	auto total  = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
	auto output = Array<uint8_t>(total, total);

	std::memcpy(output->getBase(), buffer.data(), total);

	cbSuccess(output);
}

void hx::schannel::SChannelContext::decode(Array<uint8_t> input, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto buffer             = std::vector<uint8_t>(length);
	auto buffers            = std::array<SecBuffer, 4>();
	auto buffersDescription = SecBufferDesc();

	std::memcpy(buffer.data(), input->getBase() + offset, length);

	init_sec_buffer(&buffers[0], SECBUFFER_DATA, buffer.data(), length);
	init_sec_buffer(&buffers[1], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer(&buffers[2], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer(&buffers[3], SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&buffersDescription, buffers.data(), buffers.size());

	auto result = SEC_E_OK;
	if (SEC_E_OK != (result = DecryptMessage(&ctxtHandle, &buffersDescription, 0, 0)))
	{
		cbFailure(HX_CSTRING("Failed to decrypt message"));

		return;
	}

	auto output = Array<uint8_t>(buffers[1].cbBuffer, buffers[1].cbBuffer);

	std::memcpy(output->GetBase(), buffers[1].pvBuffer, buffers[1].cbBuffer);

	cbSuccess(output);
}

void hx::schannel::SChannelContext::close(Dynamic cbSuccess, Dynamic cbFailure)
{
	auto type                    = SCHANNEL_SHUTDOWN;
	auto inputBuffer             = SecBuffer();
	auto inputBufferDescription  = SecBufferDesc();

	init_sec_buffer(&inputBuffer, SECBUFFER_TOKEN, &type, sizeof(type));
	init_sec_buffer_desc(&inputBufferDescription, &inputBuffer, 1);

	if (SEC_E_OK != ApplyControlToken(&ctxtHandle, &inputBufferDescription))
	{
		cbFailure(HX_CSTRING("Failed to apply control token"));

		return;
	}

	auto outputBuffer            = SecBuffer();
	auto outputBufferDescription = SecBufferDesc();

	init_sec_buffer(&outputBuffer, SECBUFFER_EMPTY, nullptr, 0);
	init_sec_buffer_desc(&outputBufferDescription, &outputBuffer, 1);

	auto result = InitializeSecurityContext(&credHandle, &ctxtHandle, const_cast<char*>(host), requestFlags, 0, 0, nullptr, 0, nullptr, &outputBufferDescription, &contextFlags, &ctxtTimestamp);
	if (result == SEC_E_OK || result == SEC_I_CONTEXT_EXPIRED)
	{
		auto output = Array<uint8_t>(outputBuffer.cbBuffer, outputBuffer.cbBuffer);

		std::memcpy(output->getBase(), outputBuffer.pvBuffer, outputBuffer.cbBuffer);

		FreeContextBuffer(outputBuffer.pvBuffer);

		cbSuccess(output);
	}
	else
	{
		cbFailure(HX_CSTRING("Unexpected InitializeSecurityContext result"));
	}
}

cpp::Pointer<hx::schannel::SChannelContext> hx::schannel::SChannelContext::create(::String host)
{
	return cpp::Pointer<SChannelContext>(new SChannelContext(host.utf8_str()));
}