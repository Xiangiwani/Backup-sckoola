#pragma once
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <array>
#include <algorithm>
#include <string>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <cstdlib>

// Versioned streaming envelope: magic, 16-byte salt, 12-byte nonce,
// ciphertext, 16-byte GCM tag. Header is authenticated as AAD.
namespace cb_crypto {
inline void check(bool ok, const char *msg) { if(!ok) throw std::runtime_error(msg); }
inline void transform(const std::filesystem::path &input, const std::filesystem::path &output,
                      const std::string &password_env, bool encrypt) {
    const char *password=std::getenv(password_env.c_str());
    check(password && std::char_traits<char>::length(password)>=12,
          "Encryption requires a password of at least 12 bytes in the configured password_env environment variable.");
    std::ifstream in(input,std::ios::binary);
    check(bool(in),"Cannot read encryption input");
    std::array<unsigned char,36> header{};
    const std::array<unsigned char,8> magic={'C','B','A','E','S','0','0','1'};
    if(encrypt) {
        std::copy(magic.begin(),magic.end(),header.begin());
        check(RAND_bytes(header.data()+8,28)==1,"Secure random generation failed");
    } else {
        in.read(reinterpret_cast<char*>(header.data()),header.size());
        check(in.gcount()==(std::streamsize)header.size() && std::equal(magic.begin(),magic.end(),header.begin()),"Invalid encrypted archive header");
    }
    std::array<unsigned char,32> key{};
    check(PKCS5_PBKDF2_HMAC(password,static_cast<int>(std::char_traits<char>::length(password)),header.data()+8,16,
                          600000,EVP_sha256(),key.size(),key.data())==1,"Password key derivation failed");
    std::unique_ptr<EVP_CIPHER_CTX,decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(),EVP_CIPHER_CTX_free);
    check(bool(ctx),"Cipher allocation failed");
    int initialized=EVP_CipherInit_ex(ctx.get(),EVP_aes_256_gcm(),nullptr,key.data(),header.data()+24,encrypt?1:0);
    OPENSSL_cleanse(key.data(),key.size());
    check(initialized==1,"Cipher initialization failed");
    int n=0;
    check(EVP_CipherUpdate(ctx.get(),nullptr,&n,header.data(),header.size())==1,"Header authentication failed");
    try {
        std::ofstream out(output,std::ios::binary|std::ios::trunc);
        check(bool(out),"Cannot write encryption output");
        if(encrypt) out.write(reinterpret_cast<const char*>(header.data()),header.size());
        uintmax_t remaining=std::filesystem::file_size(input);
        if(!encrypt) { check(remaining>=header.size()+16,"Truncated encrypted archive"); remaining-=header.size()+16; }
        std::array<unsigned char,131072> buffer{};
        std::array<unsigned char,131104> encoded{};
        while(remaining) {
            auto count=static_cast<std::streamsize>(std::min<uintmax_t>(remaining,buffer.size()));
            in.read(reinterpret_cast<char*>(buffer.data()),count);
            check(in.gcount()==count,"Truncated encryption input");
            check(EVP_CipherUpdate(ctx.get(),encoded.data(),&n,buffer.data(),static_cast<int>(count))==1,"Cipher update failed");
            out.write(reinterpret_cast<char*>(encoded.data()),n); remaining-=count;
        }
        std::array<unsigned char,16> tag{};
        if(!encrypt) {
            in.read(reinterpret_cast<char*>(tag.data()),tag.size());
            check(in.gcount()==16,"Missing authentication tag");
            check(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_SET_TAG,16,tag.data())==1,"Cannot set authentication tag");
        }
        check(EVP_CipherFinal_ex(ctx.get(),encoded.data(),&n)==1,"Authentication failed: wrong password or damaged encrypted backup");
        out.write(reinterpret_cast<char*>(encoded.data()),n);
        if(encrypt) {
            check(EVP_CIPHER_CTX_ctrl(ctx.get(),EVP_CTRL_GCM_GET_TAG,16,tag.data())==1,"Cannot get authentication tag");
            out.write(reinterpret_cast<char*>(tag.data()),tag.size());
        }
        out.flush(); check(bool(out),"Encryption output write failed");
    } catch(...) { std::error_code ec; std::filesystem::remove(output,ec); throw; }
}
}
