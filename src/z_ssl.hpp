#pragma once
#include <memory>
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>
#include "z_fd.hpp"

// place it at the beginning of `main()`
#define z_ssl_init() \
    [[maybe_unused]] z_ssl::Init __z_ssl_init{}

struct z_ssl {
    z_ssl() = delete;
    ~z_ssl() = delete;

    // Initializes the wolfSSL library for use.
    // Must be called once per application and before any other call to the library.
    struct [[nodiscard]] Init {
        Init() noexcept {
            wolfSSL_Init();
        }
        ~Init() noexcept {
            wolfSSL_Cleanup();
        }
    };

    struct CTX_Deleter {
        void operator()(WOLFSSL_CTX *ctx) const noexcept {
            if (ctx) wolfSSL_CTX_free(ctx);
        }
    };
    using CTX_Ptr = std::unique_ptr<WOLFSSL_CTX, CTX_Deleter>;

    static CTX_Ptr CTX_new(WOLFSSL_METHOD *method) noexcept;
};

struct z_SSL : protected z_Fd {
private:
    WOLFSSL *ssl = nullptr;

    z_SSL(int fd, WOLFSSL_CTX *ssl_ctx) noexcept
        : z_Fd{fd}, ssl{wolfSSL_new(ssl_ctx)} {}

    ~z_SSL() noexcept { /* close(); */ }

public:
    z_ref_counted(z_SSL);
    z_ref_creator(z_SSL);

    // todo
};
