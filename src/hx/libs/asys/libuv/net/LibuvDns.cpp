#include <hxcpp.h>
#include <array>
#include <memory>
#include "../LibuvUtils.h"
#include "NetUtils.h"

namespace
{
    class AddrInfoCleaner
    {
    public:
        addrinfo* info;

        AddrInfoCleaner(addrinfo* _info) : info(_info) {}
        ~AddrInfoCleaner()
        {
            uv_freeaddrinfo(info);
        }
    };

    void hostname_callback(uv_getnameinfo_t* request, int status, const char* hostname, const char* service)
    {
        auto gcZone    = hx::AutoGCZone();
        auto spRequest = std::unique_ptr<uv_getnameinfo_t>(request);
        auto spData    = std::unique_ptr<hx::asys::libuv::BaseRequest>(static_cast<hx::asys::libuv::BaseRequest*>(request->data));

        if (status < 0)
        {
            Dynamic(spData->cbFailure.rooted)(hx::asys::libuv::uv_err_to_enum(status));
        }
        else
        {
            Dynamic(spData->cbSuccess.rooted)(String::create(hostname));
        }
    }
}

void hx::asys::net::dns::resolve(Context ctx, String host, Dynamic cbSuccess, Dynamic cbFailure)
{
    auto libuvCtx = hx::asys::libuv::context(ctx);
    auto data     = std::make_unique<hx::asys::libuv::BaseRequest>(cbSuccess, cbFailure);
    auto request  = std::make_unique<uv_getaddrinfo_t>();
    auto wrapper  = [](uv_getaddrinfo_t* request, int status, addrinfo* addr) {
        auto gcZone      = hx::AutoGCZone();
        auto spRequest   = std::unique_ptr<uv_getaddrinfo_t>(request);
        auto spData      = std::unique_ptr<hx::asys::libuv::BaseRequest>(static_cast<hx::asys::libuv::BaseRequest*>(request->data));
        auto addrCleaner = AddrInfoCleaner(addr);

        if (status < 0)
        {
            Dynamic(spData->cbFailure.rooted)(hx::asys::libuv::uv_err_to_enum(status));
        }
        else
        {
            auto ips  = new Array_obj<hx::EnumBase>(0, 0);
            auto info = addr;

            do
            {
                switch (info->ai_addr->sa_family)
                {
                    case AF_INET: {
                        ips->Add(hx::asys::libuv::net::ip_from_sockaddr(reinterpret_cast<sockaddr_in*>(info->ai_addr)));
                        break;
                    }

                    case AF_INET6: {
                        ips->Add(hx::asys::libuv::net::ip_from_sockaddr(reinterpret_cast<sockaddr_in6*>(info->ai_addr)));
                        break;
                    }

                    // TODO : What should we do if its another type?
                }

                info = info->ai_next;
            } while (nullptr != info);

            Dynamic(spData->cbSuccess.rooted)(ips);
        }
    };

    auto hints = addrinfo();
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    auto result = uv_getaddrinfo(libuvCtx->uvLoop, request.get(), wrapper, host.utf8_str(), nullptr, &hints);

    if (result < 0)
    {
        cbFailure(hx::asys::libuv::uv_err_to_enum(result));
    }
    else
    {
        request->data = data.get();

        request.release();
        data.release();
    }
}

void hx::asys::net::dns::reverse(Context ctx, const Ipv4Address ip, Dynamic cbSuccess, Dynamic cbFailure)
{
    auto libuvCtx = hx::asys::libuv::context(ctx);
    auto data     = std::make_unique<hx::asys::libuv::BaseRequest>(cbSuccess, cbFailure);
    auto request  = std::make_unique<uv_getnameinfo_t>();
    
    auto addr   = hx::asys::libuv::net::sockaddr_from_int(ip);
    auto result = uv_getnameinfo(libuvCtx->uvLoop, request.get(), hostname_callback, reinterpret_cast<sockaddr*>(&addr), 0);

    if (result < 0)
    {
        cbFailure(hx::asys::libuv::uv_err_to_enum(result));
    }
    else
    {
        request->data = data.get();

        request.release();
        data.release();
    }
}

void hx::asys::net::dns::reverse(Context ctx, const Ipv6Address ip, Dynamic cbSuccess, Dynamic cbFailure)
{
    auto libuvCtx = hx::asys::libuv::context(ctx);
    auto data     = std::make_unique<hx::asys::libuv::BaseRequest>(cbSuccess, cbFailure);
    auto request  = std::make_unique<uv_getnameinfo_t>();
    
    auto addr   = hx::asys::libuv::net::sockaddr_from_data(ip);
    auto result = uv_getnameinfo(libuvCtx->uvLoop, request.get(), hostname_callback, reinterpret_cast<sockaddr*>(&addr), 0);

    if (result < 0)
    {
        cbFailure(hx::asys::libuv::uv_err_to_enum(result));
    }
    else
    {
        request->data = data.get();

        request.release();
        data.release();
    }
}