// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/cert/ct_log_response_parser.h"

#include <memory>

#include "base/base64.h"
#include "base/json/json_value_converter.h"
#include "base/logging.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "base/values.h"
#include "net/cert/ct_serialization.h"
#include "net/cert/signed_tree_head.h"
#include "starboard/memory.h"

namespace net {

namespace ct {

namespace {

// Structure for making JSON decoding easier. The string fields
// are base64-encoded so will require further decoding.
struct JsonSignedTreeHead {
  int tree_size;
  double timestamp;
  std::string sha256_root_hash;
  DigitallySigned signature;

  static void RegisterJSONConverter(
      base::JSONValueConverter<JsonSignedTreeHead>* converted);
};

bool ConvertSHA256RootHash(base::StringPiece s, std::string* result) {
  return base::Base64Decode(s, result) && result->size() == kSthRootHashLength;
}

bool ConvertTreeHeadSignature(base::StringPiece s, DigitallySigned* result) {
  std::string tree_head_signature;
  if (!base::Base64Decode(s, &tree_head_signature)) {
    return false;
  }

  base::StringPiece sp(tree_head_signature);
  return DecodeDigitallySigned(&sp, result);
}

void JsonSignedTreeHead::RegisterJSONConverter(
    base::JSONValueConverter<JsonSignedTreeHead>* converter) {
  converter->RegisterIntField("tree_size", &JsonSignedTreeHead::tree_size);
  converter->RegisterDoubleField("timestamp", &JsonSignedTreeHead::timestamp);
  converter->RegisterCustomField("sha256_root_hash",
                                 &JsonSignedTreeHead::sha256_root_hash,
                                 &ConvertSHA256RootHash);
  converter->RegisterCustomField<DigitallySigned>(
      "tree_head_signature",
      &JsonSignedTreeHead::signature,
      &ConvertTreeHeadSignature);
}

bool IsJsonSTHStructurallyValid(const JsonSignedTreeHead& sth) {
  return sth.tree_size >= 0 && sth.timestamp >= 0 &&
         !sth.sha256_root_hash.empty() && !sth.signature.signature_data.empty();
}

// Structure for making JSON decoding easier. The string fields
// are base64-encoded so will require further decoding.
struct JsonConsistencyProof {
  std::vector<std::unique_ptr<std::string>> proof_nodes;

  static void RegisterJSONConverter(
      base::JSONValueConverter<JsonConsistencyProof>* converter);
};

bool ConvertIndividualProofNode(const base::Value* value, std::string* result) {
  std::string b64_encoded_node;
  if (!value->GetAsString(&b64_encoded_node))
    return false;

  if (!ConvertSHA256RootHash(b64_encoded_node, result))
    return false;

  return true;
}

void JsonConsistencyProof::RegisterJSONConverter(
    base::JSONValueConverter<JsonConsistencyProof>* converter) {
  converter->RegisterRepeatedCustomValue<std::string>(
      "consistency", &JsonConsistencyProof::proof_nodes,
      &ConvertIndividualProofNode);
}

}  // namespace

bool FillSignedTreeHead(const base::Value& json_signed_tree_head,
                        SignedTreeHead* signed_tree_head) {
  JsonSignedTreeHead parsed_sth;
  base::JSONValueConverter<JsonSignedTreeHead> converter;
  if (!converter.Convert(json_signed_tree_head, &parsed_sth) ||
      !IsJsonSTHStructurallyValid(parsed_sth)) {
    return false;
  }

  signed_tree_head->version = SignedTreeHead::V1;
  signed_tree_head->tree_size = parsed_sth.tree_size;
  signed_tree_head->timestamp = base::Time::FromJsTime(parsed_sth.timestamp);
  signed_tree_head->signature = parsed_sth.signature;
  memcpy(signed_tree_head->sha256_root_hash,
               parsed_sth.sha256_root_hash.c_str(), kSthRootHashLength);
  return true;
}

bool FillConsistencyProof(const base::Value& json_consistency_proof,
                          std::vector<std::string>* consistency_proof) {
  JsonConsistencyProof parsed_proof;
  base::JSONValueConverter<JsonConsistencyProof> converter;
  if (!converter.Convert(json_consistency_proof, &parsed_proof)) {
    return false;
  }

  const base::DictionaryValue* dict_value = NULL;
  if (!json_consistency_proof.GetAsDictionary(&dict_value) ||
      !dict_value->FindKey("consistency")) {
    return false;
  }

  consistency_proof->reserve(parsed_proof.proof_nodes.size());
  for (const auto& proof_node : parsed_proof.proof_nodes) {
    consistency_proof->push_back(*proof_node);
  }

  return true;
}

}  // namespace ct

}  // namespace net
