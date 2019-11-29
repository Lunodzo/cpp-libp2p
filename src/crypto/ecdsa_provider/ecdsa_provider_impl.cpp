/**
 * Copyright Soramitsu Co., Ltd. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "libp2p/crypto/ecdsa_provider/ecdsa_provider_impl.hpp"

#include <gsl/span>
#include <gsl/gsl_util>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <openssl/obj_mac.h>
#include "libp2p/crypto/error.hpp"
#include "libp2p/crypto/sha/sha256.hpp"
#include "libp2p/crypto/common_functions.hpp"

namespace libp2p::crypto::ecdsa {
  outcome::result<KeyPair> EcdsaProviderImpl::GenerateKeyPair() const {
    std::shared_ptr<EC_KEY> ec_key{
        EC_KEY_new_by_curve_name(NID_X9_62_prime256v1), EC_KEY_free};
    EC_KEY_set_asn1_flag(ec_key.get(), OPENSSL_EC_NAMED_CURVE);
    if (EC_KEY_generate_key(ec_key.get()) != 1) {
      return KeyGeneratorError::KEY_GENERATION_FAILED;
    }
    OUTCOME_TRY(private_key,
                ConvertEcKeyToBytes<PrivateKey>(ec_key, i2d_ECPrivateKey));
    OUTCOME_TRY(public_key,
                ConvertEcKeyToBytes<PublicKey>(ec_key, i2d_EC_PUBKEY));
    return KeyPair{private_key, public_key};
  }

  outcome::result<PublicKey> EcdsaProviderImpl::DerivePublicKey(
      const PrivateKey &key) const {
    OUTCOME_TRY(ec_key, ConvertBytesToEcKey(key, d2i_ECPrivateKey));
    const BIGNUM *private_num = EC_KEY_get0_private_key(ec_key.get());
    EC_POINT *ec_point = EC_POINT_new(EC_KEY_get0_group(ec_key.get()));
    auto free_ec_point =
        gsl::finally([ec_point]() { EC_POINT_free(ec_point); });
    if (EC_POINT_mul(EC_KEY_get0_group(ec_key.get()), ec_point, private_num,
                     nullptr, nullptr, nullptr)
        != 1) {
      return KeyGeneratorError::KEY_GENERATION_FAILED;
    }
    if (EC_KEY_set_public_key(ec_key.get(), ec_point) != 1) {
      return KeyGeneratorError::KEY_GENERATION_FAILED;
    }
    OUTCOME_TRY(public_key,
                ConvertEcKeyToBytes<PublicKey>(ec_key, i2d_EC_PUBKEY));
    return public_key;
  }

  outcome::result<Signature> EcdsaProviderImpl::Sign(
      gsl::span<uint8_t> message, const PrivateKey &key) const {
    common::Hash256 digest = sha256(message);
    OUTCOME_TRY(ec_key, ConvertBytesToEcKey(key, d2i_ECPrivateKey));
    OUTCOME_TRY(signature, GenerateEcSignature(digest, ec_key));
    return std::move(signature);
  }

  outcome::result<bool> EcdsaProviderImpl::Verify(gsl::span<uint8_t> message,
                                                  const Signature &signature,
                                                  const PublicKey &key) const {
    common::Hash256 digest = sha256(message);
    OUTCOME_TRY(ec_key, ConvertBytesToEcKey(key, d2i_EC_PUBKEY));
    OUTCOME_TRY(signature_status, VerifyEcSignature(digest, signature, ec_key));
    return signature_status;
  }

  template <typename KeyType>
  outcome::result<KeyType> EcdsaProviderImpl::ConvertEcKeyToBytes(
      const std::shared_ptr<EC_KEY> &ec_key,
      int (*converter)(EC_KEY *, uint8_t **)) const {
    KeyType key{};
    int generated_size = converter(ec_key.get(), nullptr);
    if (generated_size != key.size()) {
      return KeyValidatorError::DIFFERENT_KEY_TYPES;
    }
    uint8_t *key_ptr = key.data();
    generated_size = converter(ec_key.get(), &key_ptr);
    if (generated_size != key.size()) {
      return KeyGeneratorError::INTERNAL_ERROR;
    }
    return key;
  }

  template <typename KeyType>
  outcome::result<std::shared_ptr<EC_KEY>>
  EcdsaProviderImpl::ConvertBytesToEcKey(const KeyType &key,
                                         EC_KEY *(*converter)(EC_KEY **,
                                                              const uint8_t **,
                                                              long)) const {
    const uint8_t *key_ptr = key.data();
    std::shared_ptr<EC_KEY> ec_key{converter(nullptr, &key_ptr, key.size()),
                                   EC_KEY_free};
    if (ec_key == nullptr) {
      return KeyValidatorError::DIFFERENT_KEY_TYPES;
    }
    if (EC_KEY_check_key(ec_key.get()) != 1) {
      return KeyGeneratorError::INTERNAL_ERROR;
    }
    return ec_key;
  }
};  // namespace libp2p::crypto::ecdsa
