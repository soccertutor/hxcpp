#include "LibuvTcpSocket.h"
#include "NetUtils.h"
#include "../LibuvUtils.h"

namespace
{
	struct ConnectionRequest final : hx::asys::libuv::BaseRequest
	{
		std::unique_ptr<hx::asys::libuv::net::LibuvTcpSocketImpl> socket;
		hx::RootedObject<hx::Object> options;

		uv_connect_t connect;

		ConnectionRequest(Dynamic _cbSuccess, Dynamic _cbFailure)
			: hx::asys::libuv::BaseRequest(_cbSuccess, _cbFailure)
			, socket(std::make_unique<hx::asys::libuv::net::LibuvTcpSocketImpl>())
			, options(nullptr)
		{
			connect.data = this;
		}
	};

	class WriteRequest final : hx::asys::libuv::BaseRequest
	{
		std::unique_ptr<hx::ArrayPin> pin;

	public:
		uv_write_t request;
		uv_buf_t buffer;

		WriteRequest(hx::ArrayPin* _pin, char* _base, int _length, Dynamic _cbSuccess, Dynamic _cbFailure)
			: BaseRequest(_cbSuccess, _cbFailure)
			, pin(_pin)
			, buffer(uv_buf_init(_base, _length))
		{
			request.data = this;
		}

		static void callback(uv_write_t* request, int status)
		{
			auto gcZone = hx::AutoGCZone();
			auto spData = std::unique_ptr<hx::asys::libuv::BaseRequest>(static_cast<hx::asys::libuv::BaseRequest*>(request->data));

			if (status < 0)
			{
				Dynamic(spData->cbFailure.rooted)(hx::asys::libuv::uv_err_to_enum(status));
			}
			else
			{
				Dynamic(spData->cbSuccess.rooted)();
			}
		}
	};

	struct ShutdownRequest final : hx::asys::libuv::BaseRequest
	{
		uv_shutdown_t request;

		ShutdownRequest(Dynamic cbSuccess, Dynamic cbFailure) : hx::asys::libuv::BaseRequest(cbSuccess, cbFailure)
		{
			request.data = this;
		}

		static void clean_stream(uv_handle_t* handle)
		{
			delete static_cast<hx::asys::libuv::stream::StreamReader*>(handle->data);
		}

		static void callback(uv_shutdown_t* request, int status)
		{
			auto gcZone = hx::AutoGCZone();
			auto spData = std::unique_ptr<hx::asys::libuv::BaseRequest>(static_cast<hx::asys::libuv::BaseRequest*>(request->data));

			if (status < 0)
			{
				Dynamic(spData->cbFailure.rooted)(hx::asys::libuv::uv_err_to_enum(status));
			}
			else
			{
				uv_close(reinterpret_cast<uv_handle_t*>(request->handle), clean_stream);

				Dynamic(spData->cbSuccess.rooted)();
			}
		}
	};

	static void on_connection(uv_connect_t* request, const int status)
	{
		auto gcZone = hx::AutoGCZone();
		auto spData = std::unique_ptr<ConnectionRequest>(static_cast<ConnectionRequest*>(request->data));

		if (status < 0)
		{
			Dynamic(spData->cbFailure.rooted)(hx::asys::libuv::uv_err_to_enum(status));

			return;
		}

		Dynamic(spData->cbSuccess.rooted)(new hx::asys::libuv::net::LibuvTcpSocket(spData->socket.release()));
	}

	void on_connect(hx::asys::libuv::LibuvAsysContext ctx, sockaddr* const address, Dynamic options, Dynamic cbSuccess, Dynamic cbFailure)
	{
		auto request = std::make_unique<ConnectionRequest>(cbSuccess, cbFailure);
		auto result  = 0;

		if ((result = uv_tcp_init(ctx->uvLoop, &request->socket->tcp)) < 0)
		{
			cbFailure(hx::asys::libuv::uv_err_to_enum(result));

			return;
		}

		if ((result = uv_tcp_connect(&request->connect, &request->socket->tcp, address, on_connection)) < 0)
		{
			cbFailure(hx::asys::libuv::uv_err_to_enum(result));
		}
		else
		{
			request.release();
		}
	}
}

hx::asys::libuv::net::LibuvTcpSocket::LibuvTcpSocket(LibuvTcpSocketImpl* _socket) : socket(_socket)
{
	HX_OBJ_WB_NEW_MARKED_OBJECT(this);

	localAddress  = hx::asys::libuv::net::getLocalAddress(&socket->tcp);
	remoteAddress = hx::asys::libuv::net::getRemoteAddress(&socket->tcp);
}

void hx::asys::libuv::net::LibuvTcpSocket::getKeepAlive(Dynamic cbSuccess, Dynamic cbFailure)
{
	cbSuccess(socket->keepAlive > 0);
}

void hx::asys::libuv::net::LibuvTcpSocket::getSendBufferSize(Dynamic cbSuccess, Dynamic cbFailure)
{
	auto size   = 0;
	auto result = uv_send_buffer_size(reinterpret_cast<uv_handle_t*>(&socket->tcp, &size), &size);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		cbSuccess(size);
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::getRecvBufferSize(Dynamic cbSuccess, Dynamic cbFailure)
{
	auto size   = 0;
	auto result = uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(&socket->tcp), &size);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		cbSuccess(size);
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::setKeepAlive(bool keepAlive, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto result = uv_tcp_keepalive(&socket->tcp, keepAlive, socket->keepAlive);
	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		socket->keepAlive = keepAlive ? KEEP_ALIVE_VALUE : 0;

		cbSuccess();
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::setSendBufferSize(int size, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto result = uv_send_buffer_size(reinterpret_cast<uv_handle_t*>(&socket->tcp), &size);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		cbSuccess(size);
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::setRecvBufferSize(int size, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto result = uv_recv_buffer_size(reinterpret_cast<uv_handle_t*>(&socket->tcp), &size);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		cbSuccess(size);
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::read(Array<uint8_t> output, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure)
{
	socket->read(output, offset, length, cbSuccess, cbFailure);
}

void hx::asys::libuv::net::LibuvTcpSocket::write(Array<uint8_t> data, int offset, int length, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto request = std::make_unique<WriteRequest>(data->Pin(), data->GetBase() + offset, length, cbSuccess, cbFailure);
	auto result  = uv_write(&request->request, reinterpret_cast<uv_stream_t*>(&socket->tcp), &request->buffer, 1, WriteRequest::callback);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		request.release();
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::flush(Dynamic cbSuccess, Dynamic cbFailure)
{
	cbSuccess();
}

void hx::asys::libuv::net::LibuvTcpSocket::close(Dynamic cbSuccess, Dynamic cbFailure)
{
	auto result  = 0;
	auto request = std::make_unique<ShutdownRequest>(cbSuccess, cbFailure);

	if ((result = uv_shutdown(&request->request, reinterpret_cast<uv_stream_t*>(&socket->tcp), ShutdownRequest::callback)) < 0)
	{
		uv_close(reinterpret_cast<uv_handle_t*>(&socket->tcp), ShutdownRequest::clean_stream);

		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		request.release();
	}
}

void hx::asys::libuv::net::LibuvTcpSocket::__Mark(hx::MarkContext* __inCtx)
{
	HX_MARK_MEMBER(localAddress);
	HX_MARK_MEMBER(remoteAddress);
}

#ifdef HXCPP_VISIT_ALLOCS
void hx::asys::libuv::net::LibuvTcpSocket::__Visit(hx::VisitContext* __inCtx)
{
	HX_VISIT_MEMBER(localAddress);
	HX_VISIT_MEMBER(remoteAddress);
}
#endif

void hx::asys::net::TcpSocket_obj::connect_ipv4(Context ctx, const String host, int port, Dynamic options, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto address = sockaddr_in();
	auto result  = uv_ip4_addr(host.utf8_str(), port, &address);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		on_connect(hx::asys::libuv::context(ctx), reinterpret_cast<sockaddr*>(&address), options, cbSuccess, cbFailure);
	}
}

void hx::asys::net::TcpSocket_obj::connect_ipv6(Context ctx, const String host, int port, Dynamic options, Dynamic cbSuccess, Dynamic cbFailure)
{
	auto address = sockaddr_in6();
	auto result = uv_ip6_addr(host.utf8_str(), port, &address);

	if (result < 0)
	{
		cbFailure(hx::asys::libuv::uv_err_to_enum(result));
	}
	else
	{
		on_connect(hx::asys::libuv::context(ctx), reinterpret_cast<sockaddr*>(&address), options, cbSuccess, cbFailure);
	}
}