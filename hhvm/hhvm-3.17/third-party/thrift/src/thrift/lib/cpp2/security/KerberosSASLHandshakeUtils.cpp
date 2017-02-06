/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements. See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership. The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License. You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied. See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <thrift/lib/cpp2/security/KerberosSASLHandshakeUtils.h>

#include <gssapi/gssapi_generic.h>
#include <gssapi/gssapi_krb5.h>
#include <krb5.h>

#include <folly/io/IOBuf.h>
#include <folly/io/Cursor.h>

#include <vector>

using namespace std;
using namespace apache::thrift;
using namespace folly;

void KerberosSASLHandshakeUtils::GSSBufferFreeFunction(
  void* /*buf*/, void *arg) {
  gss_buffer_desc* ptr = static_cast<gss_buffer_desc*>(arg);
  GSSBufferDeleter()(ptr);
}

/**
 * Some utility functions.
 */
unique_ptr<folly::IOBuf> KerberosSASLHandshakeUtils::wrapMessage(
  gss_ctx_id_t context,
  unique_ptr<folly::IOBuf>&& buf) {

#ifdef GSSAPI_EXT_H_
  uint64_t numElements = buf->countChainElements();

  // Allocate iov vector with header | data blocks ... | padding | trailer
  std::vector<gss_iov_buffer_desc> iov(numElements + 3);
  uint64_t headerIdx = 0;
  uint64_t paddingIdx = numElements + 1;
  uint64_t trailerIdx = numElements + 2;
  iov[headerIdx].type = GSS_IOV_BUFFER_TYPE_HEADER;

  uint64_t count = 1;
  IOBuf *current = buf.get();
  do {
    iov[count].type = GSS_IOV_BUFFER_TYPE_DATA;
    iov[count].buffer.value = (void *)current->writableData();
    iov[count].buffer.length = current->length();
    count++;
    current = current->next();
  } while (current != buf.get());

  iov[paddingIdx].type = GSS_IOV_BUFFER_TYPE_PADDING;
  iov[trailerIdx].type = GSS_IOV_BUFFER_TYPE_TRAILER;

  // Compute required header / padding / trailer lengths
  OM_uint32 maj_stat, min_stat;
  maj_stat = gss_wrap_iov_length(
    &min_stat,
    context,
    1,
    GSS_C_QOP_DEFAULT,
    nullptr,
    &iov[0],
    iov.size());
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error constructing iov chain", maj_stat, min_stat);
  }

  // Allocate the additional buffers
  std::unique_ptr<IOBuf> header = IOBuf::create(
    iov[headerIdx].buffer.length);
  header->append(iov[headerIdx].buffer.length);
  std::unique_ptr<IOBuf> padding = IOBuf::create(
    iov[paddingIdx].buffer.length);
  padding->append(iov[paddingIdx].buffer.length);
  std::unique_ptr<IOBuf> trailer = IOBuf::create(
    iov[trailerIdx].buffer.length);
  trailer->append(iov[trailerIdx].buffer.length);
  iov[headerIdx].buffer.value = (void *)header->writableData();
  iov[paddingIdx].buffer.value = (void *)padding->writableData();
  iov[trailerIdx].buffer.value = (void *)trailer->writableData();

  // Link all the buffers in a chain
  header->prependChain(std::move(buf));
  header->prependChain(std::move(padding));
  header->prependChain(std::move(trailer));

  // Encrypt in place
  maj_stat = gss_wrap_iov(
    &min_stat,
    context,
    1, // conf and integrity requested
    GSS_C_QOP_DEFAULT,
    nullptr,
    &iov[0],
    iov.size()
  );
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error wrapping message", maj_stat, min_stat);
  }

  return header;
#else
  // Don't bother with getting things working on an older platform.
  // Things should never reach this point anyway, because security will
  // be disabled at a higher level.
  throw TKerberosException(
    "Linking against older version of krb5 without support for security.");
  return std::move(buf);
#endif
}

unique_ptr<folly::IOBuf> KerberosSASLHandshakeUtils::unwrapMessage(
  gss_ctx_id_t context,
  unique_ptr<folly::IOBuf>&& buf) {

#ifdef GSSAPI_EXT_H_
  // Unfortunately we have to coalesce here. We can probably use the
  // alternate iov api, but that requires knowing the details of the
  // token's boxing.
  buf->coalesce();

  gss_iov_buffer_desc iov[2];
  iov[0].type = GSS_IOV_BUFFER_TYPE_STREAM;
  iov[0].buffer.value = (void *) buf->writableData();
  iov[0].buffer.length = buf->length();
  iov[1].type = GSS_IOV_BUFFER_TYPE_DATA;

  OM_uint32 maj_stat, min_stat;
  int state;
  maj_stat = gss_unwrap_iov(
    &min_stat,
    context,
    &state,
    (gss_qop_t *) nullptr, // quality of protection output...
    iov,
    2
  );
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error unwrapping message", maj_stat, min_stat);

  }

  // The buffer was decrypted in-place. There is still some junk around
  // the plaintext though. Let's trim it.
  uint64_t headerSize =
    (uint64_t) iov[1].buffer.value - (uint64_t) buf->data();
  uint64_t trailerSize = buf->length() - headerSize - iov[1].buffer.length;
  buf->trimStart(headerSize);
  buf->trimEnd(trailerSize);
#else
  // Don't bother with getting things working on an older platform.
  // Things should never reach this point anyway, because security will
  // be disabled at a higher level.
  throw TKerberosException(
    "Linking against older version of krb5 without support for security.");
#endif

  return std::move(buf);
}

string KerberosSASLHandshakeUtils::getStatusHelper(
  OM_uint32 code,
  int type) {

  OM_uint32 min_stat;
  OM_uint32 msg_ctx = 0;
  string output;

  unique_ptr<gss_buffer_desc, GSSBufferDeleter> out_buf(new gss_buffer_desc);
  *out_buf = GSS_C_EMPTY_BUFFER;

  while (true) {
    gss_display_status(
      &min_stat,
      code,
      type,
      (gss_OID) gss_mech_krb5, // mech type, default to krb 5
      &msg_ctx,
      out_buf.get()
    );
    output += " " + string((char *)out_buf->value);
    (void) gss_release_buffer(&min_stat, out_buf.get());
    if (!msg_ctx) {
      break;
    }
  }
  return output;
}

string KerberosSASLHandshakeUtils::getStatus(
  OM_uint32 maj_stat,
  OM_uint32 min_stat) {

  string output;
  output += getStatusHelper(maj_stat, GSS_C_GSS_CODE);
  output += ";" + getStatusHelper(min_stat, GSS_C_MECH_CODE);
  return output;
}

string KerberosSASLHandshakeUtils::throwGSSException(
  const string& msg,
  OM_uint32 maj_stat,
  OM_uint32 min_stat) {

  throw TKerberosException(msg + getStatus(maj_stat, min_stat));
}

void KerberosSASLHandshakeUtils::getContextData(
  gss_ctx_id_t context,
  OM_uint32& context_lifetime,
  OM_uint32& context_security_flags,
  string& client_principal,
  string& service_principal) {

  OM_uint32 maj_stat, min_stat;

  // Acquire name buffers
  unique_ptr<gss_name_t, GSSNameDeleter> client_name(new gss_name_t);
  unique_ptr<gss_name_t, GSSNameDeleter> service_name(new gss_name_t);

  maj_stat = gss_inquire_context(
    &min_stat,
    context,
    client_name.get(),
    service_name.get(),
    &context_lifetime,
    nullptr, // mechanism
    &context_security_flags,
    nullptr, // is local
    nullptr // is open
  );
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error inquiring context", maj_stat, min_stat);
  }
  std::unique_ptr<gss_buffer_desc, GSSBufferDeleter> client_name_buf(
    new gss_buffer_desc);
  std::unique_ptr<gss_buffer_desc, GSSBufferDeleter> service_name_buf(
    new gss_buffer_desc);
  *client_name_buf = GSS_C_EMPTY_BUFFER;
  *service_name_buf = GSS_C_EMPTY_BUFFER;

  maj_stat = gss_display_name(
    &min_stat,
    *client_name.get(),
    client_name_buf.get(),
    (gss_OID *) nullptr);
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error getting client name", maj_stat, min_stat);
  }

  maj_stat = gss_display_name(
    &min_stat,
    *service_name.get(),
    service_name_buf.get(),
    (gss_OID *) nullptr);
  if (maj_stat != GSS_S_COMPLETE) {
    KerberosSASLHandshakeUtils::throwGSSException(
      "Error getting service name", maj_stat, min_stat);
  }

  client_principal = string(
    (char *)client_name_buf->value,
    client_name_buf->length);
  service_principal = string(
    (char *)service_name_buf->value,
    service_name_buf->length);
}
