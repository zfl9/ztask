#pragma once
#include <memory>
#include <wolfssl/options.h>
#include <wolfssl/ssl.h>

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
            wolfSSL_CTX_free(ctx);
        }
    };

    struct SSL_Deleter {
        void operator()(WOLFSSL *ssl) const noexcept {
            wolfSSL_free(ssl);
        }
    };

    using UniqueCTX = std::unique_ptr<WOLFSSL_CTX, CTX_Deleter>;
    using UniqueSSL = std::unique_ptr<WOLFSSL, SSL_Deleter>;

    static UniqueCTX CTX_new(WOLFSSL_METHOD *method) noexcept;
    static UniqueSSL SSL_new(WOLFSSL_CTX *ctx) noexcept;

    // todo
};
